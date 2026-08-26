// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QApplication>
#include <QFont>
#include <QLabel>
#include <QScopeGuard>
#include <QTest>
#include <QWidget>

#include "src/App/InitApplication.h"

#include <Gui/Application.h>
#include <Gui/FreeCADStyle.h>
#include <Gui/StyleParameters/ParameterManager.h>
#include <Gui/StyleParameters/StyleContext.h>

// Deliberately unlike any system default, and no two numbers equal, so a wrong token cannot
// coincidentally match.
constexpr int buttonPixelSize = 13;
constexpr int buttonWeight = 700;
constexpr double lineEditPointSize = 11.0;
constexpr double listSizeFactor = 1.5;
constexpr double treeSizeFactor = 2.0;

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
                    {.name = "LineEditFontSize", .value = "11pt"},
                    {.name = "ListFontSize", .value = "1.5em"},
                    {.name = "TreeFontSize", .value = "2rem"},
                    {.name = "MenuFontWeight", .value = "5000"},
                },
                {.name = "Widget Fonts Fixture"}
            )
        );
    }

private:
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
};

QTEST_MAIN(TestWidgetFonts)

#include "WidgetFonts.moc"
