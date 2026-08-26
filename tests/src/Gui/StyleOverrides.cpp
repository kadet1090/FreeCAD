// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QApplication>
#include <QImage>
#include <QPushButton>
#include <QTabBar>
#include <QTabWidget>
#include <QCursor>
#include <QEvent>
#include <QTest>
#include <QWidget>

#include "src/App/InitApplication.h"

#include <Gui/Application.h>
#include <Gui/FreeCADStyle.h>
#include <Gui/StyleParameters/ParameterManager.h>
#include <Gui/StyleParameters/StyleContext.h>
#include <Gui/StyleParameters/StyleOverrides.h>

namespace
{

// Captures, at the exact moment QEvent::StyleChange arrives, whatever this widget itself
// resolves through the production API — the same thing a real widget's changeEvent handler
// (QTabBar::changeEvent, for one) does. QCoreApplication::sendEvent() dispatches synchronously
// regardless of visibility, so no shown QTabBar is needed to observe whether FreeCADStyle::
// polish() has already stored this widget's new override set by the time it dispatches the
// event that triggers such a handler.
class OverrideObservingWidget: public QWidget
{
public:
    using QWidget::QWidget;

    bool observedAStyleChange = false;
    QColor backgroundDuringLastStyleChange;

protected:
    void changeEvent(QEvent* event) override
    {
        QWidget::changeEvent(event);

        if (event->type() != QEvent::StyleChange) {
            return;
        }

        observedAStyleChange = true;
        if (auto* style = qobject_cast<Gui::FreeCADStyle*>(QApplication::style())) {
            backgroundDuringLastStyleChange
                = style->resolveBoxStyle(Gui::FreeCADStyle::contextOf(this)).background.color();
        }
    }
};

}  // namespace

class TestStyleOverrides: public QObject
{
    Q_OBJECT

public:
    TestStyleOverrides()
    {
        tests::initApplication();

        if (Gui::Application::Instance == nullptr) {
            new Gui::Application(true);
        }

        // Attached to plain QWidgets through the "component" property, so no real QTabBar or
        // QTreeView is needed. TestPanelBackground goes through TestPaneBackground, and
        // TestPanelPadding goes through TestPanePadding the same way, so the nested-reference
        // path is what the assertions actually exercise.
        Gui::Application::Instance->styleParameterManager()->addSource(
            new Gui::StyleParameters::InMemoryParameterSource(
                {
                    {.name = "TestPaneBackground", .value = "#112233"},
                    {.name = "TestPanelBackground", .value = "@TestPaneBackground"},
                    {.name = "TestPanePadding", .value = "padding(4px)"},
                    {.name = "TestPanelPadding", .value = "@TestPanePadding"},
                    {.name = "TestPanelBorderColor", .value = "#000000"},
                    {.name = "TestPanelFontSize", .value = "13px"},
                    {.name = "TabBarBackground", .value = "@TestPaneBackground"},

                    // The two tab fills the rendered mapping test tells apart. Flat colours
                    // rather than the theme's gradient, so a sampled pixel is the token and
                    // nothing else, and unmistakably different from each other.
                    {.name = "TabBarTabBackground", .value = "#101010"},
                    {.name = "TabBarTabSelectedBackground", .value = "#00ffff"},
                },
                {.name = "Style Overrides Fixture"}
            )
        );

        // The production entry points read the widget's own style, so the application style
        // has to be the one under test.
        QApplication::setStyle(new Gui::FreeCADStyle);
    }


private:
    static Gui::FreeCADStyle* style()
    {
        return qobject_cast<Gui::FreeCADStyle*>(QApplication::style());
    }

    static Gui::StyleParameters::ParameterManager* manager()
    {
        return Gui::Application::Instance->styleParameterManager();
    }

    /// The colour a widget's Background token resolves to, through the full production path.
    static QColor backgroundOf(const QWidget* widget)
    {
        return style()->resolveBoxStyle(Gui::FreeCADStyle::contextOf(widget)).background.color();
    }

    /// The left padding a widget's Padding token resolves to, through resolveBoxGeometry() —
    /// the one aggregate cache the background-only tests above never touch.
    static qreal paddingOf(const QWidget* widget)
    {
        return style()->resolveBoxGeometry(Gui::FreeCADStyle::contextOf(widget)).padding.left();
    }

    /// How much of @p rect @p canvas paints in @p colour. A tab's fill shares its rect with a
    /// label and whatever inset the shape takes, so it can only be asserted as a presence or an
    /// absence — never as the colour of any one pixel picked in advance.
    static int pixelsOfColour(const QImage& canvas, const QRect& rect, const QColor& colour)
    {
        int found = 0;
        for (int y = rect.top(); y <= rect.bottom(); ++y) {
            for (int x = rect.left(); x <= rect.right(); ++x) {
                if (canvas.rect().contains(x, y) && canvas.pixelColor(x, y) == colour) {
                    ++found;
                }
            }
        }
        return found;
    }

    /// A panel whose Background comes from TestPanelBackground → TestPaneBackground.
    static QWidget* makePanel(QWidget* parent)
    {
        auto* panel = new QWidget(parent);
        panel->setProperty("component", "TestPanel");
        return panel;
    }

private Q_SLOTS:

    void test_aWidgetWithNoOverridesResolvesTheThemeValue()  // NOLINT
    {
        QWidget root;
        QWidget* panel = makePanel(&root);

        QCOMPARE(backgroundOf(panel), QColor(0x11, 0x22, 0x33));
    }

    // This test, and the others below it that write overrideSetProperty directly, stand in for
    // storeOverrideSet() — the header documents that property as style-owned and the sole writer
    // being storeOverrideSet() is what makes overrideSetOf()'s memo sound. Writing it here is
    // still safe because each widget is freshly constructed and the property is set before any
    // resolve, so there is no stale memo to invalidate; doing it this way lets the test target
    // cache binning against an arbitrary interned set directly, without going through polish()'s
    // collection walk. Do not take this as licence to write the property from production code.
    void test_aStoredSetChangesWhatTheWidgetResolves()  // NOLINT
    {
        QWidget root;
        QWidget* panel = makePanel(&root);

        const uint32_t identifier = manager()->overrideRegistry().intern(
            {{"TestPaneBackground", "#445566"}}
        );
        panel->setProperty(Gui::FreeCADStyle::overrideSetProperty, identifier);

        QCOMPARE(backgroundOf(panel), QColor(0x44, 0x55, 0x66));
    }

    // If the token cache were still one flat map, whichever widget painted first would decide
    // the colour for the other. Resolving both in one test is what catches that.
    void test_twoSetsDoNotShareCacheEntries()  // NOLINT
    {
        QWidget root;
        QWidget* first = makePanel(&root);
        QWidget* second = makePanel(&root);

        first->setProperty(
            Gui::FreeCADStyle::overrideSetProperty,
            manager()->overrideRegistry().intern({{"TestPaneBackground", "#445566"}})
        );
        second->setProperty(
            Gui::FreeCADStyle::overrideSetProperty,
            manager()->overrideRegistry().intern({{"TestPaneBackground", "#778899"}})
        );

        QCOMPARE(backgroundOf(first), QColor(0x44, 0x55, 0x66));
        QCOMPARE(backgroundOf(second), QColor(0x77, 0x88, 0x99));

        // ...and again in the opposite order, now that both are cached.
        QCOMPARE(backgroundOf(second), QColor(0x77, 0x88, 0x99));
        QCOMPARE(backgroundOf(first), QColor(0x44, 0x55, 0x66));
    }

    // Same guarantee as test_twoSetsDoNotShareCacheEntries, but through resolveBoxGeometry()
    // rather than resolveBoxStyle() — the background-only tests above never call it, so a
    // boxGeometryCache that forgot to bin by override set would pass every other test here.
    void test_twoSetsDoNotShareGeometryCacheEntries()  // NOLINT
    {
        QWidget root;
        QWidget* first = makePanel(&root);
        QWidget* second = makePanel(&root);

        first->setProperty(
            Gui::FreeCADStyle::overrideSetProperty,
            manager()->overrideRegistry().intern({{"TestPanePadding", "padding(8px)"}})
        );
        second->setProperty(
            Gui::FreeCADStyle::overrideSetProperty,
            manager()->overrideRegistry().intern({{"TestPanePadding", "padding(16px)"}})
        );

        QCOMPARE(paddingOf(first), 8.0);
        QCOMPARE(paddingOf(second), 16.0);

        // ...and again in the opposite order, now that both are cached.
        QCOMPARE(paddingOf(second), 16.0);
        QCOMPARE(paddingOf(first), 8.0);
    }

    // An overridden widget must not leave its colour in the bin every other widget reads.
    void test_anOverriddenWidgetDoesNotAffectAPlainOne()  // NOLINT
    {
        QWidget root;
        QWidget* overridden = makePanel(&root);
        QWidget* plain = makePanel(&root);

        overridden->setProperty(
            Gui::FreeCADStyle::overrideSetProperty,
            manager()->overrideRegistry().intern({{"TestPaneBackground", "#445566"}})
        );

        QCOMPARE(backgroundOf(overridden), QColor(0x44, 0x55, 0x66));
        QCOMPARE(backgroundOf(plain), QColor(0x11, 0x22, 0x33));
    }

    void test_aDeclarationOnTheWidgetItselfApplies()  // NOLINT
    {
        QWidget root;
        QWidget* panel = makePanel(&root);
        panel->setProperty("fcStyleTestPaneBackground", "#445566");

        style()->polish(panel);

        QCOMPARE(backgroundOf(panel), QColor(0x44, 0x55, 0x66));
    }

    // The declaration goes on the container; the widget that consumes the token is somewhere
    // below it and knows nothing about it. This is the whole feature.
    void test_aChildInheritsAnAncestorDeclaration()  // NOLINT
    {
        QWidget root;
        root.setProperty("fcStyleTestPaneBackground", "#445566");
        auto* middle = new QWidget(&root);
        QWidget* panel = makePanel(middle);

        style()->polish(panel);

        QCOMPARE(backgroundOf(panel), QColor(0x44, 0x55, 0x66));
    }

    void test_aNearerDeclarationWins()  // NOLINT
    {
        QWidget root;
        root.setProperty("fcStyleTestPaneBackground", "#445566");
        auto* middle = new QWidget(&root);
        middle->setProperty("fcStyleTestPaneBackground", "#778899");
        QWidget* panel = makePanel(middle);

        style()->polish(panel);

        QCOMPARE(backgroundOf(panel), QColor(0x77, 0x88, 0x99));
    }

    void test_declarationsFromDifferentDepthsMerge()  // NOLINT
    {
        QWidget root;
        root.setProperty("fcStyleTestPaneBackground", "#445566");
        auto* middle = new QWidget(&root);
        middle->setProperty("fcStyleTestPanelBorderColor", "#010203");
        QWidget* panel = makePanel(middle);

        style()->polish(panel);

        const auto boxStyle = style()->resolveBoxStyle(Gui::FreeCADStyle::contextOf(panel));
        QCOMPARE(boxStyle.background.color(), QColor(0x44, 0x55, 0x66));
        QVERIFY(boxStyle.borderColor.has_value());
        QCOMPARE(boxStyle.borderColor->top, QColor(0x01, 0x02, 0x03));
    }

    void test_siblingSubtreesResolveIndependently()  // NOLINT
    {
        QWidget root;

        auto* firstBranch = new QWidget(&root);
        firstBranch->setProperty("fcStyleTestPaneBackground", "#445566");
        QWidget* firstPanel = makePanel(firstBranch);

        auto* secondBranch = new QWidget(&root);
        secondBranch->setProperty("fcStyleTestPaneBackground", "#778899");
        QWidget* secondPanel = makePanel(secondBranch);

        style()->polish(firstPanel);
        style()->polish(secondPanel);

        QCOMPARE(backgroundOf(firstPanel), QColor(0x44, 0x55, 0x66));
        QCOMPARE(backgroundOf(secondPanel), QColor(0x77, 0x88, 0x99));
    }

    void test_identicalDeclarationsShareOneRegistryEntry()  // NOLINT
    {
        const std::size_t before = manager()->overrideRegistry().size();

        QWidget root;

        auto* firstBranch = new QWidget(&root);
        firstBranch->setProperty("fcStyleTestPaneBackground", "#abcdef");
        QWidget* firstPanel = makePanel(firstBranch);

        auto* secondBranch = new QWidget(&root);
        secondBranch->setProperty("fcStyleTestPaneBackground", "#abcdef");
        QWidget* secondPanel = makePanel(secondBranch);

        style()->polish(firstPanel);
        style()->polish(secondPanel);

        QCOMPARE(manager()->overrideRegistry().size() - before, static_cast<std::size_t>(1));
        QCOMPARE(
            firstPanel->property(Gui::FreeCADStyle::overrideSetProperty).toUInt(),
            secondPanel->property(Gui::FreeCADStyle::overrideSetProperty).toUInt()
        );
    }

    // A dialog or popup shares a QObject parent for lifetime management only. It is its own
    // surface and must not pick up the chrome of whatever happens to own it.
    void test_aWindowDoesNotInheritFromItsParent()  // NOLINT
    {
        QWidget root;
        root.setProperty("fcStyleTestPaneBackground", "#445566");

        auto* popup = new QWidget(&root, Qt::Window);
        QWidget* panel = makePanel(popup);

        style()->polish(panel);

        QCOMPARE(backgroundOf(panel), QColor(0x11, 0x22, 0x33));
    }

    // A window's own declarations still count — only inheritance stops at the boundary.
    void test_aWindowKeepsItsOwnDeclaration()  // NOLINT
    {
        QWidget root;
        auto* popup = new QWidget(&root, Qt::Window);
        popup->setProperty("fcStyleTestPaneBackground", "#445566");
        QWidget* panel = makePanel(popup);

        style()->polish(panel);

        QCOMPARE(backgroundOf(panel), QColor(0x44, 0x55, 0x66));
    }

    void test_aNonStringDeclarationIsIgnored()  // NOLINT
    {
        QWidget root;
        QWidget* panel = makePanel(&root);
        panel->setProperty("fcStyleTestPaneBackground", 42);

        style()->polish(panel);

        QCOMPARE(backgroundOf(panel), QColor(0x11, 0x22, 0x33));
    }

    // Storing zero would leave a dynamic property on every widget in the application.
    void test_aWidgetWithNoOverridesCarriesNoProperty()  // NOLINT
    {
        QWidget root;
        QWidget* panel = makePanel(&root);

        style()->polish(panel);

        QVERIFY(!panel->property(Gui::FreeCADStyle::overrideSetProperty).isValid());
    }

    // overrideSetOf() memoizes the last widget->id pair it read. storeOverrideSet() must reset
    // that memo, or a widget resolved once before its override set was written keeps returning
    // the stale (empty) bin forever afterwards, even once the property carries a real id.
    //
    // The widget under test is a standalone top-level QWidget (no parent) rather than one
    // produced via makePanel(&root): polish() on a widget with a parent also resolves a token
    // on that parent (for inherited transparency) on the way to storeOverrideSet(), and that
    // unrelated resolve happens to evict the memo as a side effect, hiding a missing reset.
    // A window widget takes the isWindow() early-out instead, so the memo left behind by the
    // first backgroundOf() call below survives untouched into storeOverrideSet() — exactly the
    // condition the hazard describes.
    void test_resolvingBeforeAndAfterPolishOnTheSameWidgetPicksUpTheChange()  // NOLINT
    {
        QWidget panel;
        panel.setProperty("component", "TestPanel");

        QCOMPARE(backgroundOf(&panel), QColor(0x11, 0x22, 0x33));

        panel.setProperty("fcStyleTestPaneBackground", "#445566");
        style()->polish(&panel);

        QCOMPARE(backgroundOf(&panel), QColor(0x44, 0x55, 0x66));
    }

    // A property literally named "fcStyle", with nothing after the prefix, names no token.
    // declaredOverrides() must skip it rather than record an override under an empty name —
    // otherwise this widget would merge a spurious {"", "#445566"} entry that an otherwise
    // identical, undecorated widget does not, and the two would land in different registry
    // bins for no visible reason.
    void test_aBarePrefixPropertyContributesNoOverride()  // NOLINT
    {
        QWidget root;
        QWidget* plainPanel = makePanel(&root);
        QWidget* barePanel = makePanel(&root);
        barePanel->setProperty("fcStyle", "#445566");

        style()->polish(plainPanel);
        style()->polish(barePanel);

        QCOMPARE(
            barePanel->property(Gui::FreeCADStyle::overrideSetProperty).toUInt(),
            plainPanel->property(Gui::FreeCADStyle::overrideSetProperty).toUInt()
        );
        QCOMPARE(backgroundOf(barePanel), QColor(0x11, 0x22, 0x33));
    }

    // Re-polishing after an ancestor's declaration is withdrawn must drop the override, not
    // just leave the old id in place — storeOverrideSet() has to clear a real id back to none,
    // the empty-set direction test_aWidgetWithNoOverridesCarriesNoProperty never exercises.
    void test_removingAnAncestorsDeclarationClearsTheOverride()  // NOLINT
    {
        QWidget root;
        root.setProperty("fcStyleTestPaneBackground", "#445566");
        QWidget* panel = makePanel(&root);

        style()->polish(panel);
        QCOMPARE(backgroundOf(panel), QColor(0x44, 0x55, 0x66));

        root.setProperty("fcStyleTestPaneBackground", QVariant());
        style()->polish(panel);

        QCOMPARE(backgroundOf(panel), QColor(0x11, 0x22, 0x33));
        QVERIFY(!panel->property(Gui::FreeCADStyle::overrideSetProperty).isValid());
    }

    void test_settingAnOverrideAfterPolishTakesEffect()  // NOLINT
    {
        QWidget root;
        QWidget* panel = makePanel(&root);

        style()->polish(&root);
        style()->polish(panel);
        QCOMPARE(backgroundOf(panel), QColor(0x11, 0x22, 0x33));

        Gui::FreeCADStyle::setStyleOverride(
            &root,
            QStringLiteral("TestPaneBackground"),
            QStringLiteral("#445566")
        );

        QCOMPARE(backgroundOf(panel), QColor(0x44, 0x55, 0x66));
    }

    void test_changingAnOverrideAfterPolishTakesEffect()  // NOLINT
    {
        QWidget root;
        root.setProperty("fcStyleTestPaneBackground", "#445566");
        QWidget* panel = makePanel(&root);

        style()->polish(panel);
        QCOMPARE(backgroundOf(panel), QColor(0x44, 0x55, 0x66));

        Gui::FreeCADStyle::setStyleOverride(
            &root,
            QStringLiteral("TestPaneBackground"),
            QStringLiteral("#778899")
        );

        QCOMPARE(backgroundOf(panel), QColor(0x77, 0x88, 0x99));
    }

    // A colour override reaches paint time on its own: resolveBoxStyle() reads the widget's
    // current override set fresh on every call, which is what the two tests above rely on. A
    // font does not — applyWidgetFont() bakes it into the widget's own QFont once, and nothing
    // repaints that on its own. Without recomputeOverrideSets() re-applying it, this call would
    // leave the declaration recorded but the widget still showing the font from its last polish.
    void test_settingAFontOverrideAfterPolishTakesEffect()  // NOLINT
    {
        QWidget root;
        QWidget* panel = makePanel(&root);

        style()->polish(&root);
        style()->polish(panel);
        QCOMPARE(panel->font().pixelSize(), 13);

        Gui::FreeCADStyle::setStyleOverride(
            panel,
            QStringLiteral("TestPanelFontSize"),
            QStringLiteral("21px")
        );

        QCOMPARE(panel->font().pixelSize(), 21);
    }

    void test_reparentingIntoASubtreeTakesEffectAfterARefresh()  // NOLINT
    {
        QWidget root;
        auto* overriding = new QWidget(&root);
        overriding->setProperty("fcStyleTestPaneBackground", "#445566");
        auto* plain = new QWidget(&root);

        QWidget* panel = makePanel(plain);
        style()->polish(panel);
        QCOMPARE(backgroundOf(panel), QColor(0x11, 0x22, 0x33));

        panel->setParent(overriding);
        Gui::FreeCADStyle::refreshStyleOverrides(panel);

        QCOMPARE(backgroundOf(panel), QColor(0x44, 0x55, 0x66));
    }

    void test_reparentingOutOfASubtreeTakesEffectAfterARefresh()  // NOLINT
    {
        QWidget root;
        auto* overriding = new QWidget(&root);
        overriding->setProperty("fcStyleTestPaneBackground", "#445566");
        auto* plain = new QWidget(&root);

        QWidget* panel = makePanel(overriding);
        style()->polish(panel);
        QCOMPARE(backgroundOf(panel), QColor(0x44, 0x55, 0x66));

        panel->setParent(plain);
        Gui::FreeCADStyle::refreshStyleOverrides(panel);

        QCOMPARE(backgroundOf(panel), QColor(0x11, 0x22, 0x33));
    }

    // A late change on a container has to reach everything under it, not just the container.
    void test_aRefreshReachesTheWholeSubtree()  // NOLINT
    {
        QWidget root;
        auto* middle = new QWidget(&root);
        QWidget* panel = makePanel(middle);

        style()->polish(&root);
        style()->polish(middle);
        style()->polish(panel);

        Gui::FreeCADStyle::setStyleOverride(
            &root,
            QStringLiteral("TestPaneBackground"),
            QStringLiteral("#445566")
        );

        QCOMPARE(backgroundOf(panel), QColor(0x44, 0x55, 0x66));
    }

    void test_unpolishClearsTheStoredSet()  // NOLINT
    {
        QWidget root;
        QWidget* panel = makePanel(&root);
        panel->setProperty("fcStyleTestPaneBackground", "#445566");

        style()->polish(panel);
        QVERIFY(panel->property(Gui::FreeCADStyle::overrideSetProperty).isValid());

        style()->unpolish(panel);
        QVERIFY(!panel->property(Gui::FreeCADStyle::overrideSetProperty).isValid());
    }

    // recomputeOverrideSets() recurses into every child, windows included. Its correctness there
    // rests entirely on computeOverrideSet() re-walking upward from the window and breaking
    // immediately, the same way it does for a direct polish() — a refresh must not let a window
    // start inheriting its parent's overrides.
    void test_aRefreshDoesNotLeakIntoAChildWindow()  // NOLINT
    {
        QWidget root;
        root.setProperty("fcStyleTestPaneBackground", "#445566");

        auto* popup = new QWidget(&root, Qt::Window);
        QWidget* panel = makePanel(popup);

        style()->polish(panel);
        QCOMPARE(backgroundOf(panel), QColor(0x11, 0x22, 0x33));

        Gui::FreeCADStyle::refreshStyleOverrides(&root);

        QCOMPARE(backgroundOf(panel), QColor(0x11, 0x22, 0x33));
    }

    // storeOverrideSet() must run before tagWidgetTransparency() in polish(): that call can
    // synchronously dispatch QEvent::StyleChange to this same widget, and a handler reacting to
    // it — QTabBar::changeEvent, for one — resolves tokens for this widget before polish()
    // returns. If the override id were stored after the transparency tagging, such a handler
    // would observe the id this widget had before this polish call, not the one it is being
    // polished into.
    void test_theOverrideIdIsInPlaceWhenStyleChangeFires()  // NOLINT
    {
        QWidget root;
        root.setProperty("fcStyleTestPaneBackground", "#445566");

        OverrideObservingWidget widget(&root);
        widget.setProperty("component", "TestPanel");

        style()->polish(&widget);
        QCOMPARE(backgroundOf(&widget), QColor(0x44, 0x55, 0x66));

        // Changes what the widget's override set resolves to, and forces
        // tagWidgetTransparency() to actually flip and dispatch: it early-returns when the
        // surface does not change, so without this the event this test depends on never fires.
        root.setProperty("fcStyleTestPaneBackground", "#778899");
        widget.setProperty("transparent", true);

        style()->polish(&widget);

        QVERIFY(widget.observedAStyleChange);
        QCOMPARE(widget.backgroundDuringLastStyleChange, QColor(0x77, 0x88, 0x99));
    }

    // The shape the property editor relies on: a declaration on the container reaches the tab
    // bar nested two levels below it, through a token the tab bar's own token references.
    void test_aTabBarPicksUpTheContainerDeclaration()  // NOLINT
    {
        QWidget container;
        auto* tabs = new QTabWidget(&container);
        tabs->addTab(new QWidget, QStringLiteral("Data"));

        QTabBar* tabBar = tabs->tabBar();
        style()->polish(tabBar);
        const QColor before
            = style()->resolveBoxStyle(Gui::FreeCADStyle::contextOf(tabBar)).background.color();

        Gui::FreeCADStyle::setStyleOverride(
            &container,
            QStringLiteral("TestPaneBackground"),
            QStringLiteral("#445566")
        );

        const QColor after
            = style()->resolveBoxStyle(Gui::FreeCADStyle::contextOf(tabBar)).background.color();

        QVERIFY(before != after);
        QCOMPARE(after, QColor(0x44, 0x55, 0x66));
    }

    // QTabBar marks its active tab with State_Selected, and contextOf() has to translate that
    // into StyleState::Selected. Nothing that builds a StyleContext by hand can see whether it
    // does: such a test pins the theme's spelling, not the style's translation of Qt's flag.
    // Reverting the branch to Checked leaves every by-name suite green while the active tab
    // resolves a prefix no theme defines, losing both its fill and its border and rendering as
    // an ordinary inactive tab. Only a rendered tab bar closes that.
    void test_theActiveTabPaintsTheSelectedBackground()  // NOLINT
    {
        QTabWidget tabs;
        tabs.addTab(new QWidget, QStringLiteral("First"));
        tabs.addTab(new QWidget, QStringLiteral("Second"));
        tabs.addTab(new QWidget, QStringLiteral("Third"));
        tabs.setCurrentIndex(2);

        QTabBar* tabBar = tabs.tabBar();
        style()->polish(tabBar);
        tabs.show();
        QVERIFY(QTest::qWaitForWindowExposed(&tabs));

        const QRect activeTab = tabBar->tabRect(2);
        const QRect restingTab = tabBar->tabRect(0);

        // contextOf() takes tab hover from QCursor::pos() rather than the option, because Qt
        // tracks it through WA_Hover and an internal hover index it does not always propagate.
        // Hover outranks selection, so an active tab sitting under wherever the cursor maps
        // would resolve the hovered prefix and this would be measuring the wrong state.
        QVERIFY2(
            !activeTab.contains(tabBar->mapFromGlobal(QCursor::pos())),
            "the cursor maps inside the active tab, so it resolves Hovered rather than Selected"
        );

        QImage canvas(tabBar->size(), QImage::Format_ARGB32);
        canvas.fill(Qt::magenta);
        tabBar->render(&canvas);

        const QColor selectedFill(0x00, 0xff, 0xff);
        const QColor restingFill(0x10, 0x10, 0x10);

        QVERIFY2(
            pixelsOfColour(canvas, activeTab, selectedFill) > 0,
            "the active tab was not filled from TabBarTabSelectedBackground"
        );

        // The claim is about the active tab alone, so an inactive one has to keep the plain
        // fill — otherwise the assertion above could be satisfied by every tab painting alike.
        QCOMPARE(pixelsOfColour(canvas, activeTab, restingFill), 0);
        QVERIFY2(
            pixelsOfColour(canvas, restingTab, restingFill) > 0,
            "the inactive tab was not filled from TabBarTabBackground"
        );
        QCOMPARE(pixelsOfColour(canvas, restingTab, selectedFill), 0);
    }

    // An empty expression means "no override", not "an override that evaluates to nothing".
    // A stored empty expression is accepted by declaredOverrides and then warns on every
    // resolve, which is both noisy and never what a caller clearing a preference meant.
    void test_anEmptyExpressionClearsTheOverride()  // NOLINT
    {
        QPushButton button;

        Gui::FreeCADStyle::setStyleOverride(
            &button,
            QStringLiteral("ButtonFontSize"),
            QStringLiteral("21px")
        );
        QVERIFY(button.property("fcStyleButtonFontSize").isValid());

        Gui::FreeCADStyle::setStyleOverride(&button, QStringLiteral("ButtonFontSize"), QString());

        QVERIFY(!button.property("fcStyleButtonFontSize").isValid());
    }
};

QTEST_MAIN(TestStyleOverrides)

#include "StyleOverrides.moc"
