// SPDX-License-Identifier: LGPL-2.1-or-later

#include <vector>

#include <Inventor/SoDB.h>

#include <QListView>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTest>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>

#include <Gui/GeometrySelectorPopup.h>
#include <Gui/GeometrySelectorWidget.h>

#include "DropdownStyleFixture.h"

// The popup resolves the same DropdownList tokens a combo box's popup does, so the fixture's
// theme is the whole vocabulary these tests assert in: #ff0000 selected, #0000ff hovered,
// #101010 surface, #00ff00 surface edge.
class TestGeometrySelectorPopup: public QObject, private DropdownStyleFixture
{
    Q_OBJECT

private Q_SLOTS:

    // With the fixture's live Gui::Application, App::GetApplication().newDocument() below also
    // builds a Gui::Document, and that builds a Thumbnail's SoOrthographicCamera — the first
    // Coin node this binary constructs. SoDB::init() is idempotent (mirrors InventorBuilder.cpp),
    // so doing it once here is what every other Coin-touching test does.
    void initTestCase()
    {
        SoDB::init();
    }

    void cleanupTestCase()
    {
        SoDB::finish();
    }

    void init()
    {
        App::DocumentInitFlags flags;
        flags.createView = false;
        m_docName = App::GetApplication().getUniqueDocumentName("gsp_test");
        m_doc = App::GetApplication().newDocument(m_docName.c_str(), "testUser", flags);
        m_object = m_doc->addObject("App::FeatureTest", "TestObj");
    }

    void cleanup()
    {
        if (App::GetApplication().getDocument(m_docName.c_str())) {
            App::GetApplication().closeDocument(m_docName.c_str());
        }
        m_doc = nullptr;
        m_object = nullptr;
    }

    // The chosen entry is marked the way every other dropdown marks it — a background, not a
    // glyph — and no other row is.
    void test_theChosenEntryIsFilledFromTheDropdownRowToken()  // NOLINT
    {
        installFreshPopupStyle();

        Gui::GeometrySelectorPopup
            popup(optionsOf(4), {}, /*allowCustom=*/false, /*currentIndex=*/2, nullptr);
        showPopup(popup);

        QListView* view = viewOf(popup);
        const QImage canvas = renderOf(*view->viewport());

        QCOMPARE(canvas.pixelColor(rowCentre(*view, 2)), QColor(0xff, 0x00, 0x00));
        QVERIFY(canvas.pixelColor(rowCentre(*view, 3)) != QColor(0xff, 0x00, 0x00));
    }

    // Two rows can carry a highlight at once: the pointer's, and the entry the control holds.
    void test_aHoveredRowDoesNotStealTheChosenEntrysMark()  // NOLINT
    {
        installFreshPopupStyle();

        Gui::GeometrySelectorPopup
            popup(optionsOf(4), {}, /*allowCustom=*/false, /*currentIndex=*/2, nullptr);
        showPopup(popup);

        QListView* view = viewOf(popup);
        // The precondition hover depends on. Stated rather than assumed — though note it holds
        // even without GeometrySelectorPopup's own explicit setMouseTracking(true) call: any
        // live FreeCADStyle sets Qt::WA_MouseTracking on every QAbstractItemView it polishes,
        // and QAbstractScrollArea::event() forwards that to the viewport on its own
        // (QEvent::MouseTrackingChange, qabstractscrollarea.cpp). So this assertion cannot by
        // itself catch that line going missing; it only documents that the mechanism the row
        // colours below depend on is in place.
        QVERIFY(view->viewport()->hasMouseTracking());
        hover(popup, *view, 0);

        const QImage canvas = renderOf(*view->viewport());

        QCOMPARE(canvas.pixelColor(rowCentre(*view, 2)), QColor(0xff, 0x00, 0x00));
        QCOMPARE(canvas.pixelColor(rowCentre(*view, 0)), QColor(0x00, 0x00, 0xff));
    }

    // The arrow keys move a cursor, not the selection. Without the chosen-row tag the mark
    // would follow the keys and the popup would forget which entry it holds.
    void test_arrowingDoesNotMoveTheChosenEntrysMark()  // NOLINT
    {
        installFreshPopupStyle();

        Gui::GeometrySelectorPopup
            popup(optionsOf(4), {}, /*allowCustom=*/false, /*currentIndex=*/2, nullptr);
        showPopup(popup);

        QListView* view = viewOf(popup);
        // The QWidget overload of keyClick calls qt_sendSpontaneousEvent directly on the named
        // widget and never consults focus, so it would pass whether or not the popup's focus
        // proxy actually routed the key to the view. Going through the window is what a real key
        // press does, and is the only form that can catch a focus-routing regression.
        QTest::keyClick(popup.windowHandle(), Qt::Key_Down);
        QCOMPARE(view->currentIndex().row(), 3);

        const QImage canvas = renderOf(*view->viewport());

        QCOMPARE(canvas.pixelColor(rowCentre(*view, 2)), QColor(0xff, 0x00, 0x00));
        QCOMPARE(canvas.pixelColor(rowCentre(*view, 3)), QColor(0x00, 0x00, 0xff));
    }

    // A popup that holds nothing is the widget's most common state: m_currentIndex starts at -1
    // and setOptions puts it back there. The cursor must still read as a cursor, so arrowing
    // onto a row paints it hovered — not with the accent fill that says "this is what you have
    // chosen", which would claim a choice the control has not made.
    void test_aPopupHoldingNothingPaintsItsCursorAsHovered()  // NOLINT
    {
        installFreshPopupStyle();

        Gui::GeometrySelectorPopup
            popup(optionsOf(4), {}, /*allowCustom=*/false, /*currentIndex=*/-1, nullptr);
        showPopup(popup);

        QListView* view = viewOf(popup);
        QTest::keyClick(popup.windowHandle(), Qt::Key_Down);
        QVERIFY(view->currentIndex().isValid());

        const QImage canvas = renderOf(*view->viewport());

        QCOMPARE(
            canvas.pixelColor(rowCentre(*view, view->currentIndex().row())),
            QColor(0x00, 0x00, 0xff)
        );
    }

    // Clicking a row is what the popup exists for. Delivered through the popup's window rather
    // than by calling activateIndex, so the whole path — the view's own click handling and the
    // connection onto it — is what the test rests on.
    void test_clickingARowActivatesIt()  // NOLINT
    {
        installFreshPopupStyle();

        Gui::GeometrySelectorPopup
            popup(optionsOf(4), {}, /*allowCustom=*/false, /*currentIndex=*/0, nullptr);
        showPopup(popup);

        QListView* view = viewOf(popup);
        QSignalSpy activations(&popup, &Gui::GeometrySelectorPopup::optionActivated);

        const QPoint windowPos = view->viewport()->mapTo(&popup, rowCentre(*view, 2));
        QTest::mouseClick(popup.windowHandle(), Qt::LeftButton, Qt::NoModifier, windowPos);
        QCoreApplication::processEvents();

        QCOMPARE(activations.count(), 1);
        QCOMPARE(activations.at(0).at(0).toInt(), 2);
    }

    // The popup has a surface of its own — the frame around the list paints it, from the same
    // tokens a combo popup's frame uses.
    void test_theSurfaceIsDrawnFromTheDropdownSurfaceTokens()  // NOLINT
    {
        installFreshPopupStyle();

        Gui::GeometrySelectorPopup
            popup(optionsOf(3), {}, /*allowCustom=*/false, /*currentIndex=*/-1, nullptr);
        showPopup(popup);

        const QImage canvas = renderOf(popup);

        // The edge, and the fill just inside the border and padding but above the first row.
        QCOMPARE(canvas.pixelColor(0, 0), surfaceBorderColor);
        QCOMPARE(canvas.pixelColor(containerBorder, containerBorder), surfaceColor);
    }

    // More rows than the cap allows: the popup stops at the cap, scrolls rather than growing
    // past the screen, and ends on a row edge — exactly as a combo popup does.
    void test_aLongPopupIsCappedAndEndsOnARowEdge()  // NOLINT
    {
        const auto capGuard = overrideToken("DropdownListMaxHeight", "80px");

        installFreshPopupStyle();

        // A parent is what FreeCADStyle::correctComboPopupPlacement needs even to begin — it
        // bails out before the row-edge trim runs at all if the container has none. A combo
        // box's popup always has one (the combo box itself); GeometrySelectorWidget passes
        // itself as the popup's parent too, so an anchor here matches how the popup is ever
        // really shown, rather than testing a parentless shape nothing produces.
        QWidget anchor;
        anchor.move(200, 200);
        anchor.show();
        QVERIFY(QTest::qWaitForWindowExposed(&anchor));

        Gui::GeometrySelectorPopup
            popup(optionsOf(40), {}, /*allowCustom=*/false, /*currentIndex=*/0, &anchor);
        showPopup(popup);

        QListView* view = viewOf(popup);
        QVERIFY(popup.height() <= 80);
        QVERIFY(view->verticalScrollBar()->maximum() > 0);

        // The cap is a pixel count, not a whole number of rows, so what it leaves over is a
        // partial row. The style trims that back off, so a scrolled popup must end exactly on a
        // row edge: the row straddling the viewport's bottom edge is shown in full, not a sliver.
        // Asserted directly against that row rather than through view->sizeHintForRow(0) modulo
        // the viewport height — that modulo assumes every row shares one pitch, which no longer
        // holds once a popup can also hold a separator row of its own height.
        const int bottom = view->viewport()->height() - 1;
        const QModelIndex lastRow = view->indexAt({0, bottom});
        QVERIFY(lastRow.isValid());
        QCOMPARE(view->visualRect(lastRow).bottom(), bottom);
    }

    // The shape that exercises the separator sizing, the popup trim, the model construction and
    // the rule placement together: a capped, scrolling popup holding options, history and Custom
    // — so two rules — that still ends on a row edge rather than a wrong modulo. Unlike the
    // options-only cap test above, FreeCADStyle::snapComboPopupToWholeRows() cannot lean on a
    // single row pitch here at all.
    void test_aCappedPopupWithHistoryAndRulesEndsOnARowEdge()  // NOLINT
    {
        const auto capGuard = overrideToken("DropdownListMaxHeight", "80px");

        installFreshPopupStyle();

        QWidget anchor;
        anchor.move(200, 200);
        anchor.show();
        QVERIFY(QTest::qWaitForWindowExposed(&anchor));

        Gui::GeometrySelectorPopup popup(
            optionsOf(20),
            optionsOf(20),
            /*allowCustom=*/true,
            /*currentIndex=*/0,
            &anchor
        );
        showPopup(popup);

        QListView* view = viewOf(popup);
        QVERIFY(popup.height() <= 80);
        QVERIFY2(
            view->verticalScrollBar()->maximum() > 0,
            "the popup was not capped, so there was nothing to trim"
        );

        const int bottom = view->viewport()->height() - 1;
        const QModelIndex lastRow = view->indexAt({0, bottom});
        QVERIFY(lastRow.isValid());
        QCOMPARE(view->visualRect(lastRow).bottom(), bottom);
    }

    // "current" places the chosen row on the control; "below" meets its bottom edge. Both are
    // the style's placement correction, reached because the popup is a tagged container.
    void test_placementFollowsTheDropdownToken()  // NOLINT
    {
        {
            const auto modeGuard = overrideToken("DropdownListPlacement", "below");
            installFreshPopupStyle();

            // Built after the style install, not before: GeometrySelectorWidget hands the
            // FreeCADStyle singleton to its child buttons via setStyle(), which QWidget keeps
            // as a raw pointer, so an anchor built while installFreshPopupStyle() is about to
            // delete the previous singleton would be handed a dangling one.
            Gui::GeometrySelectorWidget anchor(Gui::GeometryQuantity::Single);
            anchor.move(200, 200);
            anchor.show();
            QVERIFY(QTest::qWaitForWindowExposed(&anchor));

            auto* popup = new Gui::GeometrySelectorPopup(optionsOf(3), {}, false, 1, &anchor);
            popup->resize(anchor.width(), popup->sizeHint().height());
            popup->move(anchor.mapToGlobal(QPoint(0, anchor.height())));
            popup->show();
            QCoreApplication::processEvents();  // the correction is deferred by a zero timer

            QCOMPARE(
                popup->mapToGlobal(QPoint {}).y(),
                anchor.mapToGlobal(QPoint {}).y() + anchor.height()
            );
            popup->close();
        }

        {
            const auto modeGuard = overrideToken("DropdownListPlacement", "current");
            installFreshPopupStyle();

            Gui::GeometrySelectorWidget anchor(Gui::GeometryQuantity::Single);
            anchor.move(200, 200);
            anchor.show();
            QVERIFY(QTest::qWaitForWindowExposed(&anchor));

            auto* popup = new Gui::GeometrySelectorPopup(optionsOf(3), {}, false, 1, &anchor);
            popup->resize(anchor.width(), popup->sizeHint().height());
            popup->move(anchor.mapToGlobal(QPoint(0, anchor.height())));
            popup->show();
            QCoreApplication::processEvents();

            QListView* view = viewOf(*popup);
            const QPoint rowTopLeft = view->visualRect(view->model()->index(1, 0)).topLeft();
            QCOMPARE(view->viewport()->mapToGlobal(rowTopLeft).y(), anchor.mapToGlobal(QPoint {}).y());
            popup->close();
        }
    }

    // The rule sits between groups and nowhere else: never first, never last, never doubled.
    void test_separatorsDivideTheGroups()  // NOLINT
    {
        installFreshPopupStyle();

        Gui::GeometrySelectorPopup popup(optionsOf(2), optionsOf(3), /*allowCustom=*/true, -1, nullptr);
        showPopup(popup);

        QListView* view = viewOf(popup);
        // 2 options + rule + 3 history + rule + custom
        QCOMPARE(view->model()->rowCount(), 8);
        QCOMPARE(popup.optionCount(), 6);  // 2 options + 3 history + custom; rules do not count
        QVERIFY(isSeparatorRow(*view, 2));
        QVERIFY(isSeparatorRow(*view, 6));
        for (int row : {0, 1, 3, 4, 5, 7}) {
            QVERIFY2(!isSeparatorRow(*view, row), qPrintable(QStringLiteral("row %1").arg(row)));
        }
    }

    // With no history the two rules collapse into the single one that divides the options from
    // Custom, and with neither history nor Custom there is no rule at all.
    void test_aRuleNeedsANonEmptyGroupOnBothSides()  // NOLINT
    {
        installFreshPopupStyle();

        Gui::GeometrySelectorPopup withCustom(optionsOf(2), {}, /*allowCustom=*/true, -1, nullptr);
        showPopup(withCustom);
        QCOMPARE(viewOf(withCustom)->model()->rowCount(), 4);
        QVERIFY(isSeparatorRow(*viewOf(withCustom), 2));

        Gui::GeometrySelectorPopup optionsOnly(optionsOf(2), {}, /*allowCustom=*/false, -1, nullptr);
        showPopup(optionsOnly);
        QCOMPARE(viewOf(optionsOnly)->model()->rowCount(), 2);

        // The group above a rule can be empty too — a popup with no predefined options must not
        // open on a leading rule in front of its history or its Custom row.
        Gui::GeometrySelectorPopup historyOnly({}, optionsOf(2), /*allowCustom=*/false, -1, nullptr);
        showPopup(historyOnly);
        QCOMPARE(viewOf(historyOnly)->model()->rowCount(), 2);
        QVERIFY(!isSeparatorRow(*viewOf(historyOnly), 0));

        Gui::GeometrySelectorPopup customOnly({}, {}, /*allowCustom=*/true, -1, nullptr);
        showPopup(customOnly);
        QCOMPARE(viewOf(customOnly)->model()->rowCount(), 1);
        QVERIFY(!isSeparatorRow(*viewOf(customOnly), 0));
    }

    // A separator carries neither flag, which is what makes QListView::moveCursor step over it:
    // it filters its candidates through removeCurrentAndDisabled().
    void test_aSeparatorRowIsNeitherSelectableNorEnabled()  // NOLINT
    {
        installFreshPopupStyle();

        Gui::GeometrySelectorPopup popup(optionsOf(2), {}, /*allowCustom=*/true, -1, nullptr);
        showPopup(popup);

        const Qt::ItemFlags flags = viewOf(popup)->model()->index(2, 0).flags();
        QVERIFY(!flags.testFlag(Qt::ItemIsSelectable));
        QVERIFY(!flags.testFlag(Qt::ItemIsEnabled));
    }

    // A row number is no longer an index. The first history row sits two rows below the last
    // option but one index above it, and activating it must report the index.
    void test_activatingAHistoryRowEmitsItsIndexNotItsRow()  // NOLINT
    {
        installFreshPopupStyle();

        Gui::GeometrySelectorPopup popup(optionsOf(2), optionsOf(3), /*allowCustom=*/true, -1, nullptr);
        showPopup(popup);

        QSignalSpy spy(&popup, &Gui::GeometrySelectorPopup::optionActivated);
        QListView* view = viewOf(popup);
        const QPoint windowPos = view->viewport()->mapTo(&popup, rowCentre(*view, 3));
        QTest::mouseClick(popup.windowHandle(), Qt::LeftButton, Qt::NoModifier, windowPos);
        QCoreApplication::processEvents();

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 2);  // row 3 is the first history entry: index 2
    }

    // The Custom row is the last index, above whatever history added.
    void test_theCustomRowIsStillTheLastIndex()  // NOLINT
    {
        installFreshPopupStyle();

        Gui::GeometrySelectorPopup popup(optionsOf(2), optionsOf(3), /*allowCustom=*/true, -1, nullptr);
        showPopup(popup);

        QSignalSpy spy(&popup, &Gui::GeometrySelectorPopup::optionActivated);
        QListView* view = viewOf(popup);
        const QPoint windowPos = view->viewport()->mapTo(&popup, rowCentre(*view, 7));
        QTest::mouseClick(popup.windowHandle(), Qt::LeftButton, Qt::NoModifier, windowPos);
        QCoreApplication::processEvents();

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 5);  // 2 options + 3 history
    }

    // Qt does emit clicked() for a disabled row — a press+release on a rule reaches the
    // connection carrying its row, whose m_rowToIndex entry is -1. Nothing must activate.
    void test_clickingASeparatorNeverActivatesAnything()  // NOLINT
    {
        // The fixture leaves the rule's own height token unset; give it one so the row is tall
        // enough to actually receive the click (see
        // test_hoveringOverASeparatorDoesNotMoveTheCursorThere).
        const auto heightGuard = overrideToken("DropdownListSeparatorHeight", "20px");
        installFreshPopupStyle();

        Gui::GeometrySelectorPopup popup(optionsOf(2), optionsOf(3), /*allowCustom=*/true, -1, nullptr);
        showPopup(popup);

        QSignalSpy spy(&popup, &Gui::GeometrySelectorPopup::optionActivated);
        QListView* view = viewOf(popup);
        const QPoint windowPos = view->viewport()->mapTo(&popup, rowCentre(*view, 2));
        QTest::mouseClick(popup.windowHandle(), Qt::LeftButton, Qt::NoModifier, windowPos);
        QCoreApplication::processEvents();

        QCOMPARE(spy.count(), 0);
    }

    // The pointer passing over a rule must not park the cursor there, or Enter would activate
    // nothing the instant it crosses one. GeometrySelectorPopup no longer filters this itself —
    // it relies on QAbstractItemView::setCurrentIndex() refusing a disabled index — so this test
    // guards that reliance rather than code in this class.
    void test_hoveringOverASeparatorDoesNotMoveTheCursorThere()  // NOLINT
    {
        // The fixture leaves the rule's own height token unset, so give it one: a zero-height
        // row's centre coincides with its neighbour's edge and the hover would land ambiguously.
        const auto heightGuard = overrideToken("DropdownListSeparatorHeight", "20px");
        installFreshPopupStyle();

        Gui::GeometrySelectorPopup
            popup(optionsOf(2), optionsOf(3), /*allowCustom=*/true, /*currentIndex=*/0, nullptr);
        showPopup(popup);

        QListView* view = viewOf(popup);
        hover(popup, *view, 2);  // the rule between the options and the history

        QCOMPARE(view->currentIndex().row(), 0);
    }

    // The popup opens with the cursor on the chosen entry's row, which is not its index once a
    // separator sits above it — the number constrainDropdown() aligns the popup against.
    void test_openingOnAHistoryPickPutsTheCursorOnItsRow()  // NOLINT
    {
        installFreshPopupStyle();

        Gui::GeometrySelectorPopup
            popup(optionsOf(2), optionsOf(3), /*allowCustom=*/true, /*currentIndex=*/2, nullptr);
        showPopup(popup);

        QCOMPARE(viewOf(popup)->currentIndex().row(), 3);
    }

    // The "chosen" fill is keyed off the row the popup told FreeCADStyle it opened on, not off
    // the cursor — it must survive the cursor moving away, and it must be the row a separator
    // shifted, not the index constrainDropdown() was actually handed.
    void test_theChosenFillStaysOnTheHistoryRowAfterArrowing()  // NOLINT
    {
        installFreshPopupStyle();

        Gui::GeometrySelectorPopup
            popup(optionsOf(2), optionsOf(3), /*allowCustom=*/true, /*currentIndex=*/2, nullptr);
        showPopup(popup);

        QListView* view = viewOf(popup);
        QTest::keyClick(popup.windowHandle(), Qt::Key_Down);
        QCoreApplication::processEvents();

        const QImage canvas = renderOf(*view->viewport());
        QCOMPARE(canvas.pixelColor(rowCentre(*view, 3)), QColor(0xff, 0x00, 0x00));
        QCOMPARE(canvas.pixelColor(rowCentre(*view, 4)), QColor(0x00, 0x00, 0xff));
    }

    // Arrow keys step over the rule rather than parking on it — the flags do this, but a change
    // to how a separator is built could silently drop it.
    void test_arrowKeysStepOverASeparator()  // NOLINT
    {
        installFreshPopupStyle();

        Gui::GeometrySelectorPopup
            popup(optionsOf(2), optionsOf(3), /*allowCustom=*/true, /*currentIndex=*/1, nullptr);
        showPopup(popup);

        QTest::keyClick(popup.windowHandle(), Qt::Key_Down);
        QCoreApplication::processEvents();

        QCOMPARE(viewOf(popup)->currentIndex().row(), 3);
    }

    // The pointer over a predefined option names that option.
    void test_hoveringAnOptionRowReportsItsIndex()  // NOLINT
    {
        installFreshPopupStyle();

        Gui::GeometrySelectorPopup
            popup(optionsOf(3), {}, /*allowCustom=*/false, /*currentIndex=*/0, nullptr);
        showPopup(popup);
        QListView* view = viewOf(popup);
        // A popup with no anchor always opens at the same screen position, so a fresh popup's
        // row can sit exactly where an earlier test in this run left the pointer: with no actual
        // change in global position, QTest::mouseMove synthesises no move at all (the same trap
        // hover()'s own comment describes for the QWidget overload). Landing here first, before
        // the spy is listening, guarantees the coming hover() is a genuine move regardless of
        // where the previous test's popup ended.
        hoverAt(popup, *view, QPoint(0, 0));
        QSignalSpy spy(&popup, &Gui::GeometrySelectorPopup::optionHovered);

        hover(popup, *view, 2);

        QTRY_COMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toInt(), 2);
    }

    // A remembered pick sits past the predefined options in the index space, and the rule
    // between the groups shifts its row without shifting its index.
    void test_hoveringAHistoryRowReportsItsIndex()  // NOLINT
    {
        // The fixture leaves the rule's own height token unset, so give it one: a zero-height
        // rule row sits flush against its neighbour, and hovering the row right after it would
        // land ambiguously (see test_hoveringOverASeparatorDoesNotMoveTheCursorThere).
        const auto heightGuard = overrideToken("DropdownListSeparatorHeight", "20px");
        installFreshPopupStyle();

        Gui::GeometrySelectorPopup popup(
            optionsOf(2),
            optionsOf(1),
            /*allowCustom=*/false,
            /*currentIndex=*/0,
            nullptr
        );
        showPopup(popup);
        QListView* view = viewOf(popup);
        // See test_hoveringAnOptionRowReportsItsIndex: without this anchor, this popup's row can
        // coincidentally sit where an earlier test left the pointer and the coming move would be
        // synthesised as no move at all.
        hoverAt(popup, *view, QPoint(0, 0));
        QSignalSpy spy(&popup, &Gui::GeometrySelectorPopup::optionHovered);

        // Rows: 0,1 options · 2 rule · 3 history. Index 2 is the history entry.
        hover(popup, *view, 3);

        QTRY_COMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toInt(), 2);
    }

    // A rule names nothing. It is not selectable, but a pointer still travels over it.
    void test_hoveringARuleReportsNothing()  // NOLINT
    {
        // The fixture leaves the rule's own height token unset, so give it one: a zero-height
        // row's centre coincides with its neighbour's edge and the hover would land ambiguously
        // (see test_hoveringOverASeparatorDoesNotMoveTheCursorThere).
        const auto heightGuard = overrideToken("DropdownListSeparatorHeight", "20px");
        installFreshPopupStyle();

        Gui::GeometrySelectorPopup popup(
            optionsOf(2),
            optionsOf(1),
            /*allowCustom=*/false,
            /*currentIndex=*/0,
            nullptr
        );
        showPopup(popup);
        QListView* view = viewOf(popup);
        // See test_hoveringAnOptionRowReportsItsIndex: without this anchor, this popup's row can
        // coincidentally sit where an earlier test left the pointer and the coming move would be
        // synthesised as no move at all.
        hoverAt(popup, *view, QPoint(0, 0));
        QSignalSpy spy(&popup, &Gui::GeometrySelectorPopup::optionHovered);

        hover(popup, *view, 2);  // the rule between the two groups

        QTRY_COMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toInt(), -1);
    }

    // Below the last row is over nothing: indexAt() answers an invalid index there, which is
    // the other way (besides a rule) the hover must report -1.
    void test_hoveringBelowTheLastRowReportsNothing()  // NOLINT
    {
        installFreshPopupStyle();

        Gui::GeometrySelectorPopup
            popup(optionsOf(3), {}, /*allowCustom=*/false, /*currentIndex=*/0, nullptr);
        showPopup(popup);
        QListView* view = viewOf(popup);
        // showPopup() sizes the popup exactly to its rows, so there is deliberately no blank
        // viewport area below the last one yet — grow the popup to make room for one.
        popup.resize(popup.width(), popup.height() + 50);
        QCoreApplication::processEvents();

        const QModelIndex lastRow = view->model()->index(view->model()->rowCount() - 1, 0);
        const int belowLastRow = view->visualRect(lastRow).bottom() + 10;
        QVERIFY(belowLastRow < view->viewport()->height());  // still inside the viewport, not past it

        // A real row first, so there is a hover to withdraw: the popup starts unhovered
        // (m_hoveredIndex == -1), and moving from nothing to nothing would not publish at all.
        hover(popup, *view, 0);
        QSignalSpy spy(&popup, &Gui::GeometrySelectorPopup::optionHovered);

        hoverAt(popup, *view, QPoint(view->viewport()->width() / 2, belowLastRow));

        QTRY_COMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toInt(), -1);
    }

    // Moving within one row must not re-publish: the highlight would be rebuilt on every
    // pointer move, in every 3D view.
    //
    // This assertion cannot be loosened into a QTRY_COMPARE: "stays at 1" is not "eventually
    // reaches 1", and no amount of waiting distinguishes the two. It also cannot tolerate an
    // extra, environment-injected transition without losing the ability to catch the bug it
    // exists for — with setHoveredIndex()'s de-duplication guard deleted, every one of the
    // three moves below still resolves to the same row, so the final published value is still
    // correct; only the count (1 vs. 3) tells them apart. Weakening the assertion to check only
    // the last value would make this test blind to that exact regression.
    //
    // Investigated: under real, sustained CPU load, this test can flake — but only when the
    // binary is invoked directly against a live, loaded X11 display (as opposed to how this
    // suite is actually run: CTest sets QT_QPA_PLATFORM=offscreen for this target). Confirmed
    // by instrumenting eventFilter()/setHoveredIndex()/leaveEvent(): under xcb the popup
    // occasionally receives a genuine QEvent::Leave between two of the moves below, with no
    // corresponding change in this test's own synthesised pointer position — i.e. the real X
    // server, not this test, generates it, most likely because a loaded window manager is slow
    // to keep up with the rapid popup creation/destruction this suite does test-over-test. That
    // Leave is indistinguishable, from inside the widget, from a pointer that truly left, so
    // GeometrySelectorPopup::leaveEvent() (correctly) withdraws the hover, and the next
    // synthesised move (also correctly) re-publishes it — both real signals, not a bug in this
    // class. 130 combined runs under sustained synthetic CPU load (100 direct invocations with
    // QT_QPA_PLATFORM=offscreen set explicitly, 30 via `ctest -R
    // GeometrySelectorPopup_Tests_run`, which sets it automatically) produced zero failures of
    // any test in this suite, including this one. Explicitly draining any deferred-delete
    // backlog (QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete)) before the
    // spy is created did not measurably reduce the xcb failure rate, which is why the fix here
    // is not "wait a bit longer first" — there is nothing this test can wait out that it
    // controls the timing of.
    void test_stayingOnOneRowReportsOnlyOnce()  // NOLINT
    {
        installFreshPopupStyle();

        Gui::GeometrySelectorPopup
            popup(optionsOf(3), {}, /*allowCustom=*/false, /*currentIndex=*/0, nullptr);
        showPopup(popup);
        QListView* view = viewOf(popup);
        QSignalSpy spy(&popup, &Gui::GeometrySelectorPopup::optionHovered);

        hover(popup, *view, 1);
        hoverAt(popup, *view, offsetWithinRow(*view, 1, 2));
        hoverAt(popup, *view, offsetWithinRow(*view, 1, -2));

        QCOMPARE(spy.count(), 1);
    }

    // Hiding the popup withdraws the hover; otherwise a dismissed dropdown leaves the
    // 3D view highlighted with nothing on screen to explain it.
    void test_hidingThePopupWithdrawsTheHover()  // NOLINT
    {
        installFreshPopupStyle();

        Gui::GeometrySelectorPopup
            popup(optionsOf(3), {}, /*allowCustom=*/false, /*currentIndex=*/0, nullptr);
        showPopup(popup);
        QListView* view = viewOf(popup);
        hover(popup, *view, 1);

        QSignalSpy spy(&popup, &Gui::GeometrySelectorPopup::optionHovered);
        popup.hide();

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toInt(), -1);
    }

    // Nothing was hovered, so hiding has nothing to withdraw.
    void test_hidingAnUnhoveredPopupReportsNothing()  // NOLINT
    {
        installFreshPopupStyle();

        Gui::GeometrySelectorPopup
            popup(optionsOf(3), {}, /*allowCustom=*/false, /*currentIndex=*/0, nullptr);
        showPopup(popup);

        QSignalSpy spy(&popup, &Gui::GeometrySelectorPopup::optionHovered);
        popup.hide();

        QCOMPARE(spy.count(), 0);
    }

private:
    // GeometrySelectorPopup::adoptAsDropdown() constrains itself through
    // Application::Instance->freeCADStyle() directly, not through whatever style
    // installFreshApplicationStyle() puts on qApp — see GeometrySelectorPopup.cpp. That
    // singleton outlives every test in this binary, so its own box-geometry and token caches
    // would otherwise pin a popup to whichever token values were in force the first time any
    // test built one. Deleting it makes Application::freeCADStyle() rebuild a fresh instance
    // lazily on the popup's next access, giving it the same per-test freshness the ambient
    // style gets.
    static void installFreshPopupStyle()
    {
        delete Gui::Application::Instance->freeCADStyle();
        DropdownStyleFixture::installFreshApplicationStyle();
    }

    // Distinct labels so a row picked by index cannot be confused with its neighbour.
    std::vector<Gui::GeometrySelectorOption> optionsOf(int count) const
    {
        std::vector<Gui::GeometrySelectorOption> options;
        options.reserve(count);
        for (int index = 0; index < count; ++index) {
            options.push_back(
                Gui::GeometrySelectorOption::fromReference(
                    {.object = m_object, .subName = "Edge" + std::to_string(index + 1)}
                )
            );
        }
        return options;
    }

    static QListView* viewOf(Gui::GeometrySelectorPopup& popup)
    {
        auto* view = popup.findChild<QListView*>();
        Q_ASSERT(view != nullptr);
        return view;
    }

    static QPoint rowCentre(const QListView& view, int row)
    {
        return view.visualRect(view.model()->index(row, 0)).center();
    }

    static bool isSeparatorRow(const QListView& view, int row)
    {
        return view.model()->index(row, 0).data(Qt::AccessibleDescriptionRole).toString()
            == QLatin1String("separator");
    }

    static void showPopup(Gui::GeometrySelectorPopup& popup)
    {
        popup.resize(200, popup.sizeHint().height());
        popup.show();
        QVERIFY(QTest::qWaitForWindowExposed(&popup));
        QCoreApplication::processEvents();  // the placement correction is deferred by a zero timer
    }

    // The pointer arriving over a row. Routed through the popup's QWindow rather than the
    // QWidget overload of QTest::mouseMove, which for a widget with no button held warps the
    // real desktop pointer via QCursor::setPos: on a live display that produces no move at all
    // when the cursor already sits at that global position, and the row is then never hovered.
    // The QWindow overload has no such trap — the same reason keyClick above goes through
    // popup.windowHandle() rather than the view. Routing through the window, rather than
    // synthesizing a QMouseEvent straight at the viewport, also means the event passes through
    // Qt's own mouse-tracking gate (QApplicationPrivate::sendMouseEvent) instead of skipping it
    // outright — genuinely closer to what a real pointer move delivers, even though nothing in
    // this popup currently drops mouse tracking in a way only that gate would catch. Do not
    // simplify.
    static void hover(Gui::GeometrySelectorPopup& popup, QListView& view, int row)
    {
        const QPoint spot = view.visualRect(view.model()->index(row, 0)).center();
        const QPoint windowPos = view.viewport()->mapTo(&popup, spot);
        QTest::mouseMove(popup.windowHandle(), windowPos);
        QCoreApplication::processEvents();
    }

    // A point inside row @p row, @p dy pixels from its centre.
    static QPoint offsetWithinRow(QListView& view, int row, int dy)
    {
        QPoint spot = view.visualRect(view.model()->index(row, 0)).center();
        spot.ry() += dy;
        return spot;
    }

    // The pointer arriving at an exact viewport position. Same routing as hover(): through
    // the popup's QWindow, never the QWidget overload of QTest::mouseMove.
    static void hoverAt(Gui::GeometrySelectorPopup& popup, QListView& view, const QPoint& spot)
    {
        const QPoint windowPos = view.viewport()->mapTo(&popup, spot);
        QTest::mouseMove(popup.windowHandle(), windowPos);
        QCoreApplication::processEvents();
    }

    std::string m_docName;
    App::Document* m_doc = nullptr;
    App::DocumentObject* m_object = nullptr;
};

QTEST_MAIN(TestGeometrySelectorPopup)

#include "GeometrySelectorPopup.moc"
