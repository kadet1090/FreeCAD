// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <src/App/InitApplication.h>

#include <Gui/SoFCDB.h>
#include <Gui/ViewProviderDocumentObject.h>

namespace
{
/// Records what the reveal contract asks of a view provider without touching the scene
/// graph, so the counting can be exercised with no 3D view anywhere.
class CountingViewProvider: public Gui::ViewProviderDocumentObject
{
public:
    int reveals = 0;
    int conceals = 0;

protected:
    void onTemporaryVisibilityChanged(bool visible) override
    {
        visible ? ++reveals : ++conceals;
    }
};

class TemporaryVisibilityTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
        // A view provider builds Coin nodes in its constructor, FreeCAD's own among them.
        // SoFCDB::init() is not idempotent, and this binary now has a second Coin-using suite.
        if (!Gui::SoFCDB::isInitialized()) {
            Gui::SoFCDB::init();
        }
    }
};
}  // namespace

TEST_F(TemporaryVisibilityTest, aRevealIsAnnouncedOnce)
{
    CountingViewProvider viewProvider;

    viewProvider.makeTemporaryVisible(true);

    EXPECT_EQ(viewProvider.reveals, 1);
    EXPECT_EQ(viewProvider.conceals, 0);
}

TEST_F(TemporaryVisibilityTest, overlappingRevealsNest)
{
    CountingViewProvider viewProvider;

    viewProvider.makeTemporaryVisible(true);
    viewProvider.makeTemporaryVisible(true);
    viewProvider.makeTemporaryVisible(false);

    // The second reveal is still outstanding, so the object may not be put back yet.
    EXPECT_EQ(viewProvider.reveals, 1);
    EXPECT_EQ(viewProvider.conceals, 0);

    viewProvider.makeTemporaryVisible(false);

    EXPECT_EQ(viewProvider.conceals, 1);
}

TEST_F(TemporaryVisibilityTest, aRevealCanBeTakenUpAgainAfterItEnded)
{
    CountingViewProvider viewProvider;

    viewProvider.makeTemporaryVisible(true);
    viewProvider.makeTemporaryVisible(false);
    viewProvider.makeTemporaryVisible(true);

    EXPECT_EQ(viewProvider.reveals, 2);
    EXPECT_EQ(viewProvider.conceals, 1);
}

TEST_F(TemporaryVisibilityTest, aConcealNobodyAskedForChangesNothing)
{
    CountingViewProvider viewProvider;

    // Whoever owns the object's visibility has already put it where it belongs; a stray
    // conceal must not overrule that, nor leave the count owing a reveal.
    viewProvider.makeTemporaryVisible(false);
    viewProvider.makeTemporaryVisible(false);

    EXPECT_EQ(viewProvider.conceals, 0);

    viewProvider.makeTemporaryVisible(true);

    EXPECT_EQ(viewProvider.reveals, 1);
}
