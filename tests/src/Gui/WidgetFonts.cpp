// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QApplication>
#include <QFont>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QScopeGuard>
#include <QTabBar>
#include <QTest>
#include <QToolButton>
#include <QWidget>

#include "src/App/InitApplication.h"

#include <Gui/Application.h>
#include <Gui/FreeCADStyle.h>
#include <Gui/StyleParameters/ParameterManager.h>
#include <Gui/StyleParameters/StyleContext.h>
#include <Gui/ThemeReloadEvent.h>

// Deliberately unlike any system default, and no two numbers equal, so a wrong token cannot
// coincidentally match.
constexpr int buttonPixelSize = 13;
constexpr int buttonWeight = 700;
constexpr int primaryButtonWeight = 900;
constexpr double lineEditPointSize = 11.0;
constexpr double listSizeFactor = 1.5;
constexpr double treeSizeFactor = 2.0;
constexpr int groupBoxPixelSize = 19;
constexpr int reloadedButtonPixelSize = 21;

// applyWidgetFonts() is reached through the style's protected eventFilter(); the test needs a
// way in that does not also drag in Application's own filter, which reapplies the stylesheet
// through a main window a headless test does not have.
class ReloadableStyle: public Gui::FreeCADStyle
{
public:
    using Gui::FreeCADStyle::eventFilter;
};

class TestWidgetFonts: public QObject
{
    Q_OBJECT

public:
    TestWidgetFonts()
    {
        tests::initApplication();

        if (Gui::Application::Instance == nullptr) {
            new Gui::Application(false);
        }

        Gui::Application::Instance->styleParameterManager()->addSource(
            new Gui::StyleParameters::InMemoryParameterSource(
                {
                    {.name = "ButtonFontSize", .value = "13px"},
                    {.name = "ButtonFontWeight", .value = "700"},
                    {.name = "ButtonFontFamily", .value = "'Fixture Sans, Fixture Fallback'"},
                    {.name = "ButtonFontStyle", .value = "italic"},
                    {.name = "ButtonPrimaryFontWeight", .value = "900"},
                    {.name = "ButtonSmallFontSize", .value = "9px"},
                    {.name = "LineEditFontSize", .value = "11pt"},
                    {.name = "ListFontSize", .value = "1.5em"},
                    {.name = "TreeFontSize", .value = "2rem"},
                    {.name = "MenuFontWeight", .value = "5000"},
                    {.name = "GroupBoxFontSize", .value = "19px"},
                },
                {.name = "Widget Fonts Fixture"}
            )
        );

        // Registered last so it outranks the fixture above, and left empty so it costs nothing
        // until a test asks for a different value.
        overrides = new Gui::StyleParameters::InMemoryParameterSource(
            {},
            {.name = "Widget Fonts Overrides"}
        );
        Gui::Application::Instance->styleParameterManager()->addSource(overrides);
    }

private:
    Gui::StyleParameters::InMemoryParameterSource* overrides = nullptr;

    // Swaps one token in for the body of a test and puts the fixture's value back on the way
    // out, so an assertion that returns early cannot leak it into the next test.
    [[nodiscard]] auto overrideToken(const std::string& name, const std::string& value) const
    {
        auto* manager = Gui::Application::Instance->styleParameterManager();

        overrides->define({.name = name, .value = value});
        manager->reload();

        return qScopeGuard([manager, this, name]() {
            overrides->remove(name);
            manager->reload();
        });
    }

    static Gui::StyleParameters::StyleContext contextFor(Gui::StyleParameters::StyleComponent component)
    {
        Gui::StyleParameters::StyleContext context;
        context.component = component;
        return context;
    }

private Q_SLOTS:

    // The polish pass applies this font with setFont(), so it must carry the tokens and nothing
    // else: an attribute in the mask that no token stated would overwrite the widget's own.
    void test_tokenFontCarriesOnlyTheAttributesTokensStated()  // NOLINT
    {
        Gui::FreeCADStyle style;

        QFont base;
        base.setPixelSize(30);
        base.setFamily(QStringLiteral("Some Deliberate Family"));

        const QFont token = style.resolveTokenFont(
            contextFor(Gui::StyleParameters::StyleComponent::LineEdit),
            base
        );

        QCOMPARE(token.pointSizeF(), lineEditPointSize);
        QVERIFY(token.resolveMask() & QFont::SizeResolved);
        QVERIFY((token.resolveMask() & QFont::WeightResolved) == 0);
        QVERIFY((token.resolveMask() & QFont::FamiliesResolved) == 0);
    }

    // A component with no font tokens must be distinguishable from one with them, because the
    // polish pass uses an empty mask to mean "leave this widget's font alone".
    void test_tokenFontIsEmptyWithoutTokens()  // NOLINT
    {
        Gui::FreeCADStyle style;

        const QFont token = style.resolveTokenFont(
            contextFor(Gui::StyleParameters::StyleComponent::TabBar),
            QFont()
        );

        QCOMPARE(token.resolveMask(), quint32 {0});
    }

    void test_pixelWeightFamilyAndStyleTokensAllApply()  // NOLINT
    {
        Gui::FreeCADStyle style;

        const QFont token = style.resolveTokenFont(
            contextFor(Gui::StyleParameters::StyleComponent::PushButton),
            QFont()
        );

        QCOMPARE(token.pixelSize(), buttonPixelSize);
        QCOMPARE(static_cast<int>(token.weight()), buttonWeight);
        QCOMPARE(token.style(), QFont::StyleItalic);
        QCOMPARE(
            token.families(),
            QStringList({QStringLiteral("Fixture Sans"), QStringLiteral("Fixture Fallback")})
        );
    }

    // em multiplies the base it is handed, which is what makes a sub-element token such as
    // GroupBoxTitleFontSize cascade off the widget's own font.
    void test_emScalesTheBaseFont()  // NOLINT
    {
        Gui::FreeCADStyle style;

        QFont base;
        base.setPixelSize(20);

        const QFont token
            = style.resolveTokenFont(contextFor(Gui::StyleParameters::StyleComponent::List), base);

        QCOMPARE(token.pixelSize(), static_cast<int>(20 * listSizeFactor));
    }

    // rem ignores the base entirely and multiplies the application font, so a theme can state a
    // ratio that does not change with wherever the widget happens to sit.
    void test_remScalesTheApplicationFontNotTheBase()  // NOLINT
    {
        Gui::FreeCADStyle style;

        const QFont previous = QApplication::font();
        const auto guard = qScopeGuard([previous]() { QApplication::setFont(previous); });

        QFont application = QApplication::font();
        application.setPixelSize(16);
        QApplication::setFont(application);

        QFont base;
        base.setPixelSize(40);

        const QFont token
            = style.resolveTokenFont(contextFor(Gui::StyleParameters::StyleComponent::Tree), base);

        QCOMPARE(token.pixelSize(), static_cast<int>(16 * treeSizeFactor));
    }

    // QFont warns and rejects anything outside 1..1000; a malformed token must not be able to
    // spray the log from inside a paint call.
    void test_weightIsClampedToTheRangeQFontAccepts()  // NOLINT
    {
        Gui::FreeCADStyle style;

        const QFont token
            = style.resolveTokenFont(contextFor(Gui::StyleParameters::StyleComponent::Menu), QFont());

        QCOMPARE(static_cast<int>(token.weight()), 1000);
    }

    // resolveFont keeps its old contract for the paint-time callers: a complete font, with the
    // base showing through wherever no token spoke.
    void test_resolveFontFillsTheGapsFromTheBase()  // NOLINT
    {
        Gui::FreeCADStyle style;

        QFont base;
        base.setPixelSize(30);
        base.setFamily(QStringLiteral("Some Deliberate Family"));

        const QFont resolved
            = style.resolveFont(contextFor(Gui::StyleParameters::StyleComponent::LineEdit), base);

        QCOMPARE(resolved.pointSizeF(), lineEditPointSize);
        QCOMPARE(resolved.family(), QStringLiteral("Some Deliberate Family"));
    }

    // A theme states typography per variant, and Primary is the variant a theme is most likely
    // to restyle. The variant has to reach the font pass from the widget: polish() has no style
    // option to read the DefaultButton feature from.
    void test_polishAppliesThePrimaryVariantFont()  // NOLINT
    {
        Gui::FreeCADStyle style;

        QPushButton button;
        button.setDefault(true);

        style.polish(&button);

        QCOMPARE(static_cast<int>(button.font().weight()), primaryButtonWeight);
    }

    void test_polishAppliesTheComponentFontToTheWidget()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QPushButton button;

        style.polish(&button);

        QCOMPARE(button.font().pixelSize(), buttonPixelSize);
        QCOMPARE(static_cast<int>(button.font().weight()), buttonWeight);
    }

    // ButtonSmall* reached a tool button through a QSS attribute selector. It has to arrive
    // through the ControlSize variant now, or deleting the QSS silently drops it.
    void test_theSmallControlSizeVariantReachesAToolButtonFont()  // NOLINT
    {
        Gui::FreeCADStyle style;

        QToolButton button;
        button.setProperty("controlSize", QStringLiteral("small"));

        style.polish(&button);

        QCOMPARE(button.font().pixelSize(), 9);
    }

    // A theme that stops stating a token has to give the widget its own font back. Without
    // this, dropping a token from a theme would be a one-way change until the next restart.
    void test_polishingUnderAThemeWithoutTheTokenRestoresTheFont()  // NOLINT
    {
        QPushButton button;
        const QFont original = button.font();

        {
            Gui::FreeCADStyle style;
            style.polish(&button);
            QCOMPARE(button.font().pixelSize(), buttonPixelSize);
        }

        // reset() stops the fallback chain, which is how a theme says "no value here".
        const auto guard = overrideToken("ButtonFontSize", "reset()");

        // A fresh style: an instance caches resolved tokens for its lifetime, and only a
        // ThemeReloadEvent it never receives would drop them.
        Gui::FreeCADStyle reloaded;
        reloaded.polish(&button);

        QCOMPARE(button.font().pixelSize(), original.pixelSize());
    }

    // A widget nobody wrote a font token for must come out of polish() untouched, rather than
    // pinned to a font the style invented for it. QTabBar rather than a bare QLabel: a QLabel
    // resolves to StyleComponent::None, which resolves nothing by construction, so that would
    // only prove the early return fires - not the stated claim that a component contextOf()
    // actually recognises, and for which the fixture states no font token, is left alone.
    void test_aComponentWithoutFontTokensIsLeftAlone()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QTabBar plain;
        const QFont original = plain.font();

        style.polish(&plain);

        QCOMPARE(plain.font(), original);
        QVERIFY(!plain.property("_fc_styleFontMask").isValid());
    }

    // Production always has an application stylesheet, and that is what switches Qt's font
    // propagation off: QStyleSheetStyle::initWidget() sets WA_StyleSheet, and
    // QWidgetPrivate::naturalWidgetFont() then never consults the parent. Reproduce that here,
    // or this measures a regime FreeCAD never runs in.
    //
    // This assertion is the tripwire for the day the last QSS rule is deleted: it goes red
    // exactly when propagation comes back, which is when someone needs to know.
    void test_aContainerFontDoesNotReachAChildWithoutOne()  // NOLINT
    {
        const QString previousSheet = qApp->styleSheet();
        const auto sheetGuard = qScopeGuard([previousSheet]() { qApp->setStyleSheet(previousSheet); });

        // QApplication::setStyle() deletes whatever style it replaces once that style's parent
        // is qApp - which it always is by the time this runs, whether it is the lazily-created
        // default or one installed by an earlier call. Detaching the parent first keeps the
        // original object alive across the swap, so it can be handed straight back afterwards:
        // a route that cannot fail, unlike rebuilding one from a factory key, which silently
        // does nothing (setStyle(nullptr) early-returns) for a key no factory owns.
        QStyle* const previousStyle = QApplication::style();
        previousStyle->setParent(nullptr);
        const auto styleGuard = qScopeGuard([previousStyle]() {
            QApplication::setStyle(previousStyle);
        });

        QApplication::setStyle(new Gui::FreeCADStyle);
        qApp->setStyleSheet(QStringLiteral("/* propagation fixture */"));

        QGroupBox box;
        QLabel child(&box);

        box.ensurePolished();
        child.ensurePolished();

        QCOMPARE(box.font().pixelSize(), groupBoxPixelSize);

        // Not just "not the container's": pinned to what an untouched, unparented-by-font
        // widget actually gets, so a wrong value cannot pass by accident.
        QCOMPARE(child.font().pixelSize(), QApplication::font().pixelSize());
    }

    // A widget that was given a font of its own keeps the parts of it no token speaks for. The
    // applied font's mask has to cover both, or setFont() drops the widget's own attributes back
    // to the inherited value.
    void test_polishKeepsAnAttributeTheWidgetSetForItself()  // NOLINT
    {
        // The fixture states ButtonFontFamily; resetting it isolates the case this test exists
        // for; otherwise the token would legitimately win the family and the assertion below
        // would prove nothing about the union.
        const auto guard = overrideToken("ButtonFontFamily", "reset()");

        Gui::FreeCADStyle style;

        QPushButton button;
        QFont own = button.font();
        own.setFamilies({QStringLiteral("Widget Own Family")});
        button.setFont(own);

        style.polish(&button);

        QCOMPARE(button.font().pixelSize(), buttonPixelSize);
        QCOMPARE(button.font().families(), QStringList({QStringLiteral("Widget Own Family")}));
    }

    // unpolish() has to give back exactly what polish() found, or moving a widget away from
    // this style - or simply tearing it down - would leave it wearing a token font forever, and
    // the bookkeeping properties dangling on it besides.
    void test_unpolishRestoresTheOriginalFontAndClearsTheProperties()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QPushButton button;
        const QFont original = button.font();

        style.polish(&button);
        QCOMPARE(button.font().pixelSize(), buttonPixelSize);

        style.unpolish(&button);

        QCOMPARE(button.font(), original);
        QVERIFY(!button.property("_fc_styleFontBase").isValid());
        QVERIFY(!button.property("_fc_styleFontMask").isValid());
    }

    // The preference used to call setFont() directly. It has to speak through the token system
    // now, or the style's own pass would overwrite it on the next polish.
    void test_aFontSizeOverrideBeatsTheComponentToken()  // NOLINT
    {
        QStyle* const previousStyle = QApplication::style();
        previousStyle->setParent(nullptr);
        const auto styleGuard = qScopeGuard([previousStyle]() {
            QApplication::setStyle(previousStyle);
        });

        QApplication::setStyle(new Gui::FreeCADStyle);

        QPushButton button;
        Gui::FreeCADStyle::setStyleOverride(
            &button,
            QStringLiteral("ButtonFontSize"),
            QStringLiteral("21px")
        );

        Gui::FreeCADStyle style;
        style.polish(&button);

        QCOMPARE(button.font().pixelSize(), 21);
    }

    // The reload walk shipped flat once: only the top level and its immediate children picked
    // up a re-themed font, while anything nested deeper kept the stale one. A container inside
    // a top-level window, with the button one level below that, is the shallowest tree that bug
    // would have failed on.
    void test_themeReloadReachesAWidgetNestedTwoLevelsDeep()  // NOLINT
    {
        ReloadableStyle style;

        QWidget window;
        auto* container = new QWidget(&window);
        auto* button = new QPushButton(container);

        style.polish(button);
        QCOMPARE(button->font().pixelSize(), buttonPixelSize);

        // Matches reloadedButtonPixelSize below; a literal here keeps the fixture value the
        // assertion checks readable at the point where it is stated.
        const auto guard = overrideToken("ButtonFontSize", "21px");

        Gui::ThemeReloadEvent reloadEvent;
        style.eventFilter(qApp, &reloadEvent);

        QCOMPARE(button->font().pixelSize(), reloadedButtonPixelSize);
    }
};

QTEST_MAIN(TestWidgetFonts)

#include "WidgetFonts.moc"
