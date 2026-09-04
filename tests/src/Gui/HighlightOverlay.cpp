// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QApplication>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTest>
#include <QWidget>

#include "src/App/InitApplication.h"

#include <Gui/Application.h>
#include <Gui/FreeCADStyle.h>
#include <Gui/HighlightOverlay.h>
#include <Gui/StyleParameters/ParameterManager.h>
#include <Gui/StyleParameters/StyleContext.h>

namespace
{
constexpr QMargins testMargins {4, 4, 4, 4};
constexpr QColor borderColor(0x44, 0x55, 0x66);

/// A scroll area inset inside a window, with content larger than the viewport.
///
/// The inset is what the preferences dialog's own 16px layout margins provide, and it is what
/// gives the halo somewhere to overhang into. The content being larger than the viewport puts
/// it at the viewport origin while leaving the vertical scrollbar room to move.
struct ScrollFixture
{
    QWidget window;
    QScrollArea* area = new QScrollArea(&window);
    QWidget* content = new QWidget;

    ScrollFixture()
    {
        window.resize(400, 400);
        area->setFrameShape(QFrame::NoFrame);
        area->setGeometry(50, 50, 200, 200);
        content->resize(400, 800);
        area->setWidget(content);
    }

    void show()
    {
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
    }
};
}  // namespace

class TestHighlightOverlay: public QObject
{
    Q_OBJECT

public:
    TestHighlightOverlay()
    {
        tests::initApplication();

        if (Gui::Application::Instance == nullptr) {
            new Gui::Application(true);
        }

        // Saturated and unmistakable, so a resolved value can only have come from these.
        Gui::Application::Instance->styleParameterManager()->addSource(
            new Gui::StyleParameters::InMemoryParameterSource(
                {
                    {.name = "HighlightOverlayBackground", .value = "#112233"},
                    {.name = "HighlightOverlayBorderColor", .value = "#445566"},
                    {.name = "HighlightOverlayBorderThickness", .value = "2px"},
                    {.name = "HighlightOverlayBorderRadius", .value = "3px"},
                    {.name = "HighlightOverlayMargin", .value = "6px"},
                },
                {.name = "Highlight Overlay Fixture"}
            )
        );

        // The production paths read the widget's own style, so the application style has to be
        // the one under test.
        QApplication::setStyle(new Gui::FreeCADStyle);
    }

private:
    static Gui::FreeCADStyle* style()
    {
        return qobject_cast<Gui::FreeCADStyle*>(QApplication::style());
    }

private Q_SLOTS:
    void theHaloSurroundsTheTarget()
    {
        ScrollFixture fixture;

        auto* nesting = new QWidget(fixture.content);
        nesting->setGeometry(10, 20, 100, 100);

        auto* target = new QWidget(nesting);
        target->setGeometry(5, 7, 40, 12);

        const QRect halo
            = Gui::HighlightOverlay::highlightRect(target, fixture.area->viewport(), testMargins);

        // 10 + 5 across and 20 + 7 down from the viewport origin, then grown by 4 a side.
        QCOMPARE(halo, QRect(11, 23, 48, 20));
    }

    void aTargetOnAnInactivePageGetsNoHalo()
    {
        ScrollFixture fixture;

        auto* stack = new QStackedWidget(fixture.content);
        stack->setGeometry(0, 0, 400, 400);

        auto* firstPage = new QWidget;
        auto* secondPage = new QWidget;
        stack->addWidget(firstPage);
        stack->addWidget(secondPage);
        stack->setCurrentWidget(firstPage);

        auto* onFirstPage = new QWidget(firstPage);
        onFirstPage->setGeometry(5, 5, 30, 10);

        auto* onSecondPage = new QWidget(secondPage);
        onSecondPage->setGeometry(5, 5, 30, 10);

        const QRect visible
            = Gui::HighlightOverlay::highlightRect(onFirstPage, fixture.area->viewport(), testMargins);
        const QRect hidden = Gui::HighlightOverlay::highlightRect(
            onSecondPage,
            fixture.area->viewport(),
            testMargins
        );

        // Asserted together: without the first line the second would also pass on a fixture
        // that simply never produces a halo.
        QVERIFY(!visible.isNull());
        QVERIFY(hidden.isNull());
    }

    void aWidgetOutsideTheSurfaceGetsNoHalo()
    {
        ScrollFixture fixture;

        QWidget elsewhere;
        auto* stranger = new QWidget(&elsewhere);
        stranger->setGeometry(5, 5, 30, 10);

        const QRect outsider
            = Gui::HighlightOverlay::highlightRect(stranger, fixture.area->viewport(), testMargins);
        const QRect absent
            = Gui::HighlightOverlay::highlightRect(nullptr, fixture.area->viewport(), testMargins);

        QVERIFY(outsider.isNull());
        QVERIFY(absent.isNull());
    }

    void scrollingMovesTheHalo()
    {
        ScrollFixture fixture;

        auto* target = new QWidget(fixture.content);
        target->setGeometry(10, 300, 40, 12);

        fixture.show();

        const QRect before
            = Gui::HighlightOverlay::highlightRect(target, fixture.area->viewport(), testMargins);

        fixture.area->verticalScrollBar()->setValue(50);

        const QRect after
            = Gui::HighlightOverlay::highlightRect(target, fixture.area->viewport(), testMargins);

        QCOMPARE(after, before.translated(0, -50));
    }

    void settingATargetShowsTheOverlay()
    {
        ScrollFixture fixture;

        auto* target = new QWidget(fixture.content);
        target->setGeometry(10, 20, 40, 12);

        auto* overlay = new Gui::HighlightOverlay(fixture.area);

        fixture.show();

        overlay->setTarget(target);
        QCOMPARE(overlay->target(), target);
        QVERIFY(overlay->isVisible());

        overlay->setTarget(nullptr);
        QVERIFY(overlay->target() == nullptr);
        QVERIFY(!overlay->isVisible());
    }

    void aDestroyedTargetClearsItself()
    {
        ScrollFixture fixture;

        auto* target = new QWidget(fixture.content);
        target->setGeometry(10, 20, 40, 12);

        auto* overlay = new Gui::HighlightOverlay(fixture.area);

        fixture.show();

        overlay->setTarget(target);
        delete target;

        QVERIFY(overlay->target() == nullptr);
        QVERIFY(!overlay->isVisible());
    }

    void theOverlayResolvesTheHighlightOverlayTokens()
    {
        ScrollFixture fixture;

        auto* overlay = new Gui::HighlightOverlay(fixture.area);

        const Gui::StyleParameters::StyleContext context = Gui::FreeCADStyle::contextOf(overlay);

        QCOMPARE(style()->resolveBoxStyle(context).background.color(), QColor(0x11, 0x22, 0x33));
    }

    void theHaloMarginComesFromTheToken()
    {
        ScrollFixture fixture;

        auto* overlay = new Gui::HighlightOverlay(fixture.area);

        const Gui::StyleParameters::StyleContext context = Gui::FreeCADStyle::contextOf(overlay);

        QCOMPARE(style()->resolveBoxGeometry(context).margin, QMarginsF(6, 6, 6, 6));
    }

    // highlightRect() and the token resolution are each tested on their own above, but nothing
    // exercises the line in paintEvent() that combines them. That line once fed paintBox() a rect
    // already grown by the margin, and paintBox() insets by that same margin again before it
    // draws — the two cancel out, so the border painted right on the target's own edge instead of
    // standing off it. This renders the real overlay and pins the border ring's position.
    void theHaloBorderStandsOneMarginOutsideTheTarget()
    {
        ScrollFixture fixture;

        auto* target = new QWidget(fixture.content);
        target->setGeometry(20, 30, 80, 24);

        auto* overlay = new Gui::HighlightOverlay(fixture.area);

        fixture.show();
        overlay->setTarget(target);

        const QImage canvas = fixture.window.grab().toImage();
        const QPoint topEdgeCenter = target->mapTo(&fixture.window, QPoint(target->width() / 2, 0));

        // One row into the 2px ring, which stands 6px (the token's margin) above the target.
        QCOMPARE(canvas.pixelColor(topEdgeCenter.x(), topEdgeCenter.y() - 5), borderColor);

        // Twice the margin above the edge is clear past the halo entirely, so no border ink
        // belongs here — guards against a fix that grows the rect by more than it should.
        QVERIFY(canvas.pixelColor(topEdgeCenter.x(), topEdgeCenter.y() - 12) != borderColor);
    }

    // A control flush against the surface's edge used to lose that side of its ring entirely:
    // the overlay was a child of the viewport, so anything outside the viewport's rect was
    // clipped away. It now hangs off the window and clips itself one margin wider.
    void theHaloReachesPastTheSurfaceEdge()
    {
        ScrollFixture fixture;

        auto* target = new QWidget(fixture.content);
        target->setGeometry(0, 100, 60, 20);

        auto* overlay = new Gui::HighlightOverlay(fixture.area);

        fixture.show();
        overlay->setTarget(target);

        const QImage canvas = fixture.window.grab().toImage();
        const QPoint leftEdgeMiddle = target->mapTo(&fixture.window, QPoint(0, target->height() / 2));

        // One row into the ring, 6px left of the viewport's own left edge — outside the surface,
        // inside the window.
        QCOMPARE(canvas.pixelColor(leftEdgeMiddle.x() - 5, leftEdgeMiddle.y()), borderColor);

        // Twice the margin out is past the clip, so the overhang is bounded rather than free to
        // smear across the rest of the window.
        QVERIFY(canvas.pixelColor(leftEdgeMiddle.x() - 12, leftEdgeMiddle.y()) != borderColor);
    }

    // The overhang is what makes the clip load-bearing: hanging off the window, an unclipped
    // overlay would happily paint a target that has been scrolled out of sight over whatever
    // sits above the scroll area.
    void aScrolledOutTargetPaintsNothingOutsideTheSurface()
    {
        ScrollFixture fixture;

        auto* target = new QWidget(fixture.content);
        target->setGeometry(20, 0, 60, 20);

        auto* overlay = new Gui::HighlightOverlay(fixture.area);

        fixture.show();
        overlay->setTarget(target);

        // Enough to carry the target above the viewport, but not so far that the window would
        // have clipped the halo away regardless.
        fixture.area->verticalScrollBar()->setValue(40);

        const QImage canvas = fixture.window.grab().toImage();
        const QPoint middle
            = target->mapTo(&fixture.window, QPoint(target->width() / 2, target->height() / 2));

        QVERIFY(middle.y() < fixture.area->viewport()->mapTo(&fixture.window, QPoint(0, 0)).y());

        // Neither the ring nor the tint it encloses belongs above the surface.
        const QColor ink = canvas.pixelColor(middle);
        QVERIFY(ink != borderColor);
        QVERIFY(ink != QColor(0x11, 0x22, 0x33));
    }

    // Nothing about the halo needs a scroll area; a scroll area only adds a viewport to measure
    // against and scrollbars to follow.
    void aPlainWidgetHostMarksItsOwnChildren()
    {
        QWidget window;
        window.resize(300, 300);

        auto* host = new QWidget(&window);
        host->setGeometry(40, 40, 200, 200);

        auto* target = new QWidget(host);
        target->setGeometry(30, 50, 60, 20);

        auto* overlay = new Gui::HighlightOverlay(host);

        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        overlay->setTarget(target);
        QVERIFY(overlay->isVisible());

        const QImage canvas = window.grab().toImage();
        const QPoint topEdgeCenter = target->mapTo(&window, QPoint(target->width() / 2, 0));

        QCOMPARE(canvas.pixelColor(topEdgeCenter.x(), topEdgeCenter.y() - 5), borderColor);
    }
};

QTEST_MAIN(TestHighlightOverlay)

#include "HighlightOverlay.moc"
