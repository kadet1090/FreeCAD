// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QApplication>
#include <QBoxLayout>
#include <QColor>
#include <QCoreApplication>
#include <QEvent>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QSizeGrip>
#include <QStatusBar>
#include <QStyleOption>
#include <QTest>
#include <QWidget>

#include "src/App/InitApplication.h"

#include <Gui/Application.h>
#include <Gui/FCStatusBar.h>
#include <Gui/FreeCADStyle.h>
#include <Gui/StyleParameters/ParameterManager.h>

namespace
{

// The fixture's own numbers, chosen so no two sums collide — a wrong token therefore fails
// loudly instead of coincidentally matching. The edge is 2px rather than 1px so a pixel
// sampled on it sits solidly inside the border rather than on a boundary.
constexpr int statusBarEdge = 2;
constexpr int statusBarHeight = 31;

const QColor statusBarBackground = QColor(QStringLiteral("#202020"));
const QColor statusBarEdgeColor = QColor(QStringLiteral("#00ff00"));

const QColor messageColor = QColor(QStringLiteral("#0000ff"));
const QColor warningColor = QColor(QStringLiteral("#ffff00"));
const QColor errorColor = QColor(QStringLiteral("#ff0000"));
const QColor fallbackColor = QColor(QStringLiteral("#123456"));

const QMargins statusBarPadding = QMargins(5, 3, 7, 4);
constexpr int statusBarItemSpacing = 9;

// A vertical inset stated in two unequal halves, so a bar that accounted for only one of them
// would be off by a number the other cannot coincidentally supply.
constexpr int statusBarInsetTop = 11;
constexpr int statusBarInsetBottom = 13;

// What Qt's own reformat() leaves between two items, and the margins it leaves around them.
constexpr int qtItemSpacing = 6;

// An item taller than the height the fixture states, so a bar that took its height from its
// tallest item could not come out at the stated one.
constexpr int statusBarTallItem = 64;

// Whether @p layout still holds a fixed (non-expanding) spacer sized along its own direction —
// the same fixed-vs-stretch test applyLayoutTokens() itself makes before deleting one.
bool hasFixedSpacer(QLayout* layout)
{
    auto* box = qobject_cast<QBoxLayout*>(layout);

    if (box == nullptr) {
        return false;
    }

    const QBoxLayout::Direction direction = box->direction();
    const bool horizontal = direction == QBoxLayout::LeftToRight
        || direction == QBoxLayout::RightToLeft;

    for (int index = 0; index < box->count(); ++index) {
        QLayoutItem* item = box->itemAt(index);
        QSpacerItem* spacer = item->spacerItem();

        if (spacer == nullptr || item->expandingDirections() != Qt::Orientations {}) {
            continue;
        }

        const QSize extent = spacer->sizeHint();

        if ((horizontal ? extent.width() : extent.height()) > 0) {
            return true;
        }
    }

    return false;
}

// The layout @p item was put into, found the way the bar itself has to find it: by asking every
// layout in the tree who holds it.
QLayout* layoutHolding(QWidget* bar, QWidget* item)
{
    for (QLayout* candidate : bar->findChildren<QLayout*>()) {
        if (candidate->indexOf(item) >= 0) {
            return candidate;
        }
    }

    return nullptr;
}

}  // namespace

class TestStatusBarSurface: public QObject
{
    Q_OBJECT

public:
    TestStatusBarSurface()
    {
        tests::initApplication();

        if (Gui::Application::Instance == nullptr) {
            new Gui::Application(true);
        }

        Gui::Application::Instance->styleParameterManager()->addSource(
            new Gui::StyleParameters::InMemoryParameterSource(
                {
                    {.name = "StatusBarBackground", .value = "#202020"},
                    {.name = "StatusBarBorderColor", .value = "#00ff00"},
                    {.name = "StatusBarBorderThickness", .value = "border_thickness(0px, top: 2px)"},
                    {.name = "StatusBarHeight", .value = "31px"},
                    {.name = "StatusBarMessageTextColor", .value = "#0000ff"},
                    {.name = "StatusBarMessageWarningTextColor", .value = "#ffff00"},
                    {.name = "StatusBarMessageErrorTextColor", .value = "#ff0000"},
                    {.name = "StatusBarPadding",
                     .value = "padding(left: 5px, top: 3px, right: 7px, bottom: 4px)"},
                    {.name = "StatusBarItemSpacing", .value = "9px"},
                },
                {.name = "Status Bar Surface"}
            )
        );
    }

private:
    // Paints the bar panel over magenta and hands back the canvas, so a test can ask both what
    // was painted and whether anything was.
    static QImage paintedPanel(QStyle& style, QWidget* widget)
    {
        QStyleOption option;
        option.initFrom(widget);
        option.rect = QRect(0, 0, 80, 24);

        QImage canvas(option.rect.size(), QImage::Format_ARGB32);
        canvas.fill(Qt::magenta);

        QPainter painter(&canvas);
        style.drawPrimitive(QStyle::PE_PanelStatusBar, &option, &painter, widget);
        painter.end();

        return canvas;
    }

    // Whether anything on the canvas was painted in exactly @p color. Glyph cores are solid at
    // this font size, so an antialiased edge never has to be the thing under test.
    static bool containsColor(const QImage& canvas, const QColor& color)
    {
        for (int row = 0; row < canvas.height(); ++row) {
            for (int column = 0; column < canvas.width(); ++column) {
                if (canvas.pixelColor(column, row) == color) {
                    return true;
                }
            }
        }

        return false;
    }

    // The minimum a bar settles on once @p padding is the inset it is stated to take, with the
    // stated height taken out of the way so that what is measured is the inset alone.
    static int minimumHeightUnder(Gui::FreeCADStyle& style, const QString& padding)
    {
        Gui::FCStatusBar bar;
        bar.setStyle(&style);
        style.polish(&bar);

        Gui::FreeCADStyle::setStyleOverride(
            &bar,
            QStringLiteral("StatusBarHeight"),
            QStringLiteral("reset()")
        );
        Gui::FreeCADStyle::setStyleOverride(&bar, QStringLiteral("StatusBarPadding"), padding);

        QEvent styleChange(QEvent::StyleChange);
        QCoreApplication::sendEvent(&bar, &styleChange);

        return bar.minimumHeight();
    }

    // A bar wide enough for a message, with one permanent item so messageRect() has a bound to
    // find, rendered at a font size whose strokes are solid.
    static QImage renderedBar(Gui::FCStatusBar& bar, Gui::StyleParameters::MessageLevel level)
    {
        bar.setMessageLevel(level);
        bar.showMessage(QStringLiteral("Recompute failed"));

        QImage canvas(bar.size(), QImage::Format_ARGB32);
        canvas.fill(Qt::magenta);
        bar.render(&canvas, QPoint(), QRegion(), QWidget::DrawWindowBackground);

        return canvas;
    }

private Q_SLOTS:
    void test_theSurfaceIsPaintedFromTheStatusBarTokens()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QStatusBar bar;

        const QImage canvas = paintedPanel(style, &bar);

        QCOMPARE(canvas.pixelColor(40, 12), statusBarBackground);
        QCOMPARE(canvas.pixelColor(40, 0), statusBarEdgeColor);
        QCOMPARE(canvas.pixelColor(40, statusBarEdge), statusBarBackground);
    }

    void test_aBarWithNoStatedSurfaceKeepsTheBaseStyles()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QStatusBar bar;
        bar.setStyle(&style);
        style.polish(&bar);

        // An override of "reset()" is how a theme states nothing at all for a token, so this is
        // the same silence a theme without a StatusBar block leaves. Polished first: an override
        // reaches the resolver through the set stored on the widget, which polish() is what puts
        // there.
        Gui::FreeCADStyle::setStyleOverride(
            &bar,
            QStringLiteral("StatusBarBackground"),
            QStringLiteral("reset()")
        );

        const QImage canvas = paintedPanel(style, &bar);

        // Fusion paints nothing for this primitive, so declining leaves the canvas as it was.
        // The edge is stated on its own token and untouched by this override, so sampling it too
        // is what tells a full decline apart from one that merely skipped the fill: painting the
        // border alone would leave this pixel green instead.
        QCOMPARE(canvas.pixelColor(40, 12), QColor(Qt::magenta));
        QCOMPARE(canvas.pixelColor(40, 0), QColor(Qt::magenta));
    }

    void test_theBarIsHeldAtTheStatedHeight()  // NOLINT
    {
        Gui::FreeCADStyle style;

        QWidget host;
        Gui::FCStatusBar bar(&host);
        bar.setStyle(&style);
        style.polish(&bar);

        // Both ends, not a floor: the main window reads the bar's minimum, and the ceiling is
        // what stops the layout beneath spending anything it is given above the stated height.
        QCOMPARE(bar.minimumHeight(), statusBarHeight);
        QCOMPARE(bar.maximumHeight(), statusBarHeight);
    }

    void test_theStatedInsetReachesTheBarsOwnMinimum()  // NOLINT
    {
        Gui::FreeCADStyle style;

        const int inset = minimumHeightUnder(
            style,
            QStringLiteral("padding(left: 5px, top: %1px, right: 7px, bottom: %2px)")
                .arg(statusBarInsetTop)
                .arg(statusBarInsetBottom)
        );
        const int flat = minimumHeightUnder(
            style,
            QStringLiteral("padding(left: 5px, top: 0px, right: 7px, bottom: 0px)")
        );

        // The bar's own minimum is the only channel the main window reads: it sizes the bar from
        // that and never from its size hint, and an explicit minimum outranks whatever the
        // layout beneath asks for. An inset that misses this misses the window.
        QCOMPARE(inset - flat, statusBarInsetTop + statusBarInsetBottom);
    }

    void test_aTallItemDoesNotChangeTheStatedHeight()  // NOLINT
    {
        Gui::FreeCADStyle style;
        Gui::FCStatusBar bar;
        bar.setStyle(&style);
        style.polish(&bar);

        // An item arriving after the bar was polished makes the layout the last thing to speak
        // for the bar's height, which is the order a real bar is filled in. Qt sizes the bar to
        // its tallest item, so this is what a stated height has to outrank: which item is the
        // tallest depends on the workbench, and a bar sized that way changes height as they
        // come and go.
        auto* tall = new QLabel(QStringLiteral("mm"), &bar);
        tall->setFixedHeight(statusBarTallItem);
        bar.addPermanentWidget(tall);
        QCoreApplication::sendPostedEvents(&bar, QEvent::LayoutRequest);

        QCOMPARE(bar.minimumHeight(), statusBarHeight);
        QCOMPARE(bar.maximumHeight(), statusBarHeight);
    }

    void test_aThemeThatDropsTheHeightHandsTheBarBack()  // NOLINT
    {
        Gui::FreeCADStyle style;

        QWidget host;
        Gui::FCStatusBar bar(&host);
        bar.setStyle(&style);
        style.polish(&bar);

        // Held by the fixture's stated height first, so what follows is a change of theme rather
        // than a bar that never had one.
        QCOMPARE(bar.maximumHeight(), statusBarHeight);

        // The theme that replaces it states an inset but no height, which is the whole of what
        // makes this different from the silent case: the bar still has tokens to answer, so the
        // ceiling has to be lifted on the way past rather than by declining to act at all.
        Gui::FreeCADStyle::setStyleOverride(
            &bar,
            QStringLiteral("StatusBarHeight"),
            QStringLiteral("reset()")
        );

        QEvent styleChange(QEvent::StyleChange);
        QCoreApplication::sendEvent(&bar, &styleChange);

        QCOMPARE(bar.maximumHeight(), QWIDGETSIZE_MAX);
    }

    void test_aBarWithNoStatedLayoutKeepsQtsOwn()  // NOLINT
    {
        Gui::FreeCADStyle style;

        // Parented as the real bar is. Qt pushes a layout's own minimum onto a widget only when
        // that widget is a window, so a bare one would answer for Qt what it answers for itself.
        QWidget host;
        Gui::FCStatusBar bar(&host);
        bar.setStyle(&style);
        style.polish(&bar);

        // The silence a theme without a StatusBar block leaves. FreeCAD Classic ships no
        // parameters file at all, so none of these is stated anywhere under it.
        Gui::FreeCADStyle::setStyleOverride(
            &bar,
            QStringLiteral("StatusBarPadding"),
            QStringLiteral("reset()")
        );
        Gui::FreeCADStyle::setStyleOverride(
            &bar,
            QStringLiteral("StatusBarItemSpacing"),
            QStringLiteral("reset()")
        );
        Gui::FreeCADStyle::setStyleOverride(
            &bar,
            QStringLiteral("StatusBarHeight"),
            QStringLiteral("reset()")
        );

        // Adding an item is what makes Qt rebuild the layout from its own constants, so this is
        // the layout such a theme leaves the bar holding.
        auto* first = new QLabel(QStringLiteral("mm"), &bar);
        bar.addPermanentWidget(first);
        QCoreApplication::sendPostedEvents(&bar, QEvent::LayoutRequest);

        QLayout* itemLayout = layoutHolding(&bar, first);

        QVERIFY(itemLayout != nullptr);

        // Qt's own inset, gap and spacers, untouched: the bar has to look exactly as it did
        // before the component existed, not flush against its items and the window edge.
        QCOMPARE(bar.layout()->contentsMargins(), QMargins());
        QCOMPARE(itemLayout->spacing(), qtItemSpacing);
        QVERIFY(hasFixedSpacer(itemLayout));

        // And neither bound of our own: a minimum would outrank everything the layout asks for,
        // and a ceiling left behind by a theme that stated a height would hold the bar at a
        // height the theme now in force says nothing about.
        QCOMPARE(bar.minimumHeight(), 0);
        QCOMPARE(bar.maximumHeight(), QWIDGETSIZE_MAX);
    }

    void test_aBarStatingOnlyItsHeightStillReachesTheWindow()  // NOLINT
    {
        Gui::FreeCADStyle style;

        QWidget host;
        Gui::FCStatusBar bar(&host);
        bar.setStyle(&style);
        style.polish(&bar);

        // Only the layout tokens are silenced. A component that states one of the three is not
        // a component that states nothing, so declining must not swallow the height with them.
        Gui::FreeCADStyle::setStyleOverride(
            &bar,
            QStringLiteral("StatusBarPadding"),
            QStringLiteral("reset()")
        );
        Gui::FreeCADStyle::setStyleOverride(
            &bar,
            QStringLiteral("StatusBarItemSpacing"),
            QStringLiteral("reset()")
        );

        auto* tall = new QLabel(QStringLiteral("mm"), &bar);
        tall->setFixedHeight(statusBarTallItem);
        bar.addPermanentWidget(tall);
        QCoreApplication::sendPostedEvents(&bar, QEvent::LayoutRequest);

        // An explicit height outranks what the layout beneath asks for, so an item taller than
        // the bar is held to the bar rather than the other way round.
        QCOMPARE(bar.minimumHeight(), statusBarHeight);
        QCOMPARE(bar.maximumHeight(), statusBarHeight);

        // Qt's own spacing is still Qt's: a height says nothing about where the items sit.
        QVERIFY(hasFixedSpacer(layoutHolding(&bar, tall)));
    }

    void test_aBarWithNoItemsLeavesTheGripLayoutAlone()  // NOLINT
    {
        Gui::FreeCADStyle style;
        Gui::FCStatusBar bar;
        bar.setStyle(&style);
        style.polish(&bar);

        // Nothing has been added yet, so there is no item layout for a gap to go on. The outer
        // layout is the one holding the size grip, and a gap written there parts it from the
        // bar's edge.
        QVERIFY(bar.layout()->spacing() != statusBarItemSpacing);
    }

    void test_eachLevelResolvesItsOwnColour()  // NOLINT
    {
        using Gui::StyleParameters::MessageLevel;

        Gui::FreeCADStyle style;
        QStatusBar bar;
        bar.setStyle(&style);

        QCOMPARE(
            Gui::FreeCADStyle::statusMessageColor(&bar, MessageLevel::Default, fallbackColor),
            messageColor
        );
        QCOMPARE(
            Gui::FreeCADStyle::statusMessageColor(&bar, MessageLevel::Warning, fallbackColor),
            warningColor
        );
        QCOMPARE(
            Gui::FreeCADStyle::statusMessageColor(&bar, MessageLevel::Error, fallbackColor),
            errorColor
        );
    }

    void test_aLevelWithNoColourOfItsOwnFallsBackToTheMessageToken()  // NOLINT
    {
        using Gui::StyleParameters::MessageLevel;

        Gui::FreeCADStyle style;
        QStatusBar bar;
        bar.setStyle(&style);

        // The fixture states no Critical colour, so the severity variant has to fall through to
        // the unqualified message token rather than to the caller's fallback.
        QCOMPARE(
            Gui::FreeCADStyle::statusMessageColor(&bar, MessageLevel::Critical, fallbackColor),
            messageColor
        );
    }

    void test_aStatedOverrideOutranksTheThemeColour()  // NOLINT
    {
        using Gui::StyleParameters::MessageLevel;

        Gui::FreeCADStyle style;
        QStatusBar bar;
        bar.setStyle(&style);
        style.polish(&bar);

        // Exactly the call StatusBarObserver makes when the user has set an OutputWindow colour.
        Gui::FreeCADStyle::setStyleOverride(
            &bar,
            QStringLiteral("StatusBarMessageErrorTextColor"),
            QStringLiteral("#00ffff")
        );

        QCOMPARE(
            Gui::FreeCADStyle::statusMessageColor(&bar, MessageLevel::Error, fallbackColor),
            QColor(QStringLiteral("#00ffff"))
        );
    }

    void test_theBarPaintsItsOwnSurface()  // NOLINT
    {
        Gui::FreeCADStyle style;
        Gui::FCStatusBar bar;
        bar.setStyle(&style);
        bar.resize(200, 30);

        QImage canvas(bar.size(), QImage::Format_ARGB32);
        canvas.fill(Qt::magenta);
        bar.render(&canvas, QPoint(), QRegion(), QWidget::DrawWindowBackground);

        QCOMPARE(canvas.pixelColor(100, 15), statusBarBackground);
        QCOMPARE(canvas.pixelColor(100, 0), statusBarEdgeColor);
    }

    void test_theMessageIsPaintedInItsLevelsColour()  // NOLINT
    {
        using Gui::StyleParameters::MessageLevel;

        Gui::FreeCADStyle style;
        Gui::FCStatusBar bar;
        bar.setStyle(&style);
        bar.setFont(QFont(bar.font().family(), 20));
        bar.resize(400, 40);
        bar.addPermanentWidget(new QLabel(QStringLiteral("mm"), &bar));

        QVERIFY(containsColor(renderedBar(bar, MessageLevel::Error), errorColor));
        QVERIFY(containsColor(renderedBar(bar, MessageLevel::Warning), warningColor));
    }

    void test_theLevelChangesWhatIsPainted()  // NOLINT
    {
        using Gui::StyleParameters::MessageLevel;

        Gui::FreeCADStyle style;
        Gui::FCStatusBar bar;
        bar.setStyle(&style);
        bar.setFont(QFont(bar.font().family(), 20));
        bar.resize(400, 40);

        // The same message twice: only the level differs, so a bar that ignored it would paint
        // the identical image both times.
        QVERIFY(renderedBar(bar, MessageLevel::Error) != renderedBar(bar, MessageLevel::Default));
    }

    // Qt activates the layout while answering the request, and the tokens are written after
    // that - so the children are placed against the margins the layout had *before* this ran.
    // Nothing else re-lays them out: the invalidate that setContentsMargins() posts is a request
    // for a layout pass that, for a bar whose items have stopped changing, never comes. The bar
    // then paints with its inset recorded and none of it honoured, which is a status bar whose
    // items sit flush against the window edge while every margin reads correctly.
    void test_theLayoutTakesThePaddingAndSpacingTokens()  // NOLINT
    {
        Gui::FreeCADStyle style;
        Gui::FCStatusBar bar;
        bar.setStyle(&style);
        bar.resize(400, 40);

        auto* first = new QLabel(QStringLiteral("mm"), &bar);
        bar.addPermanentWidget(first);
        bar.addPermanentWidget(new QLabel(QStringLiteral("deg"), &bar));

        // The rebuild posts the layout request rather than sending it, and the request is the
        // hook. Delivering it here is what a real bar gets before its next paint.
        QCoreApplication::sendPostedEvents(&bar, QEvent::LayoutRequest);

        // A rebuild is what Qt does on every item added, so this is also the state the bar is
        // left in after any later one.
        QCOMPARE(bar.layout()->contentsMargins(), statusBarPadding);

        QLayout* itemLayout = layoutHolding(&bar, first);

        QVERIFY(itemLayout != nullptr);
        QCOMPARE(itemLayout->spacing(), statusBarItemSpacing);

        // Qt's own spacing would otherwise sit inside the padding and add to it, making the token
        // a description of part of the inset rather than of the inset. Its strut has to survive
        // the same pass: that is what holds the bar to the height of its tallest item.
        bool spacingRemains = false;
        bool strutSurvives = false;

        for (int index = 0; index < itemLayout->count(); ++index) {
            QLayoutItem* item = itemLayout->itemAt(index);
            QSpacerItem* spacer = item->spacerItem();

            if (spacer == nullptr || item->expandingDirections() != Qt::Orientations {}) {
                continue;
            }

            if (spacer->sizeHint().width() > 0) {
                spacingRemains = true;
            }
            else if (spacer->sizeHint().height() > 0) {
                strutSurvives = true;
            }
        }

        QVERIFY(!spacingRemains);
        QVERIFY(strutSurvives);

        // The size grip is enabled by default, and nothing in FreeCAD disables it. Its own gap
        // is not part of the bar's inset, so the sweep must leave whichever layout holds the
        // grip alone; this is the regression test for that.
        QLayout* gripLayout = nullptr;
        for (QLayout* candidate : bar.findChildren<QLayout*>()) {
            for (int index = 0; index < candidate->count(); ++index) {
                if (qobject_cast<QSizeGrip*>(candidate->itemAt(index)->widget()) != nullptr) {
                    gripLayout = candidate;
                }
            }
        }

        QVERIFY(gripLayout != nullptr);
        QVERIFY(hasFixedSpacer(gripLayout));

        // Qt nests a vertical layout between the outer one and the item layout, and spends the
        // bar's vertical inset there. Found the same way itemLayout was: by asking who holds it.
        QLayout* verticalLayout = nullptr;
        for (QLayout* candidate : bar.findChildren<QLayout*>()) {
            if (candidate->indexOf(itemLayout) >= 0) {
                verticalLayout = candidate;
            }
        }

        QVERIFY(verticalLayout != nullptr);
        QVERIFY(!hasFixedSpacer(verticalLayout));
    }
};

QTEST_MAIN(TestStatusBarSurface)
#include "StatusBarSurface.moc"
