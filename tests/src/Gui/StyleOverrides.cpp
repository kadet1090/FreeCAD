// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QApplication>
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
};

QTEST_MAIN(TestStyleOverrides)

#include "StyleOverrides.moc"
