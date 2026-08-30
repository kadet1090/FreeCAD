// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QApplication>
#include <QHeaderView>
#include <QImage>
#include <QTest>
#include <QTreeWidgetItem>

#include "src/App/InitApplication.h"

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>

#include <Gui/Application.h>
#include <Gui/FreeCADStyle.h>
#include <Gui/SoFCDB.h>
#include <Gui/StyleParameters/ParameterManager.h>
#include <Gui/Tree.h>

namespace
{

/// The item box's fill over a transparent surface, opaque so its edges can be read back off the
/// rendered viewport.
constexpr QRgb ItemColor = qRgb(255, 0, 0);

/// The item box's fill when docked, told apart from the transparent one so a test cannot pass by
/// reading the wrong surface's box.
constexpr QRgb DockedItemColor = qRgb(255, 0, 255);

/// What the canvas is filled with before a widget renders onto it. Not black: an icon's own
/// pixels are, and a scan for "anything painted here" would read them as canvas showing through.
constexpr QRgb Unpainted = qRgb(1, 2, 3);

/// What an item box keeps outside itself on each side, as stated in the fixture.
constexpr int ItemMargin = 3;

/// The edge of an item box over a transparent surface, as stated in the fixture.
constexpr QRgb ItemBorderColor = qRgb(0, 0, 255);

/// The header section's fill over a transparent surface.
constexpr QRgb HeaderColor = qRgb(0, 255, 0);

/// What a header section keeps between its label and its own edge, as stated in the fixture.
constexpr int HeaderPaddingLeft = 5;

/// The rule dividing one column from the next, as stated in the fixture.
constexpr QRgb SeparatorColor = qRgb(255, 255, 0);

/// What every header section after the first is held off the one before it.
constexpr int ColumnGap = 12;

/// What an item box keeps between its own edge and its content, as stated in the fixture.
constexpr int ItemPadding = 5;

const QString ObjectLabel = QStringLiteral("Body");
const QString ObjectDescription = QStringLiteral("a description");

/// Both header sections carry this, so their painted widths can be compared directly.
const QString HeaderLabel = QStringLiteral("Column");

/// Horizontal span of @p colour across @p band, in @p widget's own coordinates. Null when the
/// colour is absent, which is how "nothing was painted here" reads. Child widgets are left out:
/// an open editor is one, and it covers what is being asked about.
///
/// Read near the top of the band rather than down its centre: the label runs along the centre
/// line, and a glyph landing on the box's own edge would be measured as the edge moving.
std::optional<QPair<int, int>> colourSpan(QWidget& widget, const QRect& band, QRgb colour)
{
    QImage canvas(widget.size(), QImage::Format_ARGB32);
    canvas.fill(QColor(Unpainted));
    widget.render(&canvas, QPoint(), QRegion(), QWidget::DrawWindowBackground);

    const int scanline = band.top() + 2;
    const int last = std::min(band.right(), canvas.width() - 1);

    int left = -1;
    int right = -1;
    for (int x = std::max(0, band.left()); x <= last; ++x) {
        if (canvas.pixel(x, scanline) == colour) {
            left = left < 0 ? x : left;
            right = x;
        }
    }

    if (left < 0) {
        return {};
    }
    return QPair<int, int> {left, right};
}

/// Every pixel @p widget painted along the centre line of @p band, in reading order. Black means
/// the canvas still shows through, which is how "nothing was painted here" reads.
QList<QRgb> centreLine(QWidget& widget, const QRect& band)
{
    QImage canvas(widget.size(), QImage::Format_ARGB32);
    canvas.fill(QColor(Unpainted));
    widget.render(&canvas, QPoint(), QRegion(), QWidget::DrawWindowBackground);

    QList<QRgb> line;
    const int scanline = band.center().y();
    for (int x = std::max(0, band.left()); x <= std::min(band.right(), canvas.width() - 1); ++x) {
        line.append(canvas.pixel(x, scanline));
    }
    return line;
}

/// One pixel of @p widget, in its own coordinates.
QRgb pixelAt(QWidget& widget, const QPoint& point)
{
    QImage canvas(widget.size(), QImage::Format_ARGB32);
    canvas.fill(QColor(Unpainted));
    widget.render(&canvas, QPoint(), QRegion(), QWidget::DrawWindowBackground);
    return canvas.pixel(point);
}

int spanWidth(const QPair<int, int>& span)
{
    return span.second - span.first + 1;
}

/// Where the label starts inside @p band: the first pixel along its centre line that is neither
/// the box's own fill nor the surface behind it.
std::optional<int> labelStart(QWidget& widget, const QRect& band, QRgb boxColour)
{
    QImage canvas(widget.size(), QImage::Format_ARGB32);
    canvas.fill(QColor(Unpainted));
    widget.render(&canvas, QPoint(), QRegion(), QWidget::DrawWindowBackground);

    const int scanline = band.center().y();
    const int last = std::min(band.right(), canvas.width() - 1);

    for (int x = std::max(0, band.left()); x <= last; ++x) {
        const QRgb pixel = canvas.pixel(x, scanline);
        if (pixel != boxColour && pixel != Unpainted) {
            return x;
        }
    }
    return {};
}

}  // namespace

/// A transparent tree row is one band across its columns, divided by a rule and holding each
/// column's content off the seam before it. One column on its own still hugs, and the header is
/// drawn the way the rows below it are.
class TestTreeColumns: public QObject
{
    Q_OBJECT

public:
    TestTreeColumns()
    {
        tests::initApplication();

        if (Gui::Application::Instance == nullptr) {
            // View provider types have to be registered before a document can build one, and it
            // is that view provider which puts an object into the tree at all.
            Gui::Application::initTypes();
            new Gui::Application(true);
        }

        // Stated here rather than read from the shipped theme: these tests are about which edge
        // a box lands on, and pinning them to the theme's own numbers would make a future retune
        // of the tree look like a regression.
        Gui::Application::Instance->styleParameterManager()->addSource(
            new Gui::StyleParameters::InMemoryParameterSource(
                {
                    {.name = "DocumentTreeItemTransparentBackground", .value = "#ff0000"},
                    {.name = "DocumentTreeItemTransparentTextColor", .value = "#00ffff"},
                    {.name = "DocumentTreeItemTransparentPadding",
                     .value = "padding(horizontal: 5px, vertical: 2px)"},
                    // A rounded corner would put the box's own edge somewhere other than on the
                    // row's centre line, which is where every span below is read.
                    {.name = "DocumentTreeItemTransparentBorderRadius", .value = "4px"},
                    {.name = "DocumentTreeItemTransparentBorderThickness", .value = "1px"},
                    {.name = "DocumentTreeItemTransparentBorderColor", .value = "#0000ff"},
                    // A margin the seams have to close over: left standing between two cells it
                    // would open a gap in the band as surely as an edge would.
                    {.name = "DocumentTreeItemTransparentMargin", .value = "padding(horizontal: 3px)"},
                    {.name = "DocumentTreeItemTransparentSpacing", .value = "0px"},
                    // As the shipped theme states it: over a transparent surface the row band is
                    // suppressed and the item's own box carries everything, including the
                    // selection. Left resolving, it would paint past the last column.
                    {.name = "DocumentTreeRowTransparentBackground", .value = "reset()"},
                    {.name = "DocumentTreeItemTransparentColumnGap", .value = "12px"},
                    {.name = "DocumentTreeSeparatorTransparentBorderColor", .value = "#ffff00"},
                    {.name = "DocumentTreeSeparatorTransparentBorderThickness", .value = "1px"},

                    {.name = "DocumentTreeItemBackground", .value = "#ff00ff"},
                    {.name = "DocumentTreeItemBorderRadius", .value = "0px"},
                    {.name = "DocumentTreeItemBorderThickness", .value = "0px"},
                    {.name = "DocumentTreeItemMargin", .value = "padding(0px)"},
                    {.name = "DocumentTreeItemSpacing", .value = "0px"},

                    {.name = "HeaderItemTransparentBackground", .value = "#00ff00"},
                    {.name = "HeaderItemTransparentTextColor", .value = "#000000"},
                    {.name = "HeaderItemTransparentPadding",
                     .value = "padding(horizontal: 5px, vertical: 2px)"},
                    {.name = "HeaderItemTransparentBorderRadius", .value = "0px"},
                    {.name = "HeaderItemTransparentBorderThickness", .value = "0px"},
                    {.name = "HeaderItemTransparentColumnGap", .value = "12px"},
                    {.name = "HeaderSeparatorTransparentBorderColor", .value = "#ffff00"},
                    {.name = "HeaderSeparatorTransparentBorderThickness", .value = "1px"},

                    {.name = "HeaderItemBackground", .value = "#00ff00"},
                    {.name = "HeaderItemBorderRadius", .value = "0px"},
                    {.name = "HeaderItemBorderThickness", .value = "0px"},

                    {.name = "DocumentTreeItemBackground", .value = "#ff00ff"},
                    {.name = "DocumentTreeItemBorderRadius", .value = "0px"},
                    {.name = "DocumentTreeItemBorderThickness", .value = "0px"},
                    {.name = "DocumentTreeItemMargin", .value = "padding(0px)"},
                    {.name = "DocumentTreeItemSpacing", .value = "0px"},


                },
                {.name = "Tree Columns Fixture"}
            )
        );

        QApplication::setStyle(new Gui::FreeCADStyle());
    }

private Q_SLOTS:
    void initTestCase()
    {
        // A Gui::Document builds view providers, and those build Coin nodes.
        if (!Gui::SoFCDB::isInitialized()) {
            Gui::SoFCDB::init();
        }
    }

    void init()
    {
        tree = new Gui::TreeWidget("TestColumnsTree");
        tree->resize(600, 300);
        // Both optional columns on: what "Show Description" and "Show internal names" give, and
        // the configuration the band exists for. Stated rather than inherited, because these
        // come from persisted parameters.
        tree->setColumnHidden(1, false);
        tree->setColumnHidden(2, false);
        tree->header()->setVisible(true);
        tree->headerItem()->setText(0, HeaderLabel);
        tree->headerItem()->setText(1, HeaderLabel);
        tree->show();

        // The tree hears about a document and about its objects through separate signals, and it
        // only starts listening for the second once it has built the document's own item.
        document = App::GetApplication().newDocument("tree", "tester", {.createView = false});
        QTRY_COMPARE(tree->topLevelItemCount(), 1);

        auto* object = document->addObject("App::DocumentObjectGroup", "Body");
        object->Label.setValue(ObjectLabel.toStdString());
        object->Label2.setValue(ObjectDescription.toStdString());
        QTRY_COMPARE(tree->topLevelItem(0)->childCount(), 1);

        tree->topLevelItem(0)->setExpanded(true);
        QTRY_VERIFY(!tree->visualItemRect(objectItem()).isEmpty());
        QTRY_VERIFY(tree->header()->sectionSize(1) > 0);
    }

    void cleanup()
    {
        delete tree;
        tree = nullptr;
        App::GetApplication().closeDocument("tree");
    }

    // Each cell used to hug its own content, which over several columns left a string of
    // disconnected islands with no reading order — the label here, a note over there.
    void test_theRowIsOneBandAcrossItsColumns()  // NOLINT
    {
        makeTransparent();
        giveColumnsRoom();

        for (const QRgb pixel : centreLine(*tree->viewport(), paintedBand())) {
            QVERIFY2(pixel != Unpainted, "the band breaks somewhere across the row");
        }
    }

    // Most items carry no description, and the band must not open a hole where one is missing.
    void test_anEmptyDescriptionIsPartOfTheBand()  // NOLINT
    {
        makeTransparent();
        giveColumnsRoom();
        clearDescription();

        for (const QRgb pixel : centreLine(*tree->viewport(), paintedBand())) {
            QVERIFY2(pixel != Unpainted, "the band breaks over the empty description");
        }
    }

    // Docked, the cell is part of the full-width row band every other item view draws, and
    // dropping it would cut a hole in the band the selection is read from.
    void test_anEmptyDescriptionKeepsItsRowBandWhenDocked()  // NOLINT
    {
        giveColumnsRoom();
        clearDescription();

        const QRect cell = columnRect(1);
        const auto band = colourSpan(*tree->viewport(), cell, DockedItemColor);
        QVERIFY(band);
        QCOMPARE(spanWidth(*band), cell.width());
    }

    // The band is the row's own box, not a stripe across the panel: it starts where the item
    // starts, so the tree's indentation still reads as structure.
    void test_theBandStartsAtTheItemsIndent()  // NOLINT
    {
        makeTransparent();
        giveColumnsRoom();

        QRect toViewportEdge = rowBand();
        toViewportEdge.setLeft(0);

        const auto painted = paintedSpan(toViewportEdge);
        QVERIFY(painted);
        QCOMPARE(painted->first, columnRect(0).left() + ItemMargin);
    }

    void test_theBandEndsAtTheLastColumn()  // NOLINT
    {
        makeTransparent();
        giveColumnsRoom();

        QRect toViewportEdge = rowBand();
        toViewportEdge.setRight(tree->viewport()->width() - 1);

        const auto painted = paintedSpan(toViewportEdge);
        QVERIFY(painted);
        QCOMPARE(painted->second, columnRect(lastVisibleColumn()).right() - ItemMargin);
    }

    // The seam is where two cells meet. A box edge there would cut the band in two; a rule
    // there is what tells one column from the next.
    void test_theSeamCarriesARuleAndNoBoxEdge()  // NOLINT
    {
        makeTransparent();
        giveColumnsRoom();

        const int seam = columnRect(1).left();
        const int inside = paintedBand().top() + 3;

        QCOMPARE(pixelAt(*tree->viewport(), QPoint(seam, inside)), SeparatorColor);
        QCOMPARE(pixelAt(*tree->viewport(), QPoint(seam - 1, inside)), ItemColor);
    }

    // Nothing to divide: a rule down a one-column row would read as an edge the row does not
    // have.
    void test_aSingleColumnRowCarriesNoRule()  // NOLINT
    {
        makeTransparent();
        giveColumnsRoom();
        tree->setColumnHidden(1, true);
        tree->setColumnHidden(2, true);
        QCoreApplication::processEvents();

        for (const QRgb pixel : centreLine(*tree->viewport(), columnRect(0))) {
            QVERIFY2(pixel != SeparatorColor, "a lone column drew a rule against nothing");
        }
    }

    // The band joins the columns; the gap is what still keeps their contents apart.
    void test_aColumnHoldsItsContentOffTheSeam()  // NOLINT
    {
        makeTransparent();
        giveColumnsRoom();

        const int seam = columnRect(1).left();

        // Scanned from past the rule, and for anything that is not the fill: a glyph's edge
        // pixels are blends, so few of them carry the text colour outright.
        const auto text = labelStart(*tree->viewport(), columnRect(1).adjusted(1, 0, 0, 0), ItemColor);
        QVERIFY(text);
        QVERIFY2(
            *text >= seam + ItemPadding + ColumnGap,
            qPrintable(QStringLiteral("text at %1px, %2px after the seam at %3px")
                           .arg(*text)
                           .arg(*text - seam)
                           .arg(seam))
        );
    }

    // The gap has to be reserved in the width as well as applied to the content, or resizing a
    // column to its contents would elide exactly that much of the label. The view asks for that
    // width before it lays the row out, so the columns have to be read from the header instead.
    void test_theColumnGapIsReservedInTheWidth()  // NOLINT
    {
        makeTransparent();
        giveColumnsRoom();

        const int held = hintWidth(1);

        tree->setColumnHidden(0, true);
        QCoreApplication::processEvents();

        QCOMPARE(hintWidth(1), held - ColumnGap);
    }

    // The delegate used to strip State_MouseOver from any cell whose rect did not contain the
    // pointer — comparing a viewport-space rect against view-space coordinates, so showing the
    // header put every comparison out by its height and nothing ever resolved hovered.
    void test_theBandKeepsTheCornersAtItsOwnEnds()  // NOLINT
    {
        makeTransparent();
        giveColumnsRoom();

        const QRect band = paintedBand();

        // The radius cuts the outer corner away, and the top edge runs on past it. Read away
        // from the seam, which the rule paints over.
        QVERIFY(pixelAt(*tree->viewport(), band.topLeft()) != ItemBorderColor);
        QCOMPARE(
            pixelAt(*tree->viewport(), QPoint(columnRect(0).center().x(), band.top())),
            ItemBorderColor
        );
    }

    // One column is the case the hug was built for, and it is still the default: description
    // and internal name are both off until asked for.
    void test_aSingleColumnRowStillHugs()  // NOLINT
    {
        makeTransparent();
        giveColumnsRoom();
        tree->setColumnHidden(1, true);
        tree->setColumnHidden(2, true);
        QCoreApplication::processEvents();

        // Hugging is the box leaving room inside its own cell. Stated against the cell rather
        // than against a width, because hiding a column re-lays the columns out.
        const QRect cell = columnRect(0);
        const auto painted = paintedSpan(cell);
        QVERIFY(painted);
        QVERIFY2(
            painted->second < cell.right() - ItemMargin,
            qPrintable(QStringLiteral("box ends at %1px in a cell ending at %2px")
                           .arg(painted->second)
                           .arg(cell.right()))
        );
    }

    // The header used to hug its labels, which put a row of chips over a row of bands. It is
    // drawn the way the rows below it are.
    // The header used to hug its labels, which put a row of chips over a row of bands. It is
    // drawn the way the rows below it are.
    void test_theHeaderIsOneBandAcrossItsSections()  // NOLINT
    {
        makeTransparent();
        giveColumnsRoom();

        const QRect band = headerSectionRect(0).united(headerSectionRect(lastVisibleColumn()));
        for (const QRgb pixel : centreLine(*tree->header()->viewport(), band)) {
            QVERIFY2(pixel != Unpainted, "the header band breaks somewhere across its sections");
        }
    }

    void test_theHeaderSeamCarriesARule()  // NOLINT
    {
        makeTransparent();
        giveColumnsRoom();

        const int seam = headerSectionRect(1).left();
        QCOMPARE(
            pixelAt(*tree->header()->viewport(), QPoint(seam, headerSectionRect(1).center().y())),
            SeparatorColor
        );
    }

    // Docked, the header keeps the band across its whole section that every header draws.
    void test_theHeaderKeepsTheWholeSectionWhenDocked()  // NOLINT
    {
        giveColumnsRoom();

        const QRect section = headerSectionRect(0);
        const auto band = colourSpan(*tree->header()->viewport(), section, HeaderColor);
        QVERIFY(band);
        QCOMPARE(spanWidth(*band), section.width());
    }

    void test_theHeaderLabelIsHeldOffTheSeam()  // NOLINT
    {
        makeTransparent();
        giveColumnsRoom();

        const QRect section = headerSectionRect(1);
        // Scanned from past the rule, which is not the box's fill either.
        const auto label
            = labelStart(*tree->header()->viewport(), section.adjusted(1, 0, 0, 0), HeaderColor);
        QVERIFY(label);
        QVERIFY2(
            *label >= section.left() + HeaderPaddingLeft + ColumnGap,
            qPrintable(QStringLiteral("label at %1px in a section starting at %2px")
                           .arg(*label)
                           .arg(section.left()))
        );
    }

    // CE_HeaderLabel draws into whatever rect it is handed, so a handler passing it the section
    // leaves the label against the box's edge and the padding token resolving for nothing.
    void test_theHeaderLabelKeepsTheBoxPadding()  // NOLINT
    {
        makeTransparent();
        giveColumnsRoom();

        const auto section = headerBox(0);
        QVERIFY(section);

        const auto label = labelStart(*tree->header()->viewport(), headerSectionRect(0), HeaderColor);
        QVERIFY(label);
        QVERIFY2(
            *label >= section->first + HeaderPaddingLeft,
            qPrintable(
                QStringLiteral("label at %1px in a box starting at %2px").arg(*label).arg(section->first)
            )
        );
    }

private:
    void makeTransparent()
    {
        auto* style = static_cast<Gui::FreeCADStyle*>(QApplication::style());
        style->updateTransparency(tree, true);
        QVERIFY(Gui::FreeCADStyle::isTransparent(tree));
        QVERIFY(Gui::FreeCADStyle::isTransparent(tree->header()));
    }

    /// Widens every column well past its contents, so a box that hugs is unmistakably narrower
    /// than the column holding it. Sized to contents the two would coincide.
    void giveColumnsRoom()
    {
        QHeaderView* header = tree->header();
        header->setSectionResizeMode(QHeaderView::Interactive);
        // The last visible section stretches to the viewport by default, which would make the
        // description column as wide as the tree however it was resized.
        header->setStretchLastSection(false);
        for (int column = 0; column < tree->columnCount(); ++column) {
            header->resizeSection(column, 180);
        }
        QTRY_COMPARE(header->sectionSize(1), 180);
    }

    void clearDescription()
    {
        document->getObject("Body")->Label2.setValue("");
        QTRY_VERIFY(objectItem()->text(1).isEmpty());
    }

    QTreeWidgetItem* objectItem() const
    {
        return tree->topLevelItem(0)->child(0);
    }

    QModelIndex objectIndex(int column) const
    {
        const QModelIndex documentIndex = tree->model()->index(0, 0);
        return tree->model()->index(0, column, documentIndex);
    }

    /// The width the delegate asks for @p column, which is what resizing to contents reserves.
    /// Asked with a rect of its own: the hint does not depend on one, and the cell's own rect
    /// carries the tree's indentation, which has nothing to do with the width being measured.
    int hintWidth(int column) const
    {
        const QModelIndex index = objectIndex(column);

        QStyleOptionViewItem option;
        option.initFrom(tree);
        option.widget = tree;
        option.rect = QRect(0, 0, 400, 24);

        return tree->itemDelegateForIndex(index)->sizeHint(option, index).width();
    }

    /// The row's own band: from where the item starts, after the tree's indentation, to the
    /// right edge of the last visible column.
    QRect rowBand() const
    {
        return columnRect(0).united(columnRect(lastVisibleColumn()));
    }

    /// The band with its own outer margin taken off: what the row actually covers.
    QRect paintedBand() const
    {
        return rowBand().adjusted(ItemMargin, 0, -ItemMargin, 0);
    }

    int lastVisibleColumn() const
    {
        for (int column = tree->columnCount() - 1; column > 0; --column) {
            if (!tree->isColumnHidden(column)) {
                return column;
            }
        }
        return 0;
    }

    /// The span @p band actually carries paint over, whatever colour: the box's own edge counts,
    /// where a scan for the fill alone would start one pixel in.
    std::optional<QPair<int, int>> paintedSpan(const QRect& band) const
    {
        const QList<QRgb> line = centreLine(*tree->viewport(), band);

        int left = -1;
        int right = -1;
        for (int offset = 0; offset < line.size(); ++offset) {
            if (line.at(offset) != Unpainted) {
                left = left < 0 ? band.left() + offset : left;
                right = band.left() + offset;
            }
        }
        return left < 0 ? std::optional<QPair<int, int>> {} : QPair<int, int> {left, right};
    }

    /// The cell @p column occupies on the object's row, in viewport coordinates. Taken from the
    /// view rather than from the header, so the tree's own indentation is already off it — the
    /// leading column is indented, and which column that is changes as columns are hidden.
    QRect columnRect(int column) const
    {
        return tree->visualRect(objectIndex(column));
    }


    /// The section @p column occupies, in the header viewport's coordinates.
    QRect headerSectionRect(int column) const
    {
        QHeaderView* header = tree->header();
        return {
            header->sectionViewportPosition(column),
            0,
            header->sectionSize(column),
            header->viewport()->height()
        };
    }

    std::optional<QPair<int, int>> headerBox(int column) const
    {
        // A header paints its sections on its own viewport, the way any scroll area does, so
        // that is the widget the box has to be read off.
        return colourSpan(*tree->header()->viewport(), headerSectionRect(column), HeaderColor);
    }

    Gui::TreeWidget* tree = nullptr;
    App::Document* document = nullptr;
};

QTEST_MAIN(TestTreeColumns)
#include "TreeColumns.moc"
