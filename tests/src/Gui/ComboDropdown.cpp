// SPDX-License-Identifier: LGPL-2.1-or-later

#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <utility>

#include <QAbstractItemModel>
#include <QApplication>
#include <QComboBox>
#include <QFrame>
#include <QGuiApplication>
#include <QImage>
#include <QListView>
#include <QMouseEvent>
#include <QPainter>
#include <QRegion>
#include <QScopeGuard>
#include <QScreen>
#include <QScrollBar>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QStyleFactory>
#include <QStyleOptionComboBox>
#include <QStyleOptionFrame>
#include <QStyleOptionMenuItem>
#include <QStyleOptionViewItem>
#include <QTest>
#include <QWindow>

#include <Gui/ThemeReloadEvent.h>

#include "DropdownStyleFixture.h"

// Counts the paints arriving at whatever it is installed on, so a test can tell "the style asked
// for a repaint" apart from "something repainted eventually".
struct PaintCounter: QObject
{
    int paints = 0;

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Paint) {
            ++paints;
        }
        return QObject::eventFilter(watched, event);
    }
};

class TestComboDropdown: public QObject, private DropdownStyleFixture
{
    Q_OBJECT

private:
    // A dropdown that is not a combo box's: a bare view adopted through the public entry point,
    // laid out and shown so its rows have real geometry to point at. Left unexposed on purpose —
    // the caller waits for that, because only a caller can QVERIFY.
    static QListView* buildAdoptedDropdown(QFrame& container, Gui::FreeCADStyle& style)
    {
        container.setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
        auto* view = new QListView(&container);
        auto* model = new QStandardItemModel(&container);
        for (const QString& label :
             {QStringLiteral("first"), QStringLiteral("second"), QStringLiteral("third")}) {
            model->appendRow(new QStandardItem(label));
        }
        view->setModel(model);

        style.constrainDropdown(view);

        container.resize(200, 200);
        view->resize(200, 200);
        container.show();
        return view;
    }

    // A separator row, marked the way QComboBox::insertSeparator() marks one: the role Qt reads
    // it back from, and the flags that make QListView::moveCursor step over the row.
    static void markAsSeparator(QListView& view, int row)
    {
        auto* model = qobject_cast<QStandardItemModel*>(view.model());
        Q_ASSERT(model != nullptr);
        QStandardItem* item = model->item(row);
        item->setData(QStringLiteral("separator"), Qt::AccessibleDescriptionRole);
        item->setFlags(item->flags() & ~(Qt::ItemIsSelectable | Qt::ItemIsEnabled));
    }

    // The pointer arriving over @p spot and the left button going down on it. Routed through the
    // window rather than the widget: QTest::mouseMove's QWidget overload warps the real desktop
    // pointer via QCursor::setPos and synthesises no move at all when the cursor already sits
    // there, so the row would never be hovered — the same trap the popup suite documents.
    //
    // The returned guard lets the button up. A held button is application-global state, so a
    // test that returned early without it would leave every later test pressing.
    [[nodiscard]] static auto holdLeftButton(QWidget& container, const QPoint& spot)
    {
        QWindow* window = container.windowHandle();

        QTest::mouseMove(window, spot);
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, spot);
        QCoreApplication::processEvents();

        return qScopeGuard([window, spot] {
            QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, spot);
            QCoreApplication::processEvents();
        });
    }

    // Where the first row's box started before every row was given a leading gap: the container
    // inset and nothing else, because row 0 alone reserved no gap above itself. The change adds
    // that gap to row 0 and takes the same amount off the top inset, so this must not move.
    static constexpr int baselineFirstRowTop = containerBorder + containerPadding;

    // The popup's total height as the pre-change model computed it, on 2026-08-09: one gap
    // between each adjacent pair of rows and none above the first, inside the container's
    // border and padding.
    //
    // A formula rather than the raw pixel count it was measured at, because the label's height
    // comes from the ambient font and the platform theme picks that — the number is 100 under
    // qt5ct and 97 with no theme plugin, while the invariant holds in both.
    static int baselineContainerHeight(const QListView& view, int rowCount)
    {
        const int rowHeight = view.fontMetrics().height() + (2 * itemPaddingVertical);

        return (2 * containerBorder) + (2 * containerPadding) + (rowCount * rowHeight)
            + ((rowCount - 1) * itemSpacing);
    }

    // The popup list is created lazily by QComboBox, so ask for it only after polishing.
    static QListView* popupOf(QComboBox& box)
    {
        return qobject_cast<QListView*>(box.view());
    }

    // Short labels, so a correctly sized popup sits well under the cap and a failure can only
    // be about the row height, never about the content being genuinely too tall.
    static void populate(QComboBox& box)
    {
        box.addItems({QStringLiteral("Alpha"), QStringLiteral("Beta"), QStringLiteral("Gamma")});
    }

    // Far more rows than DropdownListMaxHeight can hold, so the popup is capped and has to
    // scroll — the only shape in which a partial row's worth of surface can be left over.
    static void populateBeyondTheCap(QComboBox& box)
    {
        for (int row = 0; row < 40; ++row) {
            box.addItem(QStringLiteral("Item %1").arg(row));
        }
    }

    // A menu-item option shaped the way QComboMenuDelegate builds one: the widget is the combo
    // box, never a QMenu, and the rect is the whole list-view viewport rather than a row.
    static QStyleOptionMenuItem comboRowItem(const QComboBox& box, const QListView& view)
    {
        QStyleOptionMenuItem option;
        option.initFrom(&box);
        option.menuItemType = QStyleOptionMenuItem::Normal;
        option.checkType = QStyleOptionMenuItem::NotCheckable;
        option.checked = false;
        option.menuHasCheckableItems = false;
        option.maxIconWidth = 0;
        option.reservedShortcutWidth = 0;
        option.text = QStringLiteral("Alpha");
        option.rect = view.rect();
        return option;
    }

    // The y at which the popup's first row starts painting, measured on the container so the
    // frame's own inset counts. Row 0 always carries an interaction fill — it is the combo's
    // chosen entry and also the row Qt makes current when the popup opens — so its box is the
    // topmost pixel down the popup's centre that is neither the surface nor the surface's edge.
    // Which of the two interaction fills wins is a matter of state priority, and a geometry
    // measurement has no business depending on that.
    // The box, not the cell: the cell above it also holds the leading gap, which is exactly
    // what moved, so a cell-based measurement would report the move rather than survive it.
    static int firstRowBoxTop(QWidget& container)
    {
        const QImage canvas = renderOf(container);
        const int middle = canvas.width() / 2;

        for (int y = 0; y < canvas.height(); ++y) {
            const QColor pixel = canvas.pixelColor(middle, y);
            if (pixel != surfaceColor && pixel != surfaceBorderColor) {
                return y;
            }
        }

        return -1;
    }

    // The height of row 0 of a freshly shown popup, under a style built after the caller's
    // token overrides.
    static int firstRowHeightWithAFreshStyle()
    {
        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QComboBox box;
        populate(box);
        style.polish(&box);

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });

        return popupOf(box)->visualRect(box.model()->index(0, 0)).height();
    }

private Q_SLOTS:

    // The property this change exists to create: every row the same height, so the pitch
    // between any adjacent pair is identical. Row 0 was previously shorter by exactly the
    // gap, which is what made a capped popup's trim leave a residue.
    void test_everyRowHasTheSamePitch()  // NOLINT
    {
        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QComboBox box;
        populate(box);
        style.polish(&box);

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });

        QListView* view = popupOf(box);
        QAbstractItemModel* model = box.model();

        const int firstPitch = view->visualRect(model->index(1, 0)).top()
            - view->visualRect(model->index(0, 0)).top();

        for (int row = 2; row < model->rowCount(); ++row) {
            const int pitch = view->visualRect(model->index(row, 0)).top()
                - view->visualRect(model->index(row - 1, 0)).top();
            QCOMPARE(pitch, firstPitch);
        }

        // And every row is the same height, not merely evenly spaced.
        const int firstHeight = view->visualRect(model->index(0, 0)).height();
        for (int row = 1; row < model->rowCount(); ++row) {
            QCOMPARE(view->visualRect(model->index(row, 0)).height(), firstHeight);
        }
    }

    // The cancellation is the design's core claim: the gap added to row 0's height is exactly
    // the gap removed from the container's top inset, so nothing moves and nothing grows.
    // These constants are the PRE-CHANGE baseline, measured against the build before this
    // work. This test must pass BOTH before and after. If it goes red, the cancellation is
    // wrong and the design is broken — not the test.
    void test_totalHeightAndFirstRowPositionAreUnchanged()  // NOLINT
    {
        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QComboBox box;
        populate(box);
        style.polish(&box);

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });

        QListView* view = popupOf(box);
        QWidget* container = view->parentWidget();

        QCOMPARE(container->height(), baselineContainerHeight(*view, box.count()));
        QCOMPARE(firstRowBoxTop(*container), baselineFirstRowTop);
    }

    // The model only works while the gap fits inside the padding. Past that the top inset
    // would have to go negative, which SE_ShapedFrameContents cannot express, so it clamps
    // and the first row sits `spacing - padding` lower. Documented, not silent.
    void test_aGapLargerThanThePaddingClampsRatherThanInverting()  // NOLINT
    {
        const auto paddingGuard = overrideToken("DropdownListPadding", "padding(2px)");
        const auto spacingGuard = overrideToken("DropdownListItemSpacing", "8px");

        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QComboBox box;
        populate(box);
        style.polish(&box);

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });

        QWidget* container = popupOf(box)->parentWidget();

        // Border 1 + clamped top padding 0 + the row's own 8px leading gap.
        QCOMPARE(firstRowBoxTop(*container), 1 + 0 + 8);
    }

    // Without an override the shared DropdownList cap applies, as it does to every combo box.
    void test_sharedCapApplies()  // NOLINT
    {
        QComboBox box;
        Gui::FreeCADStyle style;
        style.polish(&box);

        QCOMPARE(popupOf(box)->maximumHeight(), 250);
        QCOMPARE(popupOf(box)->parentWidget()->maximumHeight(), 250);
    }

    // The named component is consulted ahead of DropdownList, so one dropdown can differ.
    void test_overrideReplacesSharedCap()  // NOLINT
    {
        QComboBox box;
        box.setProperty("dropdownComponent", "ShortDropdown");

        Gui::FreeCADStyle style;
        style.polish(&box);

        QCOMPARE(popupOf(box)->maximumHeight(), 80);
        QCOMPARE(popupOf(box)->parentWidget()->maximumHeight(), 80);
    }

    // reset() cancels the cap rather than falling through to the shared one, leaving the
    // dropdown to Qt — which is what lets a combo box show a row per item.
    void test_overrideCanCancelTheCap()  // NOLINT
    {
        QComboBox box;
        box.setProperty("dropdownComponent", "UncappedDropdown");

        Gui::FreeCADStyle style;
        style.polish(&box);

        QCOMPARE(popupOf(box)->maximumHeight(), QWIDGETSIZE_MAX);
        QCOMPARE(popupOf(box)->parentWidget()->maximumHeight(), QWIDGETSIZE_MAX);
    }

    // The override names the popup, not the combo box, so the combo box keeps resolving as one.
    void test_overrideDoesNotRenameTheComboBox()  // NOLINT
    {
        QComboBox box;
        box.setProperty("dropdownComponent", "ShortDropdown");

        Gui::FreeCADStyle style;
        style.polish(&box);

        QVERIFY(!box.property("component").isValid());
        QCOMPARE(popupOf(box)->property("component").toString(), QStringLiteral("ShortDropdown"));
    }

    // The cap only bounds the popup; it says nothing about what is inside it. With
    // SH_ComboBox_Popup on, QComboBox sizes its rows through a QComboMenuDelegate, which asks
    // the style for CT_MenuItem passing the combo box as the widget and the whole viewport as
    // the contents size. A menu handler that answers that question instead of declining to the
    // base style makes one row as tall as the entire popup, and every other row falls out of it.
    void test_everyRowOfAPopulatedPopupIsVisible()  // NOLINT
    {
        Gui::FreeCADStyle style;
        QComboBox box;
        box.setStyle(&style);
        populate(box);
        style.polish(&box);

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });

        QListView* view = popupOf(box);
        const int rowHeight = view->visualRect(box.model()->index(0, 0)).height();
        QVERIFY2(rowHeight > 0, qPrintable(QStringLiteral("row height %1px").arg(rowHeight)));
        QVERIFY2(
            rowHeight < view->maximumHeight(),
            qPrintable(QStringLiteral("row height %1px does not fit the %2px popup")
                           .arg(rowHeight)
                           .arg(view->maximumHeight()))
        );

        // Three short rows stack well inside the cap, so the popup shows every one of them
        // without scrolling. Only the vertical extent is asserted: a menu-style row is as wide
        // as the widest label plus the shortcut column and is routinely wider than the popup,
        // which clips it — but a row that starts or ends outside the viewport is not on screen.
        const QRect viewport = view->viewport()->rect();
        for (int row = 0; row < box.count(); ++row) {
            const QRect itemRect = view->visualRect(box.model()->index(row, 0));
            QVERIFY2(
                itemRect.top() >= viewport.top() && itemRect.bottom() <= viewport.bottom(),
                qPrintable(QStringLiteral("row %1 spans y %2..%3, outside the viewport's %4..%5")
                               .arg(row)
                               .arg(itemRect.top())
                               .arg(itemRect.bottom())
                               .arg(viewport.top())
                               .arg(viewport.bottom()))
            );
        }
    }

    // The general guard for the whole class of bug: a handler that decides it does not own a
    // widget must let the base style answer, never invent a value of its own. CT_MenuItem is
    // reachable with a QComboBox because of QComboMenuDelegate, so the combo box is the honest
    // subject here — but the claim is about declining, not about combo boxes.
    void test_menuItemSizingDeclinesToTheBaseStyle()  // NOLINT
    {
        Gui::FreeCADStyle freecadStyle;
        QStyle& style = freecadStyle;
        QComboBox box;
        box.setStyle(&freecadStyle);
        populate(box);
        freecadStyle.polish(&box);

        // FreeCADStyle proxies Fusion, so Fusion is the baseline — not QApplication::style(),
        // which a platform theme plugin (qt5ct, qt6ct) replaces, making the comparison depend
        // on the developer's environment.
        const std::unique_ptr<QStyle> fusion(QStyleFactory::create(QStringLiteral("Fusion")));
        QVERIFY(fusion != nullptr);

        QListView* view = popupOf(box);
        const QStyleOptionMenuItem option = comboRowItem(box, *view);
        const QSize contentsSize = view->rect().size();

        QCOMPARE(
            style.sizeFromContents(QStyle::CT_MenuItem, &option, contentsSize, &box),
            fusion->sizeFromContents(QStyle::CT_MenuItem, &option, contentsSize, &box)
        );
    }

    // QComboBoxPrivateContainer paints its popup surface with PE_PanelMenu, but it is a plain
    // QFrame and not a QMenu, so the menu surface handler does not own it. Declining has to mean
    // handing the surface back to the base style, which fills it and draws its 1px outer frame;
    // painting nothing leaves the popup with no edge at all.
    void test_popupSurfaceKeepsTheBaseStyleFrame()  // NOLINT
    {
        Gui::FreeCADStyle freecadStyle;
        QStyle& style = freecadStyle;
        QComboBox box;
        box.setStyle(&freecadStyle);
        populate(box);
        freecadStyle.polish(&box);

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });

        // The real popup container, at the size and with the palette it was just shown with.
        QWidget* container = popupOf(box)->parentWidget();
        QStyleOption option;
        option.initFrom(container);

        const auto surfaceOf = [container, &option](QStyle& painting) {
            QImage canvas(container->size(), QImage::Format_ARGB32);
            canvas.fill(Qt::magenta);

            QPainter painter(&canvas);
            painting.drawPrimitive(QStyle::PE_PanelMenu, &option, &painter, container);
            painter.end();

            return canvas;
        };

        // FreeCADStyle proxies Fusion, so Fusion is the baseline a widget it declines must
        // still see — not QApplication::style(), which a platform theme plugin (qt5ct, qt6ct)
        // replaces, making the comparison depend on the developer's environment.
        const std::unique_ptr<QStyle> fusion(QStyleFactory::create(QStringLiteral("Fusion")));
        QVERIFY(fusion != nullptr);

        const QImage baseline = surfaceOf(*fusion);
        const QPoint edge(0, container->height() / 2);
        const QPoint interior(container->width() / 2, container->height() / 2);

        // The baseline really does draw an edge distinct from the surface behind it, so the
        // comparison below cannot pass by both styles painting nothing.
        QVERIFY(baseline.pixelColor(edge) != QColor(Qt::magenta));
        QVERIFY(baseline.pixelColor(edge) != baseline.pixelColor(interior));

        QCOMPARE(surfaceOf(style), baseline);
    }

    // Which of the two popup routes Qt takes is a decision of ours, not an accident of the base
    // style, and everything below depends on it: declining SH_ComboBox_Popup is what stops Qt
    // installing a QComboMenuDelegate and leaves the rows on the item-view path the DropdownList
    // tokens describe.
    void test_comboPopupIsNotRenderedAsAMenu()  // NOLINT
    {
        Gui::FreeCADStyle freecadStyle;
        QStyle& style = freecadStyle;
        QComboBox box;
        box.setStyle(&freecadStyle);
        populate(box);
        freecadStyle.polish(&box);

        QStyleOptionComboBox option;
        option.initFrom(&box);

        QCOMPARE(style.styleHint(QStyle::SH_ComboBox_Popup, &option, &box, nullptr), 0);

        // The consequence, not just the number: Qt picks the delegate from that hint, and the
        // menu one is what sized a row to the whole viewport.
        QVERIFY2(
            std::strcmp(box.view()->itemDelegate()->metaObject()->className(), "QComboMenuDelegate")
                != 0,
            box.view()->itemDelegate()->metaObject()->className()
        );
    }

    // A populated popup's rows are ordinary item-view rows, so their height is assembled from the
    // DropdownList item tokens: the label, the item padding around it, and the inter-row gap every
    // row reserves above itself. The menu route answered this question with the size
    // of the entire viewport, which is both wrong and impossible to tell from a plausible number
    // unless the tokens are pinned.
    void test_popupRowsAreItemViewRowsOfATokenDerivedHeight()  // NOLINT
    {
        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QComboBox box;
        populate(box);
        style.polish(&box);

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });

        QListView* view = popupOf(box);
        const int firstRow = view->visualRect(box.model()->index(0, 0)).height();
        const int secondRow = view->visualRect(box.model()->index(1, 0)).height();

        // The label, plus DropdownListItemPadding on both edges and the DropdownListItemSpacing
        // gap the row reserves above itself.
        QCOMPARE(firstRow, view->fontMetrics().height() + (2 * itemPaddingVertical) + itemSpacing);

        // Every row reserves that gap, the first included, so the pitch never varies.
        QCOMPARE(secondRow, firstRow);

        // Three such rows sit well inside the cap, so the popup is nowhere near being one row
        // tall — the shape the regression took.
        QVERIFY2(
            (box.count() * secondRow) < view->maximumHeight(),
            qPrintable(QStringLiteral("%1 rows of %2px do not fit comfortably in the %3px cap")
                           .arg(box.count())
                           .arg(secondRow)
                           .arg(view->maximumHeight()))
        );
    }

    // A separator row is a 1px rule, and the item-view popup route has to keep it one. Qt sizes
    // one as QSize(pm, pm) from PM_DefaultFrameWidth asked with the *combo box* as the widget,
    // never the popup view, so this pins the one thing that could make it grow: item-view
    // container padding reaching a QComboBox. The fixture gives Select a padding precisely so
    // there is something to leak — route that padding back through PM_DefaultFrameWidth, where it
    // lived before itemViewContentsRect() took it over as SE_ShapedFrameContents, without
    // excluding combo boxes, and this row becomes 5px.
    void test_separatorRowsAreOnePixel()  // NOLINT
    {
        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QComboBox box;
        populate(box);
        box.insertSeparator(1);
        style.polish(&box);

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });

        QCOMPARE(popupOf(box)->visualRect(box.model()->index(1, 0)).height(), 1);
    }

    // A separator row is sized by its own token, not by the row pitch and not by Qt's
    // PM_DefaultFrameWidth. Asserted through visualRect — what the view actually laid out —
    // because a pixelMetric() return value only proves that a token resolves, never that Qt
    // asked for that metric.
    void test_aSeparatorRowTakesItsHeightFromTheToken()  // NOLINT
    {
        const auto heightGuard = overrideToken("DropdownListSeparatorHeight", "9px");

        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QFrame container;
        QListView* view = buildAdoptedDropdown(container, style);
        markAsSeparator(*view, 1);
        view->doItemsLayout();

        QCOMPARE(view->visualRect(view->model()->index(1, 0)).height(), 9);
    }

    // The rows around a separator keep their own pitch: a separator that changed its neighbours'
    // height would mean the size hook is answering for rows it does not own.
    void test_aSeparatorDoesNotResizeTheRowsAroundIt()  // NOLINT
    {
        const auto heightGuard = overrideToken("DropdownListSeparatorHeight", "9px");

        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QFrame container;
        QListView* view = buildAdoptedDropdown(container, style);
        const int pitchBefore = view->visualRect(view->model()->index(0, 0)).height();

        markAsSeparator(*view, 1);
        view->doItemsLayout();

        QCOMPARE(view->visualRect(view->model()->index(0, 0)).height(), pitchBefore);
        QCOMPARE(view->visualRect(view->model()->index(2, 0)).height(), pitchBefore);
    }

    // The rule is painted, in the token's colour, inside the separator's band. Cyan because the
    // fixture already paints the popup edge #00ff00 and the hover fill #0000ff — either would
    // count as a hit with nothing drawn at all.
    void test_aSeparatorRowPaintsTheTokenRule()  // NOLINT
    {
        const auto heightGuard = overrideToken("DropdownListSeparatorHeight", "9px");
        const auto colourGuard = overrideToken("DropdownListSeparatorBorderColor", "#00ffff");
        const auto thicknessGuard = overrideToken("DropdownListSeparatorBorderThickness", "1px");

        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QFrame container;
        QListView* view = buildAdoptedDropdown(container, style);
        markAsSeparator(*view, 1);
        view->doItemsLayout();

        const QImage canvas = renderOf(*view->viewport());
        const QRect band = view->visualRect(view->model()->index(1, 0));

        QVERIFY2(
            pixelsOfColour(canvas, band, QColor(0x00, 0xff, 0xff)) > 0,
            "no rule was painted in the separator's band"
        );
        // And nowhere else: a rule leaking into a neighbour would mean the row rect is wrong.
        QCOMPARE(
            pixelsOfColour(canvas, view->visualRect(view->model()->index(0, 0)), QColor(0x00, 0xff, 0xff)),
            0
        );
    }

    // The topmost and bottommost row, within @p rect, at which @p canvas paints @p colour —
    // or nullopt if it never does. Used to find where a rule actually landed rather than
    // asserting against a computed pixel position.
    static std::optional<std::pair<int, int>> verticalSpanOfColour(
        const QImage& canvas,
        const QRect& rect,
        const QColor& colour
    )
    {
        std::optional<int> top;
        int bottom = 0;
        for (int y = rect.top(); y <= rect.bottom(); ++y) {
            for (int x = rect.left(); x <= rect.right(); ++x) {
                if (canvas.rect().contains(x, y) && canvas.pixelColor(x, y) == colour) {
                    if (!top) {
                        top = y;
                    }
                    bottom = y;
                    break;
                }
            }
        }
        if (!top) {
            return std::nullopt;
        }
        return std::make_pair(*top, bottom);
    }

    // A dropdown row carries its whole inter-row gap at its own top: the space above a
    // separator's rule is the band's top half alone, but the space below it is the band's
    // bottom half plus the next row's full leading gap. Centring the rule within its own band
    // therefore puts it high by roughly that gap. This is the symmetry invariant: with tokens
    // chosen so `height + itemSpacing - thickness` is even (an exact split exists), the rule
    // must land with equal background above and below it, measured between the two
    // neighbouring rows' actual content — not merely within the separator's own band.
    //
    // h=8, using the fixture's own DropdownListItemSpacing (g=3), t=1: h+g-t=10 is even, split
    // k=5 balances exactly (5 above, 4+3=... (8-5-1)+3=5 below). Deliberately not the value the
    // token is set to in production — this proves the placement rule generally, independent of
    // which specific numbers ship.
    void test_theSeparatorRuleIsCentredBetweenItsNeighbours()  // NOLINT
    {
        const auto heightGuard = overrideToken("DropdownListSeparatorHeight", "8px");
        const auto thicknessGuard = overrideToken("DropdownListSeparatorBorderThickness", "1px");
        const auto colourGuard = overrideToken("DropdownListSeparatorBorderColor", "#00ffff");

        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QFrame container;
        QListView* view = buildAdoptedDropdown(container, style);
        markAsSeparator(*view, 1);
        view->doItemsLayout();

        const QImage canvas = renderOf(*view->viewport());
        const QColor probe(0x00, 0xff, 0xff);
        const QRect band = view->visualRect(view->model()->index(1, 0));

        const auto rule = verticalSpanOfColour(canvas, band, probe);
        QVERIFY2(rule.has_value(), "no rule was painted in the separator's band");

        const int rowAboveContentBottom = view->visualRect(view->model()->index(0, 0)).bottom();
        const int rowBelowContentTop = view->visualRect(view->model()->index(2, 0)).top()
            + itemSpacing;

        const int gapAbove = rule->first - rowAboveContentBottom - 1;
        const int gapBelow = rowBelowContentTop - rule->second - 1;

        QCOMPARE(gapAbove, gapBelow);
    }

    // The regression pin for the reported bug, built from the numbers production actually ships
    // (DropdownListSeparatorHeight, the resolved DropdownListItemSpacing, SeparatorThickness):
    // this fixture cannot reach "FreeCAD Base.yaml" itself, so those three are restated here as
    // literals instead of left to the fixture's own (different) defaults; only the rule colour is
    // overridden beyond that, to make the rule findable in the render. This is the assertion the
    // user's screenshot would have failed: the previous placement (centred within the separator's
    // own band) put the rule a whole item spacing too high.
    void test_theSeparatorRuleIsNotBiasedTowardTheTopRow()  // NOLINT
    {
        const auto heightGuard = overrideToken("DropdownListSeparatorHeight", "5px");
        const auto spacingGuard = overrideToken("DropdownListItemSpacing", "2px");
        const auto thicknessGuard = overrideToken("DropdownListSeparatorBorderThickness", "1px");
        const auto colourGuard = overrideToken("DropdownListSeparatorBorderColor", "#00ffff");
        constexpr int shippedItemSpacing = 2;

        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QFrame container;
        QListView* view = buildAdoptedDropdown(container, style);
        markAsSeparator(*view, 1);
        view->doItemsLayout();

        const QImage canvas = renderOf(*view->viewport());
        const QColor probe(0x00, 0xff, 0xff);
        const QRect band = view->visualRect(view->model()->index(1, 0));

        const auto rule = verticalSpanOfColour(canvas, band, probe);
        QVERIFY2(rule.has_value(), "no rule was painted in the separator's band");

        const int rowAboveContentBottom = view->visualRect(view->model()->index(0, 0)).bottom();
        const int rowBelowContentTop = view->visualRect(view->model()->index(2, 0)).top()
            + shippedItemSpacing;

        const int gapAbove = rule->first - rowAboveContentBottom - 1;
        const int gapBelow = rowBelowContentTop - rule->second - 1;

        QVERIFY2(
            std::abs(gapAbove - gapBelow) <= 1,
            qPrintable(
                QStringLiteral("gap above (%1) vs below (%2) the rule").arg(gapAbove).arg(gapBelow)
            )
        );
    }

    // A rounded scroll area is clipped to its border radius so the compositor does not show the
    // widget's square corners. A combo popup's radius belongs to the container that paints its
    // edge, not to the list sitting inset inside it — masking the list would round a widget whose
    // corners are nowhere near the popup's, and leave the visible edge square.
    void test_aRoundedPopupDoesNotMaskItsList()  // NOLINT
    {
        const auto radiusGuard = overrideToken("DropdownListBorderRadius", "8px");

        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QComboBox box;
        populate(box);
        style.polish(&box);

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });

        QVERIFY2(
            popupOf(box)->mask().isEmpty(),
            qPrintable(QStringLiteral("the popup list was clipped to %1 rect(s)")
                           .arg(popupOf(box)->mask().rectCount()))
        );
    }

    // The dropdown tokens are live rather than merely present: changing the one the row padding
    // comes from moves the row. A token written against a component or element the item-view path
    // never asks for resolves to nothing in silence, and the row keeps whatever it had.
    void test_rowPaddingFollowsTheDropdownItemToken()  // NOLINT
    {
        const int before = firstRowHeightWithAFreshStyle();

        constexpr int taller = itemPaddingVertical + 6;
        const auto restore = overrideToken(
            "DropdownListItemPadding",
            "padding(horizontal: 7px, vertical: " + std::to_string(taller) + "px)"
        );

        QCOMPARE(firstRowHeightWithAFreshStyle() - before, 2 * (taller - itemPaddingVertical));
    }

    // The selected row's fill comes from DropdownListRowSelectedBackground — the Row element. A
    // menu states the same fill on its item, and an alias that copied that name across would
    // never resolve here: the item-view path paints the interaction layer from Row alone. Only a
    // rendered popup can tell "the token is defined" from "the token reached the paint".
    void test_selectedRowIsFilledFromTheDropdownRowToken()  // NOLINT
    {
        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QComboBox box;
        populate(box);
        style.polish(&box);

        // The chosen entry, which is what a dropdown's selection means — not the view's own
        // current index, which Qt moves to whatever the pointer is over.
        box.setCurrentIndex(1);

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });

        QListView* view = popupOf(box);

        // Qt makes the chosen entry the view's current row when the popup opens, and a dropdown
        // reads the view's current row as the cursor. Move it off, so the chosen row carries the
        // selection alone: Hovered outranks Selected, and the selection's own fill is the subject
        // here.
        view->setCurrentIndex(box.model()->index(0, 0));

        const QImage canvas = renderOf(*view);
        const QPoint selected = view->visualRect(box.model()->index(1, 0)).center();
        const QPoint resting = view->visualRect(box.model()->index(0, 0)).center();

        QCOMPARE(canvas.pixelColor(selected), QColor(0xff, 0x00, 0x00));
        QVERIFY(canvas.pixelColor(resting) != QColor(0xff, 0x00, 0x00));
    }

    void test_aHoveredSelectedRowStillGivesHoverFeedback()  // NOLINT
    {
        // A selection must keep reacting to the pointer: Hovered outranks Selected, so the row
        // that is both resolves the hovered background. With the order the other way round a
        // selected row was inert under the cursor, which is what the visual review rejected.
        installFreshApplicationStyle();

        QListView listView;

        QStyleOptionViewItem option;
        option.initFrom(&listView);
        option.rect = QRect(0, 0, 100, 20);
        option.state |= QStyle::State_Selected;
        option.state |= QStyle::State_MouseOver;

        QImage canvas(option.rect.size(), QImage::Format_ARGB32);
        canvas.fill(Qt::magenta);
        {
            QPainter painter(&canvas);
            QApplication::style()->drawControl(QStyle::CE_ItemViewItem, &option, &painter, &listView);
        }

        QCOMPARE(canvas.pixelColor(50, 10), QColor(0x00, 0x00, 0xff));
    }

    // With the popup off the menu route Qt no longer asks for PE_PanelMenu, so the popup's edge
    // is the container QFrame's own frame, drawn through PE_Frame from the DropdownList surface
    // tokens. That is the only thing standing between the popup and having no edge at all.
    void test_popupSurfaceIsDrawnFromTheDropdownSurfaceTokens()  // NOLINT
    {
        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QComboBox box;
        populate(box);
        style.polish(&box);

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });

        QWidget* container = popupOf(box)->parentWidget();
        const QImage canvas = renderOf(*container);

        // DropdownListBorderThickness is 1px, so the outermost ring is the border and the pixel
        // behind it the surface.
        const int middle = container->height() / 2;
        QCOMPARE(canvas.pixelColor(0, middle), QColor(0x00, 0xff, 0x00));
        QCOMPARE(canvas.pixelColor(1, middle), QColor(0x10, 0x10, 0x10));
    }

    // The property that actually broke: rows overlapped by 6px because the view's cached item
    // layout used a row height resolved before the view became a DropdownList, while every
    // fresh size-hint query returned the new one. No single function was wrong, so no test
    // that asks a function for a number could see it. This asserts the layout is
    // self-consistent instead.
    void test_popupRowsAbutWithoutOverlapping()  // NOLINT
    {
        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QComboBox box;
        populate(box);

        // The ordering the running application was measured in: constrainComboDropdown() calls
        // comboBox->view(), which creates the view, and only afterwards tags it as a dropdown.
        // Asking the untagged view for a row rectangle lays its items out at the plain List
        // pitch, which is the cache the tagging has to invalidate. Without this the view's first
        // layout would happen after the tag, and the defect would be out of reach of the test.
        box.view()->visualRect(box.model()->index(0, 0));

        style.polish(&box);

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });

        QListView* view = popupOf(box);
        QAbstractItemModel* model = box.model();

        for (int row = 1; row < model->rowCount(); ++row) {
            const QRect previous = view->visualRect(model->index(row - 1, 0));
            const QRect current = view->visualRect(model->index(row, 0));
            QCOMPARE(current.top(), previous.bottom() + 1);
        }

        const QRect last = view->visualRect(model->index(model->rowCount() - 1, 0));
        QVERIFY2(
            last.bottom() <= view->viewport()->rect().bottom(),
            qPrintable(QStringLiteral("last row ends at %1, viewport at %2")
                           .arg(last.bottom())
                           .arg(view->viewport()->rect().bottom()))
        );
    }

    // A theme reload drops the style's caches but nothing tells a view its rows changed size,
    // so its layout would keep the pre-reload pitch. Same defect as the tagging one, reached
    // by the route the owner actually uses while retuning tokens.
    void test_rowPitchFollowsAThemeReload()  // NOLINT
    {
        ReloadableStyle& style = installFreshApplicationStyle();
        QComboBox box;
        populate(box);
        style.polish(&box);

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });

        QListView* view = popupOf(box);
        QAbstractItemModel* model = box.model();
        const int pitchBefore = view->visualRect(model->index(1, 0)).top()
            - view->visualRect(model->index(0, 0)).top();

        const auto tokenGuard
            = overrideToken("DropdownListItemPadding", "padding(horizontal: 7px, vertical: 11px)");

        // qApp is the object Gui::Application sends the reload to, so it is the object the
        // filter is handed here.
        Gui::ThemeReloadEvent reloadEvent;
        style.eventFilter(qApp, &reloadEvent);
        QCoreApplication::processEvents();

        const int pitchAfter = view->visualRect(model->index(1, 0)).top()
            - view->visualRect(model->index(0, 0)).top();

        // The fixture states 5px vertical padding; the override states 11px, so each row grows
        // by twice the 6px difference.
        QCOMPARE(pitchAfter - pitchBefore, 12);
    }

    // The selected row lands on the combo box, which is how a menu-style popup behaves and what
    // Qt did before this branch declined SH_ComboBox_Popup.
    void test_placementOverCurrentPutsTheSelectedRowOnTheComboBox()  // NOLINT
    {
        const auto tokenGuard = overrideToken("DropdownListPlacement", "current");

        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QComboBox box;
        populate(box);
        box.setCurrentIndex(1);
        style.polish(&box);
        box.move(200, 200);
        box.show();

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });
        QCoreApplication::processEvents();  // the correction is deferred by a zero timer

        // Walk the real widget hierarchy to the row rather than adding up the container's margins:
        // the claim is about where the row ends up on screen, not about the arithmetic that put
        // it there.
        QListView* view = popupOf(box);
        const QPoint rowTopLeft = view->visualRect(box.model()->index(1, 0)).topLeft();
        const int rowTopGlobal = view->viewport()->mapToGlobal(rowTopLeft).y();

        QCOMPARE(rowTopGlobal, box.mapToGlobal(QPoint {}).y());
    }

    // The popup's top edge meets the combo box's bottom edge exactly — no 1px bite out of the
    // combo's own border, which is what Qt's uncorrected list placement produces.
    void test_placementBelowMeetsTheComboBoxEdge()  // NOLINT
    {
        const auto tokenGuard = overrideToken("DropdownListPlacement", "below");

        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QComboBox box;
        populate(box);
        style.polish(&box);
        box.move(200, 200);
        box.show();

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });
        QCoreApplication::processEvents();

        QWidget* container = popupOf(box)->parentWidget();
        const QPoint comboTopLeft = box.mapToGlobal(QPoint {});

        QCOMPARE(container->mapToGlobal(QPoint {}).y(), comboTopLeft.y() + box.height());
    }

    // The offset applies on top of whichever mode is in force, so a gap or an overlap can be
    // dialled in without a rebuild.
    void test_placementOffsetShiftsThePopup()  // NOLINT
    {
        const auto modeGuard = overrideToken("DropdownListPlacement", "below");
        const auto offsetGuard = overrideToken("DropdownListPlacementOffset", "6px");

        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QComboBox box;
        populate(box);
        style.polish(&box);
        box.move(200, 200);
        box.show();

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });
        QCoreApplication::processEvents();

        QWidget* container = popupOf(box)->parentWidget();
        QCOMPARE(
            container->mapToGlobal(QPoint {}).y(),
            box.mapToGlobal(QPoint {}).y() + box.height() + 6
        );
    }

    // No mode may push the popup off the screen: the clamp outranks the placement.
    void test_placementNeverLeavesTheScreen()  // NOLINT
    {
        const auto tokenGuard = overrideToken("DropdownListPlacement", "current");

        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QComboBox box;
        populate(box);
        box.setCurrentIndex(2);
        style.polish(&box);

        const QRect available = QGuiApplication::primaryScreen()->availableGeometry();
        box.move(available.left() + 40, available.bottom() - box.sizeHint().height());
        box.show();

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });
        QCoreApplication::processEvents();

        QWidget* container = popupOf(box)->parentWidget();
        const int top = container->mapToGlobal(QPoint {}).y();
        QVERIFY(top >= available.top());
        QVERIFY(top + container->height() <= available.bottom() + 1);
    }

    // The other edge, and the one an uncapped dropdown reaches. OverCurrent subtracts the current
    // row's offset from the combo box's position, so a long list scrolled to a late row asks for a
    // top well above the desktop — and, the list being taller than the space below the combo box,
    // the lower clamp has nothing to give back. Only the upper clamp stops the popup there, and
    // without it the current row itself is what ends up off screen.
    void test_placementClampsALongPopupToTheTopOfTheScreen()  // NOLINT
    {
        const auto modeGuard = overrideToken("DropdownListPlacement", "current");
        // The shared cap would keep the popup short enough to place honestly; the workbench
        // selector clears it the same way, which is what makes this shape a shipped one.
        const auto capGuard = overrideToken("DropdownListMaxHeight", "reset()");

        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        const QRect available = QGuiApplication::primaryScreen()->availableGeometry();

        QComboBox box;
        // More rows than the desktop can show, with maxVisibleItems raised past them so nothing
        // but the (cleared) cap and Qt's own screen fit bound the popup.
        const int rowCount = available.height();
        box.setMaxVisibleItems(rowCount);
        for (int row = 0; row < rowCount; ++row) {
            box.addItem(QStringLiteral("Item %1").arg(row));
        }
        box.setCurrentIndex(rowCount - 1);
        style.polish(&box);

        box.move(available.left() + 40, available.top());
        box.show();

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });
        QCoreApplication::processEvents();

        QListView* view = popupOf(box);
        QWidget* container = view->parentWidget();
        const int rowOffsetInPopup
            = view->viewport()->mapTo(container, view->visualRect(view->currentIndex()).topLeft()).y();

        // The precondition the clamp exists for, stated rather than assumed: aligning the current
        // row with the combo box would have put the popup's top above the desktop.
        QVERIFY2(
            box.mapToGlobal(QPoint {}).y() - rowOffsetInPopup < available.top(),
            qPrintable(QStringLiteral(
                           "the current row sits %1px into the popup and the combo box %2px "
                           "below the top of the desktop, so the placement stays on screen "
                           "unaided and the clamp under test is never reached"
            )
                           .arg(rowOffsetInPopup)
                           .arg(box.mapToGlobal(QPoint {}).y() - available.top()))
        );

        const int top = container->mapToGlobal(QPoint {}).y();
        QVERIFY2(
            top >= available.top(),
            qPrintable(QStringLiteral(
                           "the popup starts %1px above the desktop, so its first rows "
                           "— the current one among them — are off screen"
            )
                           .arg(available.top() - top))
        );
    }

    // With a uniform pitch the trim has nothing left to do on an unscrolled capped popup — the
    // 3px residue that survived the old model is gone. This is the payoff the uniform gap was
    // for.
    void test_anUnscrolledCappedPopupHasNoResidue()  // NOLINT
    {
        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QComboBox box;
        populateBeyondTheCap(box);
        style.polish(&box);

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });
        QCoreApplication::processEvents();  // the correction is deferred by a zero timer

        QListView* view = popupOf(box);
        const int rowHeight = view->sizeHintForRow(0);
        QVERIFY(rowHeight > 0);
        QCOMPARE(view->viewport()->height() % rowHeight, 0);
    }

    // A capped popup scrolls per item, so it only ever shows whole rows: whatever the viewport
    // has left over after the last of them is empty surface, and the owner sees it as a band of
    // background under the bottom row. Measured scrolled, where every visible row carries the
    // leading DropdownListItemSpacing and the pitch is uniform.
    void test_aScrolledCappedPopupEndsOnARowEdge()  // NOLINT
    {
        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QComboBox box;
        populateBeyondTheCap(box);
        style.polish(&box);
        box.move(200, 200);
        box.show();

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });
        QCoreApplication::processEvents();  // the correction is deferred by a zero timer

        QListView* view = popupOf(box);
        QScrollBar* verticalBar = view->verticalScrollBar();

        // The mechanism the band comes from, pinned rather than assumed: per-pixel scrolling
        // shows a partial row instead of leaving the remainder blank, and nothing below applies.
        QCOMPARE(view->verticalScrollMode(), QAbstractItemView::ScrollPerItem);

        // The precondition: a popup that shows every row it has cannot leave a remainder, so it
        // would pass the assertions below without the correction under test ever running.
        QVERIFY2(
            verticalBar->maximum() > 0,
            "the popup shows every row, so it never scrolls and has no remainder"
        );

        verticalBar->setValue(verticalBar->maximum());

        const int rowHeight = view->sizeHintForRow(1);
        QVERIFY(rowHeight > 0);
        QCOMPARE(view->viewport()->height() % rowHeight, 0);

        // The visible claim, not just the arithmetic: the bottom row meets the bottom of the
        // viewport, so there is no surface between the two.
        const QRect lastRowRect = view->visualRect(box.model()->index(box.count() - 1, 0));
        QCOMPARE(lastRowRect.bottom(), view->viewport()->rect().bottom());
    }

    // The trim changes the container's height, and the screen clamp is computed from that height,
    // so the trim has to run first. A capped popup at the bottom of the screen is the shape that
    // tells the two orders apart: the clamp seats the popup's bottom edge on the screen edge, and
    // a trim applied after it lifts the popup clear of that edge by the remainder it removed.
    void test_theTrimPrecedesTheScreenClamp()  // NOLINT
    {
        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        const QRect available = QGuiApplication::primaryScreen()->availableGeometry();

        QComboBox box;
        populateBeyondTheCap(box);
        style.polish(&box);
        box.move(available.left() + 40, available.bottom() - box.sizeHint().height());
        box.show();

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });
        QCoreApplication::processEvents();  // the correction is deferred by a zero timer

        QWidget* container = popupOf(box)->parentWidget();

        // Two preconditions, because either one missing would let both orders pass.
        //
        // A popup that was not trimmed has the same height before and after the clamp, so the
        // order cannot matter to it.
        QVERIFY2(
            container->height() < container->maximumHeight(),
            qPrintable(QStringLiteral("the popup was not trimmed: %1px against a %2px cap")
                           .arg(container->height())
                           .arg(container->maximumHeight()))
        );

        // And a popup that fits below its combo box unaided never reaches the clamp at all.
        QVERIFY2(
            box.mapToGlobal(QPoint {}).y() + container->height() - 1 > available.bottom(),
            "the popup fits below the combo box, so the clamp under test is never reached"
        );

        const int containerBottom = container->mapToGlobal(QPoint {}).y() + container->height() - 1;
        QVERIFY2(
            containerBottom == available.bottom(),
            qPrintable(QStringLiteral(
                           "the popup ends %1px short of the bottom of the screen: it "
                           "was clamped against a height it no longer has"
            )
                           .arg(available.bottom() - containerBottom))
        );
    }

    // A capped popup gives back the surface its last row does not fill, so the viewport ends on a
    // row boundary. The pitch is not a safe way to work that out: a separator is 1px where a row
    // is many, so a modulo of the first row's height trims by the wrong amount and leaves the
    // last row cut off.
    void test_aCappedPopupWithASeparatorStillEndsOnARowBoundary()  // NOLINT
    {
        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QComboBox box;
        populateBeyondTheCap(box);
        box.insertSeparator(1);
        style.polish(&box);

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });
        QCoreApplication::processEvents();  // the placement correction is deferred by a zero timer

        QListView* view = popupOf(box);
        QVERIFY2(
            view->verticalScrollBar()->maximum() > 0,
            "the popup was not capped, so there was nothing to trim"
        );

        const int bottom = view->viewport()->height() - 1;
        const QModelIndex last = view->indexAt({0, bottom});
        QVERIFY(last.isValid());
        QCOMPARE(view->visualRect(last).bottom(), bottom);
    }

    // Qt sizes the container afresh on every showPopup(), so the trim starts from the cap each
    // time. A trim that compounded instead would cost the popup a row per open.
    void test_reopeningACappedPopupDoesNotTrimItFurther()  // NOLINT
    {
        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QComboBox box;
        populateBeyondTheCap(box);
        style.polish(&box);
        box.move(200, 200);
        box.show();

        const auto heightAfterOpening = [&box] {
            box.showPopup();
            QCoreApplication::processEvents();
            const int height = popupOf(box)->parentWidget()->height();
            box.hidePopup();
            return height;
        };

        const int firstHeight = heightAfterOpening();
        QCOMPARE(heightAfterOpening(), firstHeight);
        QCOMPARE(heightAfterOpening(), firstHeight);
    }

    // The snap applies only where there is a remainder to give back. An uncapped popup is
    // already exactly as tall as its content, so there is nothing below the last row to reclaim
    // and the popup must come out of the correction at the height Qt sized it to.
    void test_anUncappedPopupIsNotShrunk()  // NOLINT
    {
        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QComboBox box;
        populate(box);
        style.polish(&box);
        box.move(200, 200);
        box.show();

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });

        // Qt has already sized the popup by the time showPopup() returns; the correction is what
        // has not run yet.
        QListView* view = popupOf(box);
        const int viewportHeightBeforeCorrection = view->viewport()->height();

        QCoreApplication::processEvents();

        QCOMPARE(view->viewport()->height(), viewportHeightBeforeCorrection);

        const QRect lastRowRect = view->visualRect(box.model()->index(box.count() - 1, 0));
        QVERIFY2(
            lastRowRect.bottom() <= view->viewport()->rect().bottom(),
            qPrintable(QStringLiteral("the last row ends %1px past the viewport")
                           .arg(lastRowRect.bottom() - view->viewport()->rect().bottom()))
        );
    }

    // The uniform pitch the trim relies on covers only the rows the style sizes. Qt sizes a
    // separator inside QComboBoxDelegate::sizeHint() as QSize(pm, pm) from PM_DefaultFrameWidth,
    // which never reaches CT_ItemViewItem and so is nothing like a row's pitch. A popup holding
    // one is therefore not a whole number of pitches tall, and a trim taken against that pitch
    // would shave a popup that already shows everything it has — clipping its last row and
    // raising a scroll bar on a popup that fits.
    void test_aPopupWithASeparatorIsNotShrunk()  // NOLINT
    {
        Gui::FreeCADStyle& style = installFreshApplicationStyle();
        QComboBox box;
        populate(box);
        box.insertSeparator(1);
        style.polish(&box);
        box.move(200, 200);
        box.show();

        box.showPopup();
        const auto guard = qScopeGuard([&box] { box.hidePopup(); });

        QListView* view = popupOf(box);
        const int viewportHeightBeforeCorrection = view->viewport()->height();

        QCoreApplication::processEvents();  // the correction is deferred by a zero timer

        // The precondition: the separator keeps the popup from dividing evenly into row pitches,
        // so a trim taken against that pitch would clip a row instead of removing empty surface
        // — which is exactly what the guard under test exists to skip.
        QVERIFY2(
            viewportHeightBeforeCorrection % view->sizeHintForRow(0) != 0,
            "the popup is already a whole number of row pitches tall, so a trim against that "
            "pitch could not shrink it and the guard under test is never reached"
        );

        const QRect lastRowRect = view->visualRect(box.model()->index(box.count() - 1, 0));
        QVERIFY2(
            lastRowRect.bottom() <= view->viewport()->rect().bottom(),
            qPrintable(QStringLiteral("the last row ends %1px past the viewport")
                           .arg(lastRowRect.bottom() - view->viewport()->rect().bottom()))
        );

        QVERIFY2(
            !view->verticalScrollBar()->isVisible(),
            "a scroll bar appeared on a popup that shows every row it has"
        );

        QCOMPARE(view->viewport()->height(), viewportHeightBeforeCorrection);
    }

    // itemViewContentsRect() insets an ordinary item view by border and padding on every side,
    // the same claim comboPopupContentsRect() makes for a popup's container. ListItemSpacing is
    // pinned to 0px so the top inset carries only that border-plus-padding claim, not also the
    // leading-gap deduction every row's own top now carries — otherwise the assertion below would
    // be measuring two things at once.
    void test_itemViewContentsRectInsetsByBorderAndPadding()  // NOLINT
    {
        const auto paddingGuard = overrideToken("ListPadding", "padding(4px)");
        const auto borderGuard = overrideToken("ListBorderThickness", "2px");
        const auto spacingGuard = overrideToken("ListItemSpacing", "0px");

        Gui::FreeCADStyle freecadStyle;
        // subElementRect() is exposed publicly on QStyle only; FreeCADStyle narrows its override.
        QStyle& style = freecadStyle;
        QListView listView;
        style.polish(&listView);

        QStyleOptionFrame option;
        option.initFrom(&listView);
        option.rect = QRect(0, 0, 100, 100);

        const QRect contents = style.subElementRect(QStyle::SE_ShapedFrameContents, &option, &listView);

        QCOMPARE(contents, QRect(6, 6, 88, 88));
    }

    void test_theChosenEntryStaysMarkedWhileTheCursorIsElsewhere()  // NOLINT
    {
        // Qt moves the popup's current index to whatever the pointer is over, so the view's
        // selection is a cursor, not a selection. The chosen entry is the combo's currentIndex.
        installFreshApplicationStyle();

        QComboBox comboBox;
        comboBox.addItems({"first", "second", "third", "fourth"});
        comboBox.setCurrentIndex(2);
        comboBox.show();
        QVERIFY(QTest::qWaitForWindowExposed(&comboBox));

        comboBox.showPopup();
        QCoreApplication::processEvents();  // the placement correction is deferred by a zero timer

        QListView* view = qobject_cast<QListView*>(comboBox.view());
        QVERIFY(view != nullptr);

        // What a mouse-move over the first row does, without needing a synthetic mouse event.
        view->setCurrentIndex(view->model()->index(0, 0));

        const QImage canvas = renderOf(*view->viewport());

        const QRect chosenRow = view->visualRect(view->model()->index(2, 0));
        const QRect cursorRow = view->visualRect(view->model()->index(0, 0));

        QCOMPARE(canvas.pixelColor(chosenRow.center()), QColor(0xff, 0x00, 0x00));
        QCOMPARE(canvas.pixelColor(cursorRow.center()), QColor(0x00, 0x00, 0xff));
    }

    // The moment the feature exists for: opening the dropdown to see what is currently set,
    // without the pointer ever going near it. Qt makes the chosen entry the view's current row
    // as the popup opens, so that row arrives carrying State_Selected — the flag a dropdown
    // otherwise reads as the cursor. Folding it here would paint the chosen entry as merely
    // hovered and the selection would never be visible on a freshly opened popup.
    void test_aKeyboardOpenedPopupShowsTheChosenEntryAsChosen()  // NOLINT
    {
        installFreshApplicationStyle();

        QComboBox comboBox;
        comboBox.addItems({"first", "second", "third", "fourth"});
        comboBox.setCurrentIndex(2);
        comboBox.show();
        QVERIFY(QTest::qWaitForWindowExposed(&comboBox));

        comboBox.showPopup();
        QCoreApplication::processEvents();  // the placement correction is deferred by a zero timer

        QListView* view = qobject_cast<QListView*>(comboBox.view());
        QVERIFY(view != nullptr);

        // The precondition the exemption exists for, stated rather than assumed: without Qt
        // making the chosen entry current on open there would be no State_Selected on it and
        // the fold could not reach it.
        QCOMPARE(view->currentIndex().row(), 2);

        const QImage canvas = renderOf(*view->viewport());
        const QRect chosenRow = view->visualRect(view->model()->index(2, 0));

        QCOMPARE(canvas.pixelColor(chosenRow.center()), QColor(0xff, 0x00, 0x00));
    }

    // The exemption must not cost the chosen entry its pointer feedback. State_MouseOver is
    // mapped to Hovered before the dropdown's own selection handling runs, so a chosen row
    // under the cursor resolves the hovered fill — hover outranking selection, as the visual
    // review asked for.
    void test_theChosenEntryStillReactsToTheCursor()  // NOLINT
    {
        installFreshApplicationStyle();

        QComboBox comboBox;
        comboBox.addItems({"first", "second", "third", "fourth"});
        comboBox.setCurrentIndex(2);
        comboBox.show();
        QVERIFY(QTest::qWaitForWindowExposed(&comboBox));

        comboBox.showPopup();
        QCoreApplication::processEvents();

        QListView* view = qobject_cast<QListView*>(comboBox.view());
        QVERIFY(view != nullptr);

        // What the pointer arriving over the chosen row does: QAbstractItemView tracks the row
        // under it and hands the painter State_MouseOver for that row alone.
        //
        // The move is delivered to the viewport directly rather than through QTest::mouseMove,
        // which for a widget with no button held warps the real desktop pointer via
        // QCursor::setPos: on a live display that produces no move at all when the cursor
        // already sits at that global position, and the row is then never hovered. Only the
        // offscreen platform makes it reliable, so do not simplify this back.
        const QRect chosenRow = view->visualRect(view->model()->index(2, 0));
        const QPointF cursorSpot = chosenRow.center();
        QMouseEvent cursorArrival(
            QEvent::MouseMove,
            cursorSpot,
            view->viewport()->mapToGlobal(cursorSpot),
            Qt::NoButton,
            Qt::NoButton,
            Qt::NoModifier
        );
        QCoreApplication::sendEvent(view->viewport(), &cursorArrival);
        QCoreApplication::processEvents();

        const QImage canvas = renderOf(*view->viewport());

        QCOMPARE(canvas.pixelColor(chosenRow.center()), QColor(0x00, 0x00, 0xff));
    }

    // DropdownListItemHoveredTextColor is what stops a hovered row's label going white.
    // QCommonStyle paints an item's label in QPalette::HighlightedText whenever State_Selected
    // is set, and on a dropdown that flag marks the cursor rather than the selection — so the
    // label of the row under the pointer takes the highlight colour over a mere hover tint. The
    // Item TextColor patched into that palette role is the only thing holding it back, and a
    // token that resolves to nothing leaves the patch unapplied and the label near-white.
    //
    // Only a rendered popup can tell that apart from "the token is defined": the name has to
    // reach the paint, and a name misspelt consistently everywhere it is written resolves to
    // nothing while every by-name test still passes.
    void test_aHoveredRowTakesItsLabelColourFromTheDropdownItemToken()  // NOLINT
    {
        const auto textGuard = overrideToken("DropdownListItemHoveredTextColor", "#00ff00");

        installFreshApplicationStyle();

        QComboBox comboBox;
        comboBox.addItems({"first", "second", "third", "fourth"});
        comboBox.setCurrentIndex(2);
        comboBox.show();
        QVERIFY(QTest::qWaitForWindowExposed(&comboBox));

        comboBox.showPopup();
        QCoreApplication::processEvents();  // the placement correction is deferred by a zero timer

        QListView* view = qobject_cast<QListView*>(comboBox.view());
        QVERIFY(view != nullptr);

        // The cursor on a row that is not the chosen entry, so the hovered label and the chosen
        // one are separable. Qt marks the current row State_Selected, which is exactly the flag
        // that sends QCommonStyle to HighlightedText.
        view->setCurrentIndex(view->model()->index(0, 0));

        const QImage canvas = renderOf(*view->viewport());
        const QRect hoveredRow = view->visualRect(view->model()->index(0, 0));
        const QRect chosenRow = view->visualRect(view->model()->index(2, 0));

        const QColor labelColour(0x00, 0xff, 0x00);
        const int hoveredLabel = pixelsOfColour(canvas, hoveredRow, labelColour);
        const int chosenLabel = pixelsOfColour(canvas, chosenRow, labelColour);

        QVERIFY2(
            hoveredLabel > 0,
            "the hovered row's label was not drawn in the token's colour, so the palette patch "
            "never ran and Qt is free to paint it in the raw highlight text colour"
        );

        // The token is the hovered row's alone: a chosen row that is not under the cursor keeps
        // the ordinary label colour, so the assertion above cannot be satisfied by the colour
        // leaking onto every row.
        QCOMPARE(chosenLabel, 0);
    }

    void test_aComboWithNoCurrentIndexMarksNothing()  // NOLINT
    {
        installFreshApplicationStyle();

        QComboBox comboBox;
        comboBox.setEditable(true);
        comboBox.addItems({"first", "second", "third"});
        comboBox.setCurrentIndex(-1);
        comboBox.show();
        QVERIFY(QTest::qWaitForWindowExposed(&comboBox));

        comboBox.showPopup();
        QCoreApplication::processEvents();

        QListView* view = qobject_cast<QListView*>(comboBox.view());
        QVERIFY(view != nullptr);

        // What a mouse-move over the first row does, without needing a synthetic mouse event.
        view->setCurrentIndex(view->model()->index(0, 0));

        const QImage canvas = renderOf(*view->viewport());

        // Nothing is chosen, so nothing carries the selected background.
        for (int row = 0; row < 3; ++row) {
            const QRect rowRect = view->visualRect(view->model()->index(row, 0));
            QVERIFY2(
                canvas.pixelColor(rowRect.center()) != QColor(0xff, 0x00, 0x00),
                qPrintable(QStringLiteral("row %1 is marked as chosen").arg(row))
            );
        }

        // And the cursor row still reads as a cursor: a combo box holding nothing answers an
        // engaged -1, so its popup keeps folding State_Selected into hover rather than falling
        // back on the plain item-view meaning.
        QCOMPARE(
            canvas.pixelColor(view->visualRect(view->model()->index(0, 0)).center()),
            QColor(0x00, 0x00, 0xff)
        );
    }

    // Qt sizes a popup and only then shows it, and it is the show that polishes a widget for the
    // first time — so a view recognised as a dropdown no earlier than its own polish is measured
    // at the plain List pitch and painted at the dropdown's. The popup opens too short, scrolled,
    // and comes right only on the second open. Recognising the view when setView() installs it,
    // which happens well before anything measures the popup, is what makes the first open right.
    void test_aViewInstalledAfterPolishIsSizedOnItsFirstOpen()  // NOLINT
    {
        installFreshApplicationStyle();

        QComboBox comboBox;
        comboBox.addItems({"first", "second", "third", "fourth"});
        comboBox.move(200, 200);
        comboBox.show();
        QVERIFY(QTest::qWaitForWindowExposed(&comboBox));

        // Built and handed over the way DlgExpressionInput's variable set combo does it, from a
        // runtime slot long after the combo box was polished.
        auto* replacementView = new QListView(&comboBox);
        comboBox.setView(replacementView);
        QCOMPARE(comboBox.view(), replacementView);

        // The first open, and the only one.
        comboBox.showPopup();
        const auto guard = qScopeGuard([&comboBox] { comboBox.hidePopup(); });
        QCoreApplication::processEvents();  // the placement correction is deferred by a zero timer

        QWidget* container = replacementView->parentWidget();

        // The precondition: four short rows at the dropdown's pitch sit well inside the shared
        // cap, so nothing but a mis-measurement could make this popup scroll.
        QVERIFY2(
            baselineContainerHeight(*replacementView, comboBox.count())
                < replacementView->maximumHeight(),
            "the four rows do not fit the cap, so a scrolled popup would be honest"
        );

        QVERIFY2(
            container->height() == baselineContainerHeight(*replacementView, comboBox.count()),
            qPrintable(QStringLiteral(
                           "the popup is %1px tall, against the %2px its four rows at "
                           "the dropdown pitch need"
            )
                           .arg(container->height())
                           .arg(baselineContainerHeight(*replacementView, comboBox.count())))
        );

        // The consequence the owner sees, not only the arithmetic: every row is on screen.
        const QRect lastRowRect = replacementView->visualRect(
            comboBox.model()->index(comboBox.count() - 1, 0)
        );
        QVERIFY2(
            lastRowRect.bottom() <= replacementView->viewport()->rect().bottom(),
            qPrintable(QStringLiteral(
                           "the last row spans y %1..%2, past the viewport's %3, so the "
                           "popup opened showing only part of its rows"
            )
                           .arg(lastRowRect.top())
                           .arg(lastRowRect.bottom())
                           .arg(replacementView->viewport()->rect().bottom()))
        );

        QVERIFY2(
            !replacementView->verticalScrollBar()->isVisible(),
            "a scroll bar appeared on a popup that has room for every row it holds"
        );
    }

    // A view handed to a combo box after it was polished — what DlgExpressionInput's variable
    // set combo does from a runtime slot — is that combo's popup like any other and has to be
    // recognised as one. Nothing polishes the combo box again, so the recognition can only come
    // from the view's own polish, which Qt runs when the popup is first shown.
    //
    // The cursor is parked away from the chosen entry on purpose: that is what separates the
    // dropdown's derived selection from the generic item-view one. An untagged view resolves as
    // a plain List, where the mark follows the view's own current row and would sit on the
    // cursor's row instead — so this cannot pass on the fixture's identically coloured ListRow
    // token.
    //
    // One open is enough: the view is recognised when setView() installs it, so the popup is laid
    // out from the metrics it paints with from the first open. This test used to open the popup
    // twice for that reason; test_aViewInstalledAfterPolishIsSizedOnItsFirstOpen above is what
    // holds the single open to being sufficient.
    void test_aViewInstalledAfterPolishStillMarksTheChosenEntry()  // NOLINT
    {
        installFreshApplicationStyle();

        QComboBox comboBox;
        comboBox.addItems({"first", "second", "third", "fourth"});
        comboBox.setCurrentIndex(2);
        comboBox.show();
        QVERIFY(QTest::qWaitForWindowExposed(&comboBox));

        // Given a parent of its own before it is handed over, the way DlgExpressionInput builds
        // one, and the combo box itself at that — a child created under an already visible
        // widget stays hidden, so the view reaches setView() unpolished no matter whose child
        // it was. A view polished while it was merely inside a combo box would be a view the
        // popup's own show can no longer reach.
        auto* replacementView = new QListView(&comboBox);
        comboBox.setView(replacementView);
        QCOMPARE(comboBox.view(), replacementView);

        comboBox.showPopup();
        QCoreApplication::processEvents();  // the placement correction is deferred by a zero timer

        // What a mouse-move over the first row does, without needing a synthetic mouse event.
        replacementView->setCurrentIndex(replacementView->model()->index(0, 0));

        const QImage canvas = renderOf(*replacementView->viewport());

        const QRect chosenRow = replacementView->visualRect(replacementView->model()->index(2, 0));
        const QRect cursorRow = replacementView->visualRect(replacementView->model()->index(0, 0));

        QCOMPARE(canvas.pixelColor(chosenRow.center()), QColor(0xff, 0x00, 0x00));
        QCOMPARE(canvas.pixelColor(cursorRow.center()), QColor(0x00, 0x00, 0xff));
    }

    // A dropdown is not necessarily a combo box's. Adopting a bare view gives it and the frame
    // around it the same surface, metrics and cap a combo popup gets — the whole reason the
    // entry point is public.
    void test_aBareViewCanBeAdoptedAsADropdown()  // NOLINT
    {
        const auto capGuard = overrideToken("DropdownListMaxHeight", "80px");

        Gui::FreeCADStyle& style = installFreshApplicationStyle();

        QFrame container;
        container.setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
        auto* view = new QListView(&container);
        auto* model = new QStandardItemModel(&container);
        for (int row = 0; row < 40; ++row) {
            model->appendRow(new QStandardItem(QStringLiteral("Item %1").arg(row)));
        }
        view->setModel(model);

        style.constrainDropdown(view);

        // The cap reaches both halves of the popup: the list, and the frame that paints its edge.
        QCOMPARE(view->maximumHeight(), 80);
        QCOMPARE(container.maximumHeight(), 80);

        // The frame resolves the dropdown surface rather than a bare widget's, which is what
        // gives it the popup's border and padding. Sampled at the container's bottom edge, past
        // where the view sits: the view is never laid out here, so it keeps its own default
        // 100x30 at the origin and paints its own dropdown edge over (0, 0) whether the
        // container was tagged or not.
        container.resize(200, 80);
        const QImage canvas = renderOf(container);
        QCOMPARE(canvas.pixelColor(0, container.height() - 1), surfaceBorderColor);
    }

    // A row's icon size is the theme's to state, so it has to come from the token rather than
    // from whatever the base style happens to hardcode.
    //
    // Asserted on the option the view builds, not on a pixelMetric() return value: a list view
    // sizes its decoration from PM_ListViewIconSize, never PM_SmallIconSize, so a style that
    // answers only the latter reports the token correctly and still hands the delegate Fusion's
    // hardcoded 24. Reading the metric back would pass in exactly that broken state. Capturing
    // the delegate's option goes through QListView::initViewItemOption, which is the code that
    // actually chooses the number the rows are drawn with.
    void test_aDropdownRowIconSizeComesFromTheToken()  // NOLINT
    {
        struct DecorationProbe: QStyledItemDelegate
        {
            mutable QSize decorationSize;

            QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
            {
                decorationSize = option.decorationSize;
                return QStyledItemDelegate::sizeHint(option, index);
            }
        };
        DecorationProbe probe;

        const auto iconGuard = overrideToken("DropdownListIconSize", "13px");

        Gui::FreeCADStyle& style = installFreshApplicationStyle();

        QFrame container;
        container.setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
        auto* view = new QListView(&container);
        auto* model = new QStandardItemModel(&container);
        model->appendRow(new QStandardItem(QStringLiteral("only")));
        view->setModel(model);
        view->setItemDelegate(&probe);

        style.constrainDropdown(view);

        view->sizeHintForIndex(model->index(0, 0));

        QCOMPARE(probe.decorationSize, QSize(13, 13));
    }

    // A dropdown holds one row pitch whether or not a given entry carries an icon. Every row
    // reserves the icon's height, the way a menu reserves it for every item once any item has
    // one (menuItemSizeFromContents), so a list mixing captioned and plain entries does not step
    // between two heights.
    void test_aRowWithoutAnIconIsAsTallAsOneWithIt()  // NOLINT
    {
        constexpr int iconExtent = 30;  // well clear of the text height, so the two cannot tie
        const auto iconGuard = overrideToken("DropdownListIconSize", std::to_string(iconExtent) + "px");

        Gui::FreeCADStyle& style = installFreshApplicationStyle();

        QPixmap swatch(iconExtent, iconExtent);
        swatch.fill(Qt::red);

        QFrame container;
        container.setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
        auto* view = new QListView(&container);
        auto* model = new QStandardItemModel(&container);
        auto* captioned = new QStandardItem(QStringLiteral("with icon"));
        captioned->setIcon(QIcon(swatch));
        model->appendRow(captioned);
        model->appendRow(new QStandardItem(QStringLiteral("without icon")));
        view->setModel(model);

        style.constrainDropdown(view);

        const int captionedRow = view->sizeHintForIndex(model->index(0, 0)).height();
        const int plainRow = view->sizeHintForIndex(model->index(1, 0)).height();

        QCOMPARE(plainRow, captionedRow);
        // The two met by raising the plain row, not by cropping the icon out of its own row.
        QVERIFY(captionedRow >= iconExtent);
    }

    // The chosen row keeps its mark while the view's own selection moves. Without a combo box
    // the view's selection is still a cursor — the popup moves it under the pointer and under
    // the arrow keys — so the entry the control holds has to come from somewhere else.
    void test_anAdoptedViewMarksItsTaggedChosenRow()  // NOLINT
    {
        Gui::FreeCADStyle& style = installFreshApplicationStyle();

        QFrame container;
        container.setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
        auto* view = new QListView(&container);
        auto* model = new QStandardItemModel(&container);
        for (const QString& label :
             {QStringLiteral("first"),
              QStringLiteral("second"),
              QStringLiteral("third"),
              QStringLiteral("fourth")}) {
            model->appendRow(new QStandardItem(label));
        }
        view->setModel(model);

        style.constrainDropdown(view, /*chosenRow=*/2);

        container.resize(200, 200);
        view->resize(200, 200);
        container.show();
        QVERIFY(QTest::qWaitForWindowExposed(&container));

        // The cursor sits somewhere else entirely.
        view->setCurrentIndex(model->index(0, 0));

        const QImage canvas = renderOf(*view->viewport());

        QCOMPARE(
            canvas.pixelColor(view->visualRect(model->index(2, 0)).center()),
            QColor(0xff, 0x00, 0x00)
        );
        QCOMPARE(
            canvas.pixelColor(view->visualRect(model->index(0, 0)).center()),
            QColor(0x00, 0x00, 0xff)
        );
    }

    // A dropdown that holds nothing is still a dropdown: its selection is a cursor like any
    // other dropdown's, so the row it sits on reads as hovered and no row is marked as chosen.
    // -1 says "adopted, nothing chosen" — the same answer a combo box with no current index
    // gives — and not "this view was never adopted".
    void test_anAdoptedViewWithNoChosenRowTreatsItsSelectionAsACursor()  // NOLINT
    {
        Gui::FreeCADStyle& style = installFreshApplicationStyle();

        QFrame container;
        container.setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
        auto* view = new QListView(&container);
        auto* model = new QStandardItemModel(&container);
        for (const QString& label :
             {QStringLiteral("first"), QStringLiteral("second"), QStringLiteral("third")}) {
            model->appendRow(new QStandardItem(label));
        }
        view->setModel(model);

        style.constrainDropdown(view);  // no chosen row

        container.resize(200, 200);
        view->resize(200, 200);
        container.show();
        QVERIFY(QTest::qWaitForWindowExposed(&container));
        view->setCurrentIndex(model->index(1, 0));

        const QImage canvas = renderOf(*view->viewport());

        QCOMPARE(
            canvas.pixelColor(view->visualRect(model->index(1, 0)).center()),
            QColor(0x00, 0x00, 0xff)
        );
    }

    // A dropdown that names its own component puts its prefix ahead of the whole
    // DropdownList→List chain, so a Selected token there outranks the shared Hovered one. That is
    // what makes clearing the generic item-view rule's mark load-bearing rather than tidy: without
    // the clear, the cursor row keeps Selected and paints the chosen entry's colour.
    void test_aNamedDropdownDoesNotLeakTheChosenFillOntoTheCursor()  // NOLINT
    {
        const auto chosenFill = overrideToken("TaggedDropdownRowSelectedBackground", "#00ff00");

        Gui::FreeCADStyle& style = installFreshApplicationStyle();

        QFrame container;
        container.setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
        auto* view = new QListView(&container);
        auto* model = new QStandardItemModel(&container);
        for (const QString& label :
             {QStringLiteral("first"),
              QStringLiteral("second"),
              QStringLiteral("third"),
              QStringLiteral("fourth")}) {
            model->appendRow(new QStandardItem(label));
        }
        view->setModel(model);

        view->setProperty("component", "TaggedDropdown");
        style.constrainDropdown(view, /*chosenRow=*/2);

        container.resize(200, 200);
        view->resize(200, 200);
        container.show();
        QVERIFY(QTest::qWaitForWindowExposed(&container));

        // The cursor sits somewhere else entirely.
        view->setCurrentIndex(model->index(0, 0));

        const QImage canvas = renderOf(*view->viewport());

        // The cursor row is hovered and must never wear the named chosen fill.
        QCOMPARE(
            canvas.pixelColor(view->visualRect(model->index(0, 0)).center()),
            QColor(0x00, 0x00, 0xff)
        );
        // The chosen row wears it.
        QCOMPARE(
            canvas.pixelColor(view->visualRect(model->index(2, 0)).center()),
            QColor(0x00, 0xff, 0x00)
        );
    }

    // A dropdown nobody is touching is not pressed. Every scroll area takes QFrame::Sunken as
    // its default shadow, and QFrame::initStyleOption reports that as State_Sunken — the same
    // flag a button raises while it is held down. A view that reads the flag as a press
    // therefore wears its pressed tokens permanently, whole surface included, from the moment it
    // is built.
    void test_anUntouchedDropdownIsNotPressed()  // NOLINT
    {
        // Cyan: the fixture already paints the popup's edge in green, and a colour it uses
        // elsewhere would be counted here whether the state produced it or not.
        const auto surfacePressed = overrideToken("DropdownListPressedBackground", "#00ffff");

        Gui::FreeCADStyle& style = installFreshApplicationStyle();

        QFrame container;
        auto* view = buildAdoptedDropdown(container, style);
        QVERIFY(QTest::qWaitForWindowExposed(&container));

        // The whole view, so its frame and the surface behind the rows are both in the picture.
        const QImage canvas = renderOf(*view);

        QCOMPARE(pixelsOfColour(canvas, canvas.rect(), QColor(0x00, 0xff, 0xff)), 0);
    }

    // Nothing in Qt tells a row it is being pressed. State_Sunken — the flag every other
    // component's Pressed state is read from — never reaches a QStyleOptionViewItem: across
    // src/widgets/itemviews only QHeaderView sets it, and then on a section. So a dropdown row's
    // press has to be read off the pointer, and until it is, no DropdownListRowPressed* token is
    // reachable at all: the theme can define one and the popup will never resolve it.
    void test_aRowUnderAHeldButtonIsPressed()  // NOLINT
    {
        const auto pressedFill = overrideToken("DropdownListRowPressedBackground", "#00ff00");

        Gui::FreeCADStyle& style = installFreshApplicationStyle();

        QFrame container;
        auto* view = buildAdoptedDropdown(container, style);
        QVERIFY(QTest::qWaitForWindowExposed(&container));

        const QRect row = view->visualRect(view->model()->index(1, 0));
        const QPoint spot = view->viewport()->mapTo(&container, row.center());

        const auto buttonGuard = holdLeftButton(container, spot);

        const QImage canvas = renderOf(*view->viewport());

        QCOMPARE(canvas.pixelColor(row.center()), QColor(0x00, 0xff, 0x00));

        // The press belongs to the row under the pointer alone — a fill that leaked onto the
        // whole view would satisfy the assertion above just as well.
        const QRect neighbour = view->visualRect(view->model()->index(2, 0));
        QVERIFY(canvas.pixelColor(neighbour.center()) != QColor(0x00, 0xff, 0x00));
    }

    // The press belongs to a row, not to the popup. The view's own frame and the container
    // around it resolve as DropdownList too, and both report State_MouseOver whenever the
    // pointer is anywhere inside them — so a press read without asking what is being painted
    // would tint the whole surface along with the row under the pointer.
    void test_aPressedRowDoesNotPressThePopupAroundIt()  // NOLINT
    {
        // Cyan: the fixture already paints the popup's edge in green, and a colour it uses
        // elsewhere would be counted here whether the press produced it or not.
        const auto surfacePressed = overrideToken("DropdownListPressedBackground", "#00ffff");

        Gui::FreeCADStyle& style = installFreshApplicationStyle();

        QFrame container;
        auto* view = buildAdoptedDropdown(container, style);
        QVERIFY(QTest::qWaitForWindowExposed(&container));

        const QRect row = view->visualRect(view->model()->index(1, 0));
        const QPoint spot = view->viewport()->mapTo(&container, row.center());

        const auto buttonGuard = holdLeftButton(container, spot);

        // The whole view, so its frame and the surface behind the rows are both in the picture.
        const QImage canvas = renderOf(*view);

        QCOMPARE(pixelsOfColour(canvas, canvas.rect(), QColor(0x00, 0xff, 0xff)), 0);
    }

    // Pressed is additive to Hovered rather than a replacement for it: the fallback chain emits
    // every active state in priority order, so a pressed row still resolves the hovered fill and
    // a PressedBackgroundEffect has something to act on. That is the shape the shipped theme
    // uses — it states an effect for the press and no background of its own — and it is what
    // makes a press read as the hover deepening rather than as an unrelated colour.
    void test_aPressedRowDeepensTheHoverFillRatherThanReplacingIt()  // NOLINT
    {
        // An effect large enough that the result cannot be confused with the hover fill it was
        // derived from, yet short of the clamp at black — a press that painted nothing would
        // otherwise be hard to tell from one that darkened all the way. No
        // DropdownListRowPressedBackground at all, so a press resolving a background of its own
        // rather than the hovered one has nothing to paint.
        const auto effectGuard
            = overrideToken("DropdownListRowPressedBackgroundEffect", "effect(darken: 0.2)");

        Gui::FreeCADStyle& style = installFreshApplicationStyle();

        QFrame container;
        auto* view = buildAdoptedDropdown(container, style);
        QVERIFY(QTest::qWaitForWindowExposed(&container));

        const QRect row = view->visualRect(view->model()->index(1, 0));
        const QPoint spot = view->viewport()->mapTo(&container, row.center());

        const auto buttonGuard = holdLeftButton(container, spot);

        const QColor pressed = renderOf(*view->viewport()).pixelColor(row.center());

        // Darker than the hover fill it came from, and still that fill's own hue: derived from
        // it, not from the surface underneath and not from nothing at all.
        const QColor hovered(0x00, 0x00, 0xff);
        QVERIFY2(
            pressed != hovered,
            "the pressed row painted the plain hover fill, so the press never resolved"
        );
        QVERIFY(pressed.blue() > 0 && pressed.blue() < hovered.blue());
        QVERIFY(pressed.blue() > pressed.red() && pressed.blue() > pressed.green());
    }

    // Letting go puts the row back. The state is read off the pointer at paint time, so this is
    // really a test that the release repaints at all — see the press's own repaint below.
    void test_releasingTheButtonPutsTheRowBack()  // NOLINT
    {
        const auto pressedFill = overrideToken("DropdownListRowPressedBackground", "#00ff00");

        Gui::FreeCADStyle& style = installFreshApplicationStyle();

        QFrame container;
        auto* view = buildAdoptedDropdown(container, style);
        QVERIFY(QTest::qWaitForWindowExposed(&container));

        const QRect row = view->visualRect(view->model()->index(1, 0));
        const QPoint spot = view->viewport()->mapTo(&container, row.center());

        // Pressed and let go again, with nothing in between: the guard's whole body is the
        // release.
        {
            const auto buttonGuard = holdLeftButton(container, spot);
        }

        const QImage canvas = renderOf(*view->viewport());

        // Back to the hover fill: the pointer has not moved, only the button came up.
        QCOMPARE(canvas.pixelColor(row.center()), QColor(0x00, 0x00, 0xff));
    }

    // The press has to ask for the repaint itself. Nothing else will: the hovered row does not
    // change, and a dropdown makes the row under the pointer current and selected before the
    // button ever goes down, so neither the view nor the selection model has anything to
    // update. Without the request the fill lands only when something unrelated repaints the row
    // — which is to say, not while the owner is looking at it.
    //
    // renderOf() paints on demand and would hide exactly that, so the paint is counted where it
    // arrives rather than asserted through a rendered pixel.
    void test_pressingAndReleasingRepaintTheRow()  // NOLINT
    {
        Gui::FreeCADStyle& style = installFreshApplicationStyle();

        QFrame container;
        auto* view = buildAdoptedDropdown(container, style);
        QVERIFY(QTest::qWaitForWindowExposed(&container));

        const QRect row = view->visualRect(view->model()->index(1, 0));
        const QPoint spot = view->viewport()->mapTo(&container, row.center());

        // The pointer arrives first, so the hover change is spent before anything is counted.
        // The row is made current too, which is the state a real dropdown's press lands in: Qt
        // moves the current row under the pointer as it travels, so by the time the button goes
        // down there is nothing left for the view or the selection model to update. Without
        // this the press changes the current row and Qt repaints for its own reasons, and the
        // count below would pass whether the style asked for anything or not.
        QTest::mouseMove(container.windowHandle(), spot);
        view->setCurrentIndex(view->model()->index(1, 0));
        QCoreApplication::processEvents();

        PaintCounter counter;
        view->viewport()->installEventFilter(&counter);
        const auto filterGuard = qScopeGuard([view, &counter] {
            view->viewport()->removeEventFilter(&counter);
        });

        QTest::mousePress(container.windowHandle(), Qt::LeftButton, Qt::NoModifier, spot);
        QCoreApplication::processEvents();
        QVERIFY2(counter.paints > 0, "the press did not repaint the row it pressed");

        counter.paints = 0;
        QTest::mouseRelease(container.windowHandle(), Qt::LeftButton, Qt::NoModifier, spot);
        QCoreApplication::processEvents();
        QVERIFY2(counter.paints > 0, "the release left the row painted as pressed");
    }
};

QTEST_MAIN(TestComboDropdown)

#include "ComboDropdown.moc"
