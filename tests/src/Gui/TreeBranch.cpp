// SPDX-License-Identifier: LGPL-2.1-or-later

#include <functional>

#include <QHeaderView>
#include <QImage>
#include <QPainter>
#include <QStandardItemModel>
#include <QStyleOptionViewItem>
#include <QTest>
#include <QTreeView>

#include "src/App/InitApplication.h"

#include <Gui/Application.h>
#include <Gui/FreeCADStyle.h>
#include <Gui/StyleParameters/ParameterManager.h>

class TestTreeBranch: public QObject
{
    Q_OBJECT

public:
    TestTreeBranch()
    {
        tests::initApplication();

        if (Gui::Application::Instance == nullptr) {
            new Gui::Application(true);
        }

        // Saturated red at full opacity so a painted pixel is unmistakable.
        Gui::Application::Instance->styleParameterManager()->addSource(
            new Gui::StyleParameters::InMemoryParameterSource(
                {
                    {.name = "TreeBranchBorderColor", .value = "#ff0000"},
                    {.name = "TreeBranchBorderThickness", .value = "1px"},
                    // Fully opaque, so anything painted under it would be lost rather than
                    // merely tinted — which is what makes the layering testable.
                    {.name = "ListRowAlternateBackground", .value = "#00ff00"},
                    // Opaque so a column that kept the resting colour is distinguishable from
                    // one the selection reached.
                    {.name = "ListRowSelectedBackground", .value = "#ff00ff"},
                    // Translucent, so a second application of the same fill lands on a
                    // different colour than the first.
                    {.name = "ListRowHoveredBackground", .value = "opacity(#0000ff, 50%)"},
                },
                {.name = "Branch Fixture"}
            )
        );
    }

private:
    // Paints one depth-1 branch cell of an otherwise ordinary tree and returns the result.
    static QImage paintCell(int cellLeft, const std::function<void(QTreeView&)>& configure = {})
    {
        QTreeView tree;
        tree.setIndentation(20);
        if (configure) {
            configure(tree);
        }

        QImage canvas(40, 24, QImage::Format_ARGB32);
        canvas.fill(Qt::transparent);

        QStyleOptionViewItem option;
        option.rect = QRect(cellLeft, 0, 20, 24);
        option.state = QStyle::State_Enabled | QStyle::State_Item | QStyle::State_Sibling;

        Gui::FreeCADStyle style;
        QPainter painter(&canvas);
        static_cast<QStyle*>(&style)->drawPrimitive(QStyle::PE_IndicatorBranch, &option, &painter, &tree);
        painter.end();

        return canvas;
    }

    static bool hasColour(const QImage& image, const QColor& colour)
    {
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                if (image.pixelColor(x, y) == colour) {
                    return true;
                }
            }
        }
        return false;
    }

private Q_SLOTS:

    // Qt paints the row, then the branch column, then the cells. An opaque row background
    // therefore has to go down first, or it buries the connectors drawn after it — which is
    // what happened to every alternate row of the property editor.
    void test_opaqueAlternateRowDoesNotEraseConnectors()  // NOLINT
    {
        QTreeView tree;
        tree.setIndentation(20);
        tree.resize(200, 100);

        QImage canvas(40, 24, QImage::Format_ARGB32);
        canvas.fill(Qt::transparent);

        QStyleOptionViewItem option;
        option.rect = QRect(20, 0, 20, 24);
        option.state = QStyle::State_Enabled | QStyle::State_Item | QStyle::State_Sibling;
        option.features |= QStyleOptionViewItem::Alternate;

        Gui::FreeCADStyle style;
        auto* asStyle = static_cast<QStyle*>(&style);
        QPainter painter(&canvas);
        asStyle->drawPrimitive(QStyle::PE_PanelItemViewRow, &option, &painter, &tree);
        asStyle->drawPrimitive(QStyle::PE_IndicatorBranch, &option, &painter, &tree);
        asStyle->drawPrimitive(QStyle::PE_PanelItemViewItem, &option, &painter, &tree);
        painter.end();

        QCOMPARE(canvas.pixelColor(30, 0), QColor(Qt::red));
        QVERIFY(hasColour(canvas, QColor(Qt::green)));
    }

    // A multi-column view emits the row surface once per column, interleaved with the cells,
    // so column 1's surface is painted after column 0's label. It must stay inside its own
    // column, or an opaque surface wipes out the text a neighbour already drew.
    void test_laterColumnSurfaceDoesNotReachIntoAnEarlierOne()  // NOLINT
    {
        QTreeView tree;
        tree.setIndentation(20);
        tree.resize(200, 100);

        QImage canvas(40, 24, QImage::Format_ARGB32);
        canvas.fill(Qt::transparent);

        Gui::FreeCADStyle style;
        auto* asStyle = static_cast<QStyle*>(&style);
        QPainter painter(&canvas);

        QStyleOptionViewItem firstColumn;
        firstColumn.rect = QRect(0, 0, 20, 24);
        firstColumn.state = QStyle::State_Enabled;
        firstColumn.features |= QStyleOptionViewItem::Alternate;
        asStyle->drawPrimitive(QStyle::PE_PanelItemViewRow, &firstColumn, &painter, &tree);

        // Stands in for the label the first column's cell draws onto its own surface.
        painter.fillRect(QRect(4, 4, 6, 6), Qt::blue);

        QStyleOptionViewItem secondColumn = firstColumn;
        secondColumn.rect = QRect(20, 0, 20, 24);
        asStyle->drawPrimitive(QStyle::PE_PanelItemViewRow, &secondColumn, &painter, &tree);
        painter.end();

        QCOMPARE(canvas.pixelColor(5, 5), QColor(Qt::blue));
    }

    // Qt strips the selection from the row emission that precedes each cell but leaves the hover
    // on it, so a style that fills from both that emission and the cell composites the hover
    // twice over the cell and once over the branch gutter — one row, two shades.
    void test_hoverFillsTheRowExactlyOnce()  // NOLINT
    {
        QTreeView tree;
        tree.setIndentation(20);
        tree.resize(200, 100);

        QImage canvas(40, 24, QImage::Format_ARGB32);
        canvas.fill(Qt::white);

        Gui::FreeCADStyle style;
        auto* asStyle = static_cast<QStyle*>(&style);
        QPainter painter(&canvas);

        // The gutter, then the one cell: what Qt emits for a hovered row of a one-column tree.
        QStyleOptionViewItem gutter;
        gutter.rect = QRect(0, 0, 20, 24);
        gutter.state = QStyle::State_Enabled | QStyle::State_MouseOver;

        QStyleOptionViewItem cell = gutter;
        cell.rect = QRect(20, 0, 20, 24);
        cell.viewItemPosition = QStyleOptionViewItem::OnlyOne;

        asStyle->drawPrimitive(QStyle::PE_PanelItemViewRow, &gutter, &painter, &tree);
        asStyle->drawPrimitive(QStyle::PE_PanelItemViewRow, &cell, &painter, &tree);
        asStyle->drawPrimitive(QStyle::PE_PanelItemViewItem, &cell, &painter, &tree);
        painter.end();

        const QColor overGutter = canvas.pixelColor(10, 12);
        const QColor overCell = canvas.pixelColor(30, 12);

        QCOMPARE(overGutter, overCell);
        QCOMPARE(overCell, QColor(127, 127, 255));
    }

    // Qt marks only the current cell with State_HasFocus, so a row's surface is asked for under
    // a different state in one column than in its neighbours. The resting look must not depend
    // on it, or the cell the user last clicked loses the alternating background its row has.
    void test_alternateSurfaceIgnoresPerCellState()  // NOLINT
    {
        QTreeView tree;
        tree.resize(200, 100);

        Gui::FreeCADStyle style;
        auto* asStyle = static_cast<QStyle*>(&style);

        const auto surfaceUnder = [&](QStyle::State state) {
            QImage canvas(40, 24, QImage::Format_ARGB32);
            canvas.fill(Qt::transparent);

            QStyleOptionViewItem option;
            option.rect = QRect(0, 0, 40, 24);
            option.state = QStyle::State_Enabled | state;
            option.features |= QStyleOptionViewItem::Alternate;

            QPainter painter(&canvas);
            asStyle->drawPrimitive(QStyle::PE_PanelItemViewRow, &option, &painter, &tree);
            painter.end();

            return canvas.pixelColor(20, 12);
        };

        QCOMPARE(surfaceUnder(QStyle::State_None), QColor(Qt::green));
        QCOMPARE(surfaceUnder(QStyle::State_HasFocus), QColor(Qt::green));
        QCOMPARE(surfaceUnder(QStyle::State_MouseOver), QColor(Qt::green));
    }

    // Qt emits the row surface once per column, each immediately before that column's cell, and
    // strips the selection from every one of them. A cell that leaves its own selection fill to
    // an earlier column therefore loses it to the surface emitted in between.
    void test_selectionReachesEveryColumn()  // NOLINT
    {
        QTreeView tree;
        tree.setSelectionBehavior(QAbstractItemView::SelectRows);
        tree.resize(200, 100);

        QImage canvas(40, 24, QImage::Format_ARGB32);
        canvas.fill(Qt::transparent);

        Gui::FreeCADStyle style;
        auto* asStyle = static_cast<QStyle*>(&style);
        QPainter painter(&canvas);

        const QList<QPair<int, QStyleOptionViewItem::ViewItemPosition>> columns = {
            {0, QStyleOptionViewItem::Beginning},
            {20, QStyleOptionViewItem::End},
        };

        for (const auto& [left, position] : columns) {
            QStyleOptionViewItem cell;
            cell.rect = QRect(left, 0, 20, 24);
            cell.state = QStyle::State_Enabled | QStyle::State_Selected;
            cell.features |= QStyleOptionViewItem::Alternate;
            cell.viewItemPosition = position;

            QStyleOptionViewItem surface = cell;
            surface.state &= ~QStyle::State_Selected;

            asStyle->drawPrimitive(QStyle::PE_PanelItemViewRow, &surface, &painter, &tree);
            asStyle->drawPrimitive(QStyle::PE_PanelItemViewItem, &cell, &painter, &tree);
        }
        painter.end();

        QCOMPARE(canvas.pixelColor(10, 12), QColor(Qt::magenta));
        QCOMPARE(canvas.pixelColor(30, 12), QColor(Qt::magenta));
    }

    // The token colour reaches the pen, on the pixel column the geometry names.
    void test_tokenColourReachesTheStroke()  // NOLINT
    {
        const QImage painted = paintCell(20);

        QCOMPARE(painted.pixelColor(30, 0), QColor(Qt::red));
    }

    // A tree may decline connectors without the theme knowing about it.
    void test_widgetPropertySuppressesConnectors()  // NOLINT
    {
        const QImage painted = paintCell(20, [](QTreeView& tree) {
            tree.setProperty("branches", false);
        });

        QVERIFY(!hasColour(painted, QColor(Qt::red)));
    }

    // A root item has no parent to reach toward.
    void test_topLevelCellDrawsNothing()  // NOLINT
    {
        const QImage painted = paintCell(0);

        QVERIFY(!hasColour(painted, QColor(Qt::red)));
    }

    // Root decorations hidden means the leading cell is depth 1, which must still draw.
    void test_leadingCellDrawsWhenRootIsNotDecorated()  // NOLINT
    {
        const QImage painted = paintCell(0, [](QTreeView& tree) { tree.setRootIsDecorated(false); });

        QCOMPARE(painted.pixelColor(10, 0), QColor(Qt::red));
    }

    // Scrolling the tree column right by one indentation step moves a depth-1 ancestor's
    // cell to x == 0 -- the same pixel column an unscrolled root cell would occupy. That
    // ancestor still has a parent to reach toward and must keep drawing its guide; only the
    // tree column's own (now off-screen) position identifies the true root cell.
    void test_leadingVisibleGuideDrawsWhenViewIsScrolledHorizontally()  // NOLINT
    {
        QStandardItemModel model(3, 1);
        QTreeView tree;
        tree.setIndentation(20);
        tree.setModel(&model);
        tree.header()->setOffset(20);

        QImage canvas(40, 24, QImage::Format_ARGB32);
        canvas.fill(Qt::transparent);

        QStyleOptionViewItem option;
        option.rect = QRect(0, 0, 20, 24);
        option.state = QStyle::State_Enabled | QStyle::State_Sibling;

        Gui::FreeCADStyle style;
        QPainter painter(&canvas);
        static_cast<QStyle*>(&style)->drawPrimitive(QStyle::PE_IndicatorBranch, &option, &painter, &tree);
        painter.end();

        QCOMPARE(canvas.pixelColor(10, 0), QColor(Qt::red));
    }
};

QTEST_MAIN(TestTreeBranch)

#include "TreeBranch.moc"
