// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <QLine>
#include <QList>
#include <QRect>
#include <QStyle>

#include <Gui/FreeCADStyle.h>

namespace
{

// One indent cell of a 20px-indented tree on a 24px row, at depth 1.
constexpr QRect cell(20, 48, 20, 24);

// Centres land on half-pixel coordinates so a 1px stroke covers one pixel column
// rather than straddling two.
constexpr qreal centerX = 30.5;
constexpr qreal centerY = 60.5;

QList<QLineF> segmentsFor(
    QStyle::State state,
    bool topLevel = false,
    Qt::LayoutDirection direction = Qt::LeftToRight,
    int leadingGap = 0
)
{
    return Gui::FreeCADStyle::branchSegments(cell, state, topLevel, direction, leadingGap);
}

}  // namespace

// A non-last child continues the vertical past its own row and elbows into the item.
TEST(TreeBranchGeometryTest, ItemWithFollowingSiblingDrawsThroughVerticalAndStub)
{
    const QList<QLineF> segments = segmentsFor(QStyle::State_Item | QStyle::State_Sibling);

    ASSERT_EQ(segments.size(), 2);
    EXPECT_EQ(segments.at(0), QLineF(centerX, 48, centerX, 72));
    EXPECT_EQ(segments.at(1), QLineF(centerX, centerY, 40, centerY));
}

// A last child closes the vertical at the elbow instead of running it to the next row.
TEST(TreeBranchGeometryTest, LastChildStopsTheVerticalAtTheElbow)
{
    const QList<QLineF> segments = segmentsFor(QStyle::State_Item);

    ASSERT_EQ(segments.size(), 2);
    EXPECT_EQ(segments.at(0), QLineF(centerX, 48, centerX, centerY));
    EXPECT_EQ(segments.at(1), QLineF(centerX, centerY, 40, centerY));
}

// An ancestor level carries the guide past rows that are not its own children.
TEST(TreeBranchGeometryTest, AncestorWithSiblingDrawsOnlyTheGuide)
{
    const QList<QLineF> segments = segmentsFor(QStyle::State_Sibling);

    ASSERT_EQ(segments.size(), 1);
    EXPECT_EQ(segments.at(0), QLineF(centerX, 48, centerX, 72));
}

// Past the last child of a level there is nothing left to connect.
TEST(TreeBranchGeometryTest, ExhaustedAncestorDrawsNothing)
{
    EXPECT_TRUE(segmentsFor(QStyle::State_None).isEmpty());
}

// A root has no parent to reach toward, so its cell stays empty whatever the flags say.
TEST(TreeBranchGeometryTest, TopLevelCellDrawsNothing)
{
    EXPECT_TRUE(segmentsFor(QStyle::State_Item | QStyle::State_Sibling, true).isEmpty());
    EXPECT_TRUE(segmentsFor(QStyle::State_Sibling, true).isEmpty());
}

// In a right-to-left layout the item's own cell is the leftmost of the branch cells, so
// the elbow's stub must reach the cell's left edge rather than its right edge.
TEST(TreeBranchGeometryTest, RightToLeftStubReachesTheLeftEdge)
{
    const QList<QLineF> segments = segmentsFor(QStyle::State_Item, /*topLevel=*/false, Qt::RightToLeft);

    ASSERT_EQ(segments.size(), 2);
    EXPECT_EQ(segments.at(0), QLineF(centerX, 48, centerX, centerY));
    EXPECT_EQ(segments.at(1), QLineF(centerX, centerY, 20, centerY));
}

// Left-to-right keeps the stub pointed at the right edge, unaffected by the new parameter.
TEST(TreeBranchGeometryTest, LeftToRightStubReachesTheRightEdge)
{
    const QList<QLineF> segments = segmentsFor(QStyle::State_Item, /*topLevel=*/false, Qt::LeftToRight);

    ASSERT_EQ(segments.size(), 2);
    EXPECT_EQ(segments.at(1), QLineF(centerX, centerY, 40, centerY));
}

// An expand arrow occupies the centre, so the strokes stop short of it and leave it clear.
TEST(TreeBranchGeometryTest, ArrowCellLeavesTheCentreClear)
{
    const QList<QLineF> segments = segmentsFor(
        QStyle::State_Item | QStyle::State_Sibling | QStyle::State_Children
    );

    ASSERT_EQ(segments.size(), 3);
    EXPECT_EQ(segments.at(0), QLineF(centerX, 48, centerX, centerY - 5.0));
    EXPECT_EQ(segments.at(1), QLineF(centerX, centerY + 5.0, centerX, 72));
    EXPECT_EQ(segments.at(2), QLineF(centerX + 5.0, centerY, 40, centerY));
}

// The gap above a row belongs to the row before it: the elbow follows the item box down,
// while the guide still spans the whole cell so it meets its neighbours.
TEST(TreeBranchGeometryTest, LeadingGapLowersTheElbowButNotTheGuide)
{
    constexpr int leadingGap = 4;
    constexpr qreal loweredCenterY = 62.5;

    const QList<QLineF> segments
        = segmentsFor(QStyle::State_Item | QStyle::State_Sibling, false, Qt::LeftToRight, leadingGap);

    ASSERT_EQ(segments.size(), 2);
    EXPECT_EQ(segments.at(0), QLineF(centerX, 48, centerX, 72));
    EXPECT_EQ(segments.at(1), QLineF(centerX, loweredCenterY, 40, loweredCenterY));
}
