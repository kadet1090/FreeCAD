// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QApplication>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QImage>
#include <QMainWindow>
#include <QPainter>
#include <QSplitter>
#include <QSplitterHandle>
#include <QStyleOption>
#include <QTabWidget>
#include <QTest>
#include <QWidget>

#include "src/App/InitApplication.h"

#include <Gui/Application.h>
#include <Gui/FreeCADStyle.h>
#include <Gui/OverlayWidgets.h>
#include <Gui/StyleParameters/ParameterManager.h>

namespace
{

constexpr QRgb BorderColor = qRgb(255, 0, 0);
constexpr QRgb BodyColor = qRgb(0, 255, 0);

/// What an overlaid strip's surface is, as against the opaque one a docked strip gets.
constexpr QRgb OverlayColor = qRgb(0, 0, 255);

/// The handle's own surface, distinct from both the panel's and the seam it draws along one edge.
constexpr QRgb HandleColor = qRgb(0, 0, 255);

/// Inset stated at the canonical North, i.e. on the bottom edge, for every rotating token.
constexpr int NorthInset = 3;
/// What the title's own rule is worth, as distinct from the panel outline it also carries.
constexpr int TitleRule = 2;
constexpr int SeparatorWidth = 7;

/// A dock holding @p body, docked into @p window at @p area so dockWidgetArea() can answer.
QDockWidget* dockInto(QMainWindow& window, QWidget* body, Qt::DockWidgetArea area)
{
    auto* dock = new QDockWidget(&window);
    dock->setWidget(body);
    window.addDockWidget(area, dock);

    return dock;
}

/// A title bar the way OverlayTitleBar presents itself: a child that declares what it is.
QWidget* declaredTitleBar(QWidget* host)
{
    auto* title = new QWidget(host);
    title->setProperty("component", QStringLiteral("Panel"));
    title->setProperty("element", QStringLiteral("Title"));

    return title;
}

}  // namespace

class TestPanelSurface: public QObject
{
    Q_OBJECT

public:
    TestPanelSurface()
    {
        tests::initApplication();

        if (Gui::Application::Instance == nullptr) {
            new Gui::Application(true);
        }

        // Stated here rather than read from the shipped theme: these tests are about which edge
        // a value lands on, and pinning them to the theme's own numbers would make every future
        // retune of the panel look like a regression.
        Gui::Application::Instance->styleParameterManager()->addSource(
            new Gui::StyleParameters::InMemoryParameterSource(
                {
                    {.name = "PanelBackground", .value = "#00ff00"},
                    {.name = "PanelBorderColor", .value = "#ff0000"},
                    {.name = "PanelBorderThickness", .value = "border_thickness(0px, bottom: 3px)"},
                    {.name = "PanelPadding", .value = "padding(0px, bottom: 3px)"},
                    {.name = "PanelTitlePadding", .value = "0px"},
                    {.name = "PanelSeparatorWidth", .value = "7px"},
                    {.name = "PanelSeparatorBackground", .value = "#0000ff"},
                    {.name = "PanelSeparatorBorderColor", .value = "#ff0000"},
                    {.name = "PanelSeparatorBorderThickness",
                     .value = "border_thickness(0px, bottom: 3px)"},
                    // An element states its own box in full: buildPrefixes only ever tries
                    // "PanelTitle", so nothing here falls back to the Panel root's colours.
                    {.name = "PanelTitleBackground", .value = "#00ff00"},
                    {.name = "PanelTitleBorderColor", .value = "#ff0000"},
                    {.name = "PanelTitleBorderThickness",
                     .value = "border_thickness(0px, bottom: 2px)"},
                    {.name = "PanelTitleTextColor", .value = "#ff00ff"},
                    {.name = "PanelTitleTransparentBackground", .value = "#0000ff"},
                    {.name = "PanelTitleTransparentFontWeight", .value = "800"},
                    {.name = "PanelTransparentPadding", .value = "reset()"},
                    {.name = "PanelTransparentBorderThickness", .value = "reset()"},
                },
                {.name = "Panel Surface Fixture"}
            )
        );
    }

private Q_SLOTS:

    // A docked QDockWidget leaves nothing of its own body visible, so the surface can only come
    // from the widget it hands that body to — and only if the style asks Qt to paint it.
    void test_aPanelBodyIsGivenAStyledBackground()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QMainWindow window;
        auto* body = new QWidget;
        dockInto(window, body, Qt::LeftDockWidgetArea);

        style.polish(body);

        QVERIFY(body->testAttribute(Qt::WA_StyledBackground));
    }

    // The attribute makes Qt route the widget's whole background through the style, so claiming
    // widgets the panel tokens do not describe would repaint them from an empty box.
    void test_anOrdinaryWidgetKeepsItsOwnBackground()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QWidget widget;

        style.polish(&widget);

        QVERIFY(!widget.testAttribute(Qt::WA_StyledBackground));
    }

    // The seam is stated once, at the canonical North, and has to come out on whichever edge
    // faces the central widget. The padding is what reserves it, so it rotates with it.
    void test_theBodyInsetFollowsTheDockArea_data()  // NOLINT
    {
        QTest::addColumn<Qt::DockWidgetArea>("area");
        QTest::addColumn<QMargins>("expected");

        QTest::newRow("top") << Qt::TopDockWidgetArea << QMargins(0, 0, 0, NorthInset);
        QTest::newRow("bottom") << Qt::BottomDockWidgetArea << QMargins(0, NorthInset, 0, 0);
        QTest::newRow("left") << Qt::LeftDockWidgetArea << QMargins(0, 0, NorthInset, 0);
        QTest::newRow("right") << Qt::RightDockWidgetArea << QMargins(NorthInset, 0, 0, 0);
    }

    void test_theBodyInsetFollowsTheDockArea()  // NOLINT
    {
        QFETCH(Qt::DockWidgetArea, area);
        QFETCH(QMargins, expected);

        Gui::FreeCADStyle style;
        QMainWindow window;
        auto* body = new QWidget;
        dockInto(window, body, area);

        style.polish(body);

        QCOMPARE(body->contentsMargins(), expected);
    }

    // Overlay mode hangs the strip off a tab widget instead of off a dock, so there is no dock
    // to find it through - which is why a title bar declares itself rather than being deduced.
    //
    // It also takes no styled background of its own: overlay mode suppresses the background
    // pass that attribute drives, so the strip issues the primitive from its own paintEvent
    // instead. The body still needs the attribute - it has no paintEvent of its own to fall
    // back on - which is why this only holds for the strip.
    void test_aTitleBarWithNoDockAboveItIsStillAPanelTitle()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QTabWidget host;
        QWidget* title = declaredTitleBar(&host);

        style.polish(title);

        QVERIFY(!title->testAttribute(Qt::WA_StyledBackground));
        QCOMPARE(title->contentsMargins(), QMargins(0, 0, 0, TitleRule));
    }

    // The tag flips when overlay mode is toggled, long after polish, and the Transparent variant
    // restates padding as well as colour. Both are kept on the widget, so a repaint cannot fix
    // them - only the StyleChange the tag carries can.
    void test_theTransparentVariantIsPickedUpWhenTheTagFlips()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QMainWindow window;
        auto* body = new QWidget;
        dockInto(window, body, Qt::LeftDockWidgetArea);

        style.polish(body);
        QCOMPARE(body->contentsMargins(), QMargins(0, 0, NorthInset, 0));

        // What OverlayTabWidget does when a panel goes into overlay mode.
        body->setProperty("transparent", true);
        style.updateTransparency(body, false);

        QCOMPARE(body->contentsMargins(), QMargins());
    }

    // A font is baked into the widget's own QFont at polish time, unlike a colour, which is read
    // again every time the widget paints. Overlay mode flips the transparency tag long after
    // polish, so a strip whose weight only the Transparent variant states would keep the docked
    // font for as long as it stayed overlaid.
    void test_aStripTakesTheTransparentFontWhenTheTagFlips()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QTabWidget host;
        QWidget* strip = declaredTitleBar(&host);

        style.polish(strip);
        const int dockedWeight = strip->font().weight();

        strip->setProperty("transparent", true);
        style.updateTransparency(strip, false);

        QCOMPARE(strip->font().weight(), 800);
        QVERIFY(dockedWeight != 800);
    }

    // QSplitter renames every handle to qt_splithandle_* once createHandle() has returned, and
    // QStyleSheetStyle reads a qt_ prefix as one of Qt's own internal children: it answers any
    // font set on such a widget by replacing it with the parent's, and does so again at the end
    // of every later write. The handle's own name is what keeps the token font on it.
    void test_aSplitterHandleStripKeepsItsFontThroughAStyleSheet()  // NOLINT
    {
        Gui::FreeCADStyle style;
        Gui::OverlaySplitter splitter(nullptr);
        splitter.addWidget(new QWidget);
        splitter.addWidget(new QWidget);
        QSplitterHandle* strip = splitter.handle(1);
        strip->setProperty("transparent", true);
        style.polish(strip);
        QCOMPARE(strip->font().weight(), 800);

        splitter.setStyleSheet(QStringLiteral("QSplitter { }"));

        QCOMPARE(strip->font().weight(), 800);
    }

    // QWidget::setContentsMargins answers itself with a synchronous resize carrying the size the
    // widget already had (qwidget.cpp:7693). Writing the inset again in reply is what sends the
    // next one, and the panel body's padding turned that into a flood thousands deep.
    void test_onlyAResizeThatMovedSomethingIsAnswered()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QMainWindow window;
        auto* body = new QWidget;
        dockInto(window, body, Qt::LeftDockWidgetArea);
        style.polish(body);
        QVERIFY(body->contentsMargins() != QMargins());

        body->setContentsMargins(0, 0, 0, 0);
        QResizeEvent unmoved(body->size(), body->size());
        QCoreApplication::sendEvent(body, &unmoved);
        QCOMPARE(body->contentsMargins(), QMargins());

        QResizeEvent moved(body->size() + QSize(1, 1), body->size());
        QCoreApplication::sendEvent(body, &moved);

        QVERIFY(body->contentsMargins() != QMargins());
    }

    // The dock area is not settled while the dock is being built: a body polished before
    // setWidget() finished is not recognisable as one yet, and one polished before the dock was
    // placed carries the inset for the wrong edge. Both showed up as a panel whose border only
    // appeared after it was dragged to another edge and back.
    void test_theBodyIsSetUpEvenWhenItWasPolishedBeforeTheDockWasPlaced()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QMainWindow window;
        auto* body = new QWidget;

        // Polished while still detached, which is what left it unrecognised.
        style.polish(body);
        QVERIFY(!body->testAttribute(Qt::WA_StyledBackground));

        QDockWidget* dock = dockInto(window, body, Qt::LeftDockWidgetArea);
        style.polish(dock);

        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        QVERIFY(body->testAttribute(Qt::WA_StyledBackground));
        QCOMPARE(body->contentsMargins(), QMargins(0, 0, NorthInset, 0));
    }

    // Dragging a panel to another edge moves the seam to the opposite side, and the inset that
    // reserves it has to follow. Nothing re-polishes a dock that is merely re-docked.
    void test_theBodyInsetFollowsTheDockToAnotherEdge()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QMainWindow window;
        auto* body = new QWidget;
        QDockWidget* dock = dockInto(window, body, Qt::LeftDockWidgetArea);
        style.polish(dock);
        style.polish(body);

        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QCOMPARE(body->contentsMargins(), QMargins(0, 0, NorthInset, 0));

        window.addDockWidget(Qt::RightDockWidgetArea, dock);
        QTest::qWait(1);

        QCOMPARE(body->contentsMargins(), QMargins(NorthInset, 0, 0, 0));
    }

    // Both runs of the strip are the same strip to a theme, so an overlaid one has to reach its
    // Transparent tokens whichever way it runs. Position and TransparencyMode are separate
    // variant slots and compose in the fallback, so a strip that also carries a run must not
    // lose the Transparent fragment on the way.
    void test_anOverlaidStripReachesItsTransparentTokensEitherRun_data()  // NOLINT
    {
        QTest::addColumn<QSize>("size");

        QTest::newRow("across") << QSize(120, 20);
        QTest::newRow("down") << QSize(20, 120);
    }

    void test_anOverlaidStripReachesItsTransparentTokensEitherRun()  // NOLINT
    {
        QFETCH(QSize, size);

        Gui::FreeCADStyle style;
        QTabWidget host;
        QWidget* strip = declaredTitleBar(&host);
        strip->resize(size);
        style.polish(strip);

        // What OverlayTabWidget does when a panel goes into overlay mode.
        strip->setProperty("transparent", true);
        style.updateTransparency(strip, false);

        QImage canvas(size, QImage::Format_RGB32);
        canvas.fill(Qt::black);

        QStyleOption option;
        option.rect = QRect(QPoint(), size);

        QPainter painter(&canvas);
        style.drawPrimitive(QStyle::PE_Widget, &option, &painter, strip);
        painter.end();

        QCOMPARE(canvas.pixel(size.width() / 2, size.height() / 2), OverlayColor);
    }

    /// Paints @p rect as a resize handle of @p window and returns what landed on the canvas.
    static QImage renderHandle(Gui::FreeCADStyle& style, QMainWindow& window, const QRect& rect)
    {
        QImage canvas(rect.size(), QImage::Format_RGB32);
        canvas.fill(Qt::black);

        // The handler reads the dock area off the rect, so the rect stays in window space and
        // the painter is moved instead - canvas (0, 0) is the handle's top left either way.
        QStyleOption option;
        option.rect = rect;

        QPainter painter(&canvas);
        painter.translate(-rect.topLeft());
        style.drawPrimitive(QStyle::PE_IndicatorDockWidgetResizeHandle, &option, &painter, &window);
        painter.end();

        return canvas;
    }

    // An overlaid top or bottom panel gets a title strip running down its left instead of across
    // its top, and every token the strip carries is stated for the horizontal one. Resolving it
    // at the strip's own run turns the rule onto the edge the content actually starts from, and
    // turns the padding with it - stated across the strip it would crush a narrow one instead.
    void test_aVerticalStripResolvesAtItsOwnRun()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QTabWidget host;

        QWidget* across = declaredTitleBar(&host);
        across->resize(120, 20);
        style.polish(across);

        QWidget* down = declaredTitleBar(&host);
        down->resize(20, 120);
        style.polish(down);

        // The rule sits on the edge the content starts from: below a strip that runs across its
        // panel, to the right of one that runs down it.
        QCOMPARE(across->contentsMargins(), QMargins(0, 0, 0, TitleRule));
        QCOMPARE(down->contentsMargins(), QMargins(0, 0, TitleRule, 0));
    }

    // The strip is laid out after it is polished, so the run it resolves at is not settled when
    // the padding is first applied. Shown rather than hidden on purpose: Qt defers a hidden
    // widget's resize event to its next show, so a hidden one would prove nothing either way.
    void test_aStripPicksUpItsRunWhenItIsLaidOut()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QWidget host;
        QWidget* strip = declaredTitleBar(&host);
        host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));

        strip->resize(120, 20);
        style.polish(strip);
        QCOMPARE(strip->contentsMargins(), QMargins(0, 0, 0, TitleRule));

        strip->resize(20, 120);

        QCOMPARE(strip->contentsMargins(), QMargins(0, 0, TitleRule, 0));
    }

    // The handle along a dock area's inner edge is the only surface that spans the whole area,
    // so it is where the seam can be drawn without breaking at every panel boundary. It meets
    // the central widget on one side, and that is the side the seam goes on: a left dock area's
    // handle carries it on the right.
    void test_theHandleMeetingTheCentralWidgetCarriesTheSeam()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QMainWindow window;
        window.setCentralWidget(new QWidget);
        dockInto(window, new QWidget, Qt::LeftDockWidgetArea);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        const QRect central = window.centralWidget()->geometry();
        // Where QDockAreaLayout::separatorRect puts it: flush against the central widget.
        const QRect handle(central.left() - 7, central.top(), 7, central.height());

        const QImage painted = renderHandle(style, window, handle);
        const int middle = painted.height() / 2;

        QCOMPARE(painted.pixel(painted.width() - 1, middle), BorderColor);
        QCOMPARE(painted.pixel(0, middle), HandleColor);
    }

    // A handle between one panel and the next touches no central widget. Rotating the seam onto
    // its default North would lay a stub straight across the column.
    void test_aHandleBetweenTwoPanelsCarriesNoSeam()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QMainWindow window;
        window.setCentralWidget(new QWidget);
        dockInto(window, new QWidget, Qt::LeftDockWidgetArea);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        const QRect central = window.centralWidget()->geometry();
        // Inside the dock area: a whole handle's width short of the central widget.
        const QRect handle(central.left() - 27, central.top() + 30, 20, 7);

        const QImage painted = renderHandle(style, window, handle);

        for (int x = 0; x < painted.width(); ++x) {
            for (int y = 0; y < painted.height(); ++y) {
                QCOMPARE(painted.pixel(x, y), HandleColor);
            }
        }
    }

    // The handle parts two panels and belongs to neither, so it is asked for with the main
    // window and has to resolve without a dock area to hang its context on.
    void test_theSeparatorTakesItsExtentFromTheToken()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QMainWindow window;

        const int extent = style.pixelMetric(QStyle::PM_DockWidgetSeparatorExtent, nullptr, &window);

        QCOMPARE(extent, SeparatorWidth);
    }

    // The inset only reserves room; the border still has to be painted into it, and on the same
    // edge. A body whose seam landed on the wrong side would keep the margins above green.
    void test_theBodySeamIsPaintedOnTheEdgeFacingTheCentralWidget()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QMainWindow window;
        auto* body = new QWidget;
        dockInto(window, body, Qt::LeftDockWidgetArea);
        style.polish(body);

        QImage canvas(20, 20, QImage::Format_RGB32);
        canvas.fill(Qt::black);

        QStyleOption option;
        option.rect = QRect(0, 0, canvas.width(), canvas.height());
        option.palette = body->palette();

        QPainter painter(&canvas);
        style.drawPrimitive(QStyle::PE_Widget, &option, &painter, body);
        painter.end();

        const int middle = canvas.height() / 2;

        // A left-docked panel meets the 3D view on its right edge, so that is where the seam is.
        QCOMPARE(canvas.pixel(canvas.width() - 1, middle), BorderColor);
        QCOMPARE(canvas.pixel(0, middle), BodyColor);
    }

    // Every check above polishes by hand. The real path is Qt polishing on first show, and it
    // polishes a widget once - so a title bar that reached the style before setTitleBarWidget()
    // installed it would never be recognised, and would never be asked for again. Setting the
    // application style is what puts the real path under test; a per-widget setStyle would not.
    void test_bothPanelPartsAreStyledOnTheRealPolishPath()  // NOLINT
    {
        // Not restored afterwards: setStyle() deletes the style it replaces, so the previous
        // pointer is dangling the moment this one is installed. This is the last check here.
        qApp->setStyle(new Gui::FreeCADStyle);

        QMainWindow window;
        auto* body = new QWidget;
        QDockWidget* dock = dockInto(window, body, Qt::LeftDockWidgetArea);

        // The construction order OverlayManager::setupTitleBar uses: the bar is built as a child
        // of the dock, declares itself, and only afterwards is installed as its title bar.
        QWidget* title = declaredTitleBar(dock);
        title->setLayout(new QHBoxLayout);
        dock->setTitleBarWidget(title);

        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        QVERIFY(body->testAttribute(Qt::WA_StyledBackground));

        // The strip takes its surface from its own paintEvent now, so the attribute proves
        // nothing about it. applyPanelStyle's other half does, and unlike the inset it does not
        // depend on which way round the strip was laid out by the time this runs.
        QVERIFY(!title->testAttribute(Qt::WA_StyledBackground));
        QCOMPARE(title->palette().color(QPalette::WindowText), QColor(255, 0, 255));
    }
};

QTEST_MAIN(TestPanelSurface)
#include "PanelSurface.moc"
