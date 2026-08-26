// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QMdiArea>
#include <QMdiSubWindow>
#include <QPalette>
#include <QScrollArea>
#include <QTest>
#include <QWidget>

#include "src/App/InitApplication.h"

#include <Gui/Application.h>
#include <Gui/FreeCADStyle.h>
#include <Gui/StyleParameters/ParameterManager.h>
#include <Gui/TaskView/TaskView.h>

class TestMdiSurface: public QObject
{
    Q_OBJECT

public:
    TestMdiSurface()
    {
        tests::initApplication();

        if (Gui::Application::Instance == nullptr) {
            new Gui::Application(true);
        }

        // A rounded top edge is what gives a scroll area a mask at all, so the corners have to
        // be stated for the mask to be observable.
        Gui::Application::Instance->styleParameterManager()->addSource(
            new Gui::StyleParameters::InMemoryParameterSource(
                {
                    {.name = "ListBorderRadius", .value = "6px"},
                },
                {.name = "Mdi Surface Fixture"}
            )
        );
    }

private Q_SLOTS:

    // The subwindows stacked under the active one are clipped away by the backing store only
    // when the surface above them is opaque. Views paint their own chrome and nothing else, so
    // the fill has to come from the subwindow itself — otherwise anything that dirties a covered
    // subwindow (a theme reload repaints every widget) leaves its paint showing through the gaps.
    void test_anMdiSubWindowIsGivenAnOpaqueFill()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QMdiArea area;
        QMdiSubWindow* subWindow = area.addSubWindow(new QWidget);

        // Cleared first: whatever Qt does with the flag on its own, the fill this relies on is
        // the style's to guarantee.
        subWindow->setAutoFillBackground(false);

        style.polish(subWindow);

        QVERIFY(subWindow->autoFillBackground());
    }

    // autoFillBackground only registers the widget as opaque if the brush it fills with is
    // itself opaque, so a themed Window colour that ever gained an alpha channel would silently
    // reopen the gap.
    void test_theFillBrushIsOpaque()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QMdiArea area;
        QMdiSubWindow* subWindow = area.addSubWindow(new QWidget);

        style.polish(subWindow);

        const QBrush brush = subWindow->palette().brush(subWindow->backgroundRole());
        QVERIFY(brush.isOpaque());
    }

    // Only the subwindow surface is filled. Forcing a fill on ordinary widgets would paint over
    // the token-driven backgrounds the style draws for them.
    void test_anOrdinaryWidgetIsLeftUnfilled()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QWidget widget;

        style.polish(&widget);

        QVERIFY(!widget.autoFillBackground());
    }

    // A QMdiArea is a scroll area, and the direct children of its viewport are the subwindows.
    // A theme reload re-polishes every widget with the workspace already populated, so the
    // viewport-stripping branch must leave them alone or the whole workspace turns see-through.
    void test_polishingAPopulatedMdiAreaLeavesItsSubWindowsFilled()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QMdiArea area;
        QMdiSubWindow* subWindow = area.addSubWindow(new QWidget);
        style.polish(subWindow);

        style.polish(&area);

        QVERIFY(subWindow->autoFillBackground());
        QVERIFY(!subWindow->testAttribute(Qt::WA_NoSystemBackground));
    }

    // The workspace surface behind the subwindows has to keep painting as well: the style draws
    // no panel for a QMdiArea, so nothing would take the stripped fill's place.
    void test_polishingAnMdiAreaLeavesItsViewportFilled()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QMdiArea area;

        style.polish(&area);

        QVERIFY(!area.viewport()->testAttribute(Qt::WA_NoSystemBackground));
    }

    // Ordinary scroll areas keep the strip — it is what lets the style paint their panel behind
    // the content instead of Qt's palette fill.
    void test_anOrdinaryScrollAreaViewportIsStillStripped()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QScrollArea area;

        style.polish(&area);

        QVERIFY(area.viewport()->testAttribute(Qt::WA_NoSystemBackground));
    }

    // QSint::ActionGroup fills a task box with the palette's window brush from its own
    // constructor, and that fill would sit on top of the panel the style paints for it.
    void test_aTaskBoxGivesUpItsGroupsOpaqueFill()  // NOLINT
    {
        Gui::FreeCADStyle style;
        Gui::TaskView::TaskBox box;

        QVERIFY(box.autoFillBackground());

        style.polish(&box);

        QVERIFY(!box.autoFillBackground());
    }

    // The mask is the style's, and a widget handed back to another style has to be handed back
    // square — nothing else would ever clear the corners this one clipped away.
    void test_unpolishingAScrollAreaGivesItsCornersBack()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QScrollArea area;
        area.setProperty("component", QStringLiteral("List"));
        area.resize(50, 50);

        style.polish(&area);

        QVERIFY(!area.mask().isEmpty());

        style.unpolish(&area);

        QVERIFY(area.mask().isEmpty());
    }
};

QTEST_MAIN(TestMdiSurface)
#include "MdiSurface.moc"
