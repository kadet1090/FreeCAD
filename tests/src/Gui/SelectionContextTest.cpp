// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <map>
#include <string>

#include <Base/Color.h>
#include <Gui/Selection/SoFCSelectionContext.h>

using Gui::SoFCSelectionContextEx;

// setColors() maps an element NAME onto the coordinate/part index the shape node
// draws with, so the number in the name has to be read from just past the prefix —
// whatever that prefix's length happens to be.
TEST(SoFCSelectionContextExTest, mapsAFourLetterPrefixOntoAZeroBasedIndex)
{
    SoFCSelectionContextEx context;

    const std::map<std::string, Base::Color> colors {
        {"Face3", Base::Color(1.0F, 0.0F, 0.0F, 1.0F)},
    };
    ASSERT_TRUE(context.setColors(colors, "Face"));

    ASSERT_EQ(context.colors.size(), 1U);
    EXPECT_EQ(context.colors.begin()->first, 2);
}

TEST(SoFCSelectionContextExTest, mapsASixLetterPrefixOntoAZeroBasedIndex)
{
    SoFCSelectionContextEx context;

    const std::map<std::string, Base::Color> colors {
        {"Vertex3", Base::Color(1.0F, 0.0F, 0.0F, 1.0F)},
    };
    ASSERT_TRUE(context.setColors(colors, "Vertex"));

    // "Vertex3" read from offset 4 is "x3", which atoi() reports as 0 and the
    // index guard then discards — the colour disappears with no diagnostic.
    ASSERT_EQ(context.colors.size(), 1U);
    EXPECT_EQ(context.colors.begin()->first, 2);
}

TEST(SoFCSelectionContextExTest, aBarePrefixBecomesTheWholeObjectWildcard)
{
    SoFCSelectionContextEx context;

    const std::map<std::string, Base::Color> colors {
        {"Vertex", Base::Color(0.0F, 1.0F, 0.0F, 1.0F)},
    };
    ASSERT_TRUE(context.setColors(colors, "Vertex"));

    ASSERT_EQ(context.colors.size(), 1U);
    EXPECT_EQ(context.colors.begin()->first, -1);
}

TEST(SoFCSelectionContextExTest, keepsOnlyTheEntriesMatchingTheAskedPrefix)
{
    SoFCSelectionContextEx context;

    const std::map<std::string, Base::Color> colors {
        {"Edge5", Base::Color(0.0F, 0.0F, 1.0F, 1.0F)},
        {"Face3", Base::Color(1.0F, 0.0F, 0.0F, 1.0F)},
        {"Vertex7", Base::Color(0.0F, 1.0F, 0.0F, 1.0F)},
    };
    ASSERT_TRUE(context.setColors(colors, "Vertex"));

    ASSERT_EQ(context.colors.size(), 1U);
    EXPECT_EQ(context.colors.begin()->first, 6);
}

// The alpha the caller authored is what reaches the renderer, and it arrives as
// Coin's transparency — the complement.
TEST(SoFCSelectionContextExTest, packColorTurnsAlphaIntoTransparency)
{
    SoFCSelectionContextEx context;
    bool hasTransparency = false;

    const uint32_t packed = context.packColor(Base::Color(1.0F, 0.0F, 0.0F, 0.25F), hasTransparency);

    EXPECT_TRUE(hasTransparency);
    EXPECT_EQ(packed & 0xFFU, 64U);  // round(0.25 * 255) == 64
}

TEST(SoFCSelectionContextExTest, packColorLeavesAnOpaqueColourOpaque)
{
    SoFCSelectionContextEx context;
    bool hasTransparency = false;

    const uint32_t packed = context.packColor(Base::Color(1.0F, 0.0F, 0.0F, 1.0F), hasTransparency);

    EXPECT_FALSE(hasTransparency);
    EXPECT_EQ(packed & 0xFFU, 255U);
}
