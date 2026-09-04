// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <src/App/InitApplication.h>

#include <Gui/SoFCDB.h>
#include <Gui/ViewProviderDocumentObject.h>

namespace
{
class ViewProviderTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
        // A view provider builds Coin nodes in its constructor. SoFCDB::init() is not
        // idempotent, hence the guard — see TemporaryVisibilityTest for the same pattern.
        if (!Gui::SoFCDB::isInitialized()) {
            Gui::SoFCDB::init();
        }
    }
};
}  // namespace

TEST_F(ViewProviderTest, mapElementNameForColorIsIdentityByDefault)
{
    Gui::ViewProviderDocumentObject viewProvider;

    // The base implementation is the fallback every provider gets unless it overrides:
    // an element already named in the shape nodes' own namespace must round-trip
    // unchanged, whether or not it names a real sub-element.
    EXPECT_EQ(viewProvider.mapElementNameForColor("Face3"), "Face3");
    EXPECT_EQ(viewProvider.mapElementNameForColor("Edge10"), "Edge10");
    EXPECT_EQ(viewProvider.mapElementNameForColor(""), "");
    EXPECT_EQ(viewProvider.mapElementNameForColor("InternalFace4"), "InternalFace4");
}
