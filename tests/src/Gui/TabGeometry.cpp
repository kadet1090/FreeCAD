// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QFontMetrics>
#include <QStyleOptionTab>
#include <QTabBar>
#include <QTest>

#include "src/App/InitApplication.h"

#include <Gui/Application.h>
#include <Gui/FreeCADStyle.h>
#include <Gui/StyleParameters/ParameterManager.h>

class TestTabGeometry: public QObject
{
    Q_OBJECT

public:
    TestTabGeometry()
    {
        tests::initApplication();

        if (Gui::Application::Instance == nullptr) {
            new Gui::Application(true);
        }

        Gui::Application::Instance->styleParameterManager()->addSource(
            new Gui::StyleParameters::InMemoryParameterSource(
                {
                    // Asymmetric on purpose, so a horizontal answer cannot pass for a vertical
                    // one, and large enough to dominate any minimum Qt imposes on a tab.
                    {.name = "TabBarTabPadding", .value = "padding(horizontal: 20px, vertical: 5px)"},
                    // A gap, which is the negative of the overlap Qt asks for.
                    {.name = "TabBarTabSpacing", .value = "-3px"},
                    {.name = "TabBarBaseHeight", .value = "4px"},
                    // What a tab bar that names a namespace of its own states instead. Both
                    // values differ from the shared ones, so neither can pass for the other.
                    {.name = "MdiTabBarTabSpacing", .value = "-9px"},
                    {.name = "MdiTabBarBaseHeight", .value = "11px"},
                },
                {.name = "Tab Geometry Fixture"}
            )
        );
    }

private Q_SLOTS:

    // Qt sizes a tab by adding these two to the label it measured, so they are how a tab's
    // padding reaches its size at all.
    void test_tabPaddingIsReportedAsTheTabSpaceMetrics()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QTabBar tabBar;
        QStyleOptionTab option;

        QCOMPARE(style.pixelMetric(QStyle::PM_TabBarTabHSpace, &option, &tabBar), 40);
        QCOMPARE(style.pixelMetric(QStyle::PM_TabBarTabVSpace, &option, &tabBar), 10);
    }

    // Spacing states a gap between neighbours; Qt asks for the overlap, which is its negative.
    void test_tabSpacingIsReportedAsANegativeOverlap()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QTabBar tabBar;
        QStyleOptionTab option;

        QCOMPARE(style.pixelMetric(QStyle::PM_TabBarTabOverlap, &option, &tabBar), 3);
    }

    // The metrics are only worth stating if the bar lays out to them. Measured against a bar
    // whose padding is overridden away, the difference is the padding itself and nothing Qt
    // adds around it.
    void test_aTabIsLaidOutWithRoomForItsPadding()  // NOLINT
    {
        Gui::FreeCADStyle style;

        // An expanding bar stretches its tabs to its own width, which would hide the very
        // difference this measures.
        QTabBar padded;
        padded.setExpanding(false);
        padded.setStyle(&style);
        style.polish(&padded);
        padded.addTab(QStringLiteral("Data"));

        QTabBar bare;
        bare.setExpanding(false);
        Gui::FreeCADStyle::setStyleOverride(
            &bare,
            QStringLiteral("TabBarTabPadding"),
            QStringLiteral("padding(0px)")
        );
        bare.setStyle(&style);
        // The override reaches the widget through the set polish() computes for it.
        style.polish(&bare);
        bare.addTab(QStringLiteral("Data"));

        QCOMPARE(padded.tabRect(0).width() - bare.tabRect(0).width(), 40);
        QCOMPARE(padded.tabRect(0).height() - bare.tabRect(0).height(), 10);
    }

    // A tab bar that differs from an ordinary one says so by naming a namespace rather than by
    // becoming a component of its own, the way the document tabs and the workbench selector do.
    void test_theNamespaceATabBarNamesOutranksTheSharedTabTokens()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QStyleOptionTab option;

        QTabBar shared;

        QTabBar named;
        named.setProperty("component", QStringLiteral("MdiTabBar"));

        QCOMPARE(style.pixelMetric(QStyle::PM_TabBarTabOverlap, &option, &shared), 3);
        QCOMPARE(style.pixelMetric(QStyle::PM_TabBarTabOverlap, &option, &named), 9);
    }

    // The base is asked for separately from the tabs, and a bar states its differences across
    // both, so the name has to reach the base on its own.
    void test_theNamespaceATabBarNamesReachesItsBase()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QStyleOptionTab option;

        QTabBar shared;

        QTabBar named;
        named.setProperty("component", QStringLiteral("MdiTabBar"));

        QCOMPARE(style.pixelMetric(QStyle::PM_TabBarBaseHeight, &option, &shared), 4);
        QCOMPARE(style.pixelMetric(QStyle::PM_TabBarBaseHeight, &option, &named), 11);
    }
};

QTEST_MAIN(TestTabGeometry)  // NOLINT
#include "TabGeometry.moc"
