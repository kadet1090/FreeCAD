// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>

#include <Inventor/SbBox3f.h>
#include <Inventor/SbMatrix.h>
#include <Inventor/SbRotation.h>
#include <Inventor/SbSphere.h>
#include <Inventor/SbVec3f.h>
#include <Inventor/SbViewVolume.h>
#include <Inventor/nodes/SoOrthographicCamera.h>

#include <Gui/Camera.h>
#include <Gui/SoFCDB.h>
#include <Gui/Thumbnail.h>

#include <src/App/InitApplication.h>

namespace
{
using Gui::Thumbnail;

std::array<SbVec3f, 8> cornersOf(const SbBox3f& box)
{
    const SbVec3f low = box.getMin();
    const SbVec3f high = box.getMax();
    return {
        SbVec3f(low[0], low[1], low[2]),
        SbVec3f(high[0], low[1], low[2]),
        SbVec3f(low[0], high[1], low[2]),
        SbVec3f(high[0], high[1], low[2]),
        SbVec3f(low[0], low[1], high[2]),
        SbVec3f(high[0], low[1], high[2]),
        SbVec3f(low[0], high[1], high[2]),
        SbVec3f(high[0], high[1], high[2]),
    };
}

/// How far the furthest corner of @p box reaches towards the edge of the frame @p camera
/// currently describes, in normalized device coordinates where the edge is exactly 1.
float largestNormalizedExtent(const SoOrthographicCamera& camera, const SbBox3f& box, float aspect)
{
    const SbMatrix toNormalized = camera.getViewVolume(aspect).getMatrix();

    float largest = 0.0F;
    for (const SbVec3f& corner : cornersOf(box)) {
        SbVec3f normalized;
        toNormalized.multVecMatrix(corner, normalized);
        largest = std::max({largest, std::abs(normalized[0]), std::abs(normalized[1])});
    }
    return largest;
}

/// How far the furthest corner of @p box reaches towards the near or far clipping plane, in
/// normalized device coordinates where either plane is exactly 1.
float largestNormalizedDepth(const SoOrthographicCamera& camera, const SbBox3f& box, float aspect)
{
    const SbMatrix toNormalized = camera.getViewVolume(aspect).getMatrix();

    float largest = 0.0F;
    for (const SbVec3f& corner : cornersOf(box)) {
        SbVec3f normalized;
        toNormalized.multVecMatrix(corner, normalized);
        largest = std::max(largest, std::abs(normalized[2]));
    }
    return largest;
}

class ThumbnailCameraTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
        // Constructing any Coin node needs the node type database. SoFCDB::init() is not
        // idempotent — it calls initClass() on every type unconditionally — so the caller
        // guards, as ApplicationPy and PropertyEditorSizing already do.
        if (!Gui::SoFCDB::isInitialized()) {
            Gui::SoFCDB::init();
        }
    }

    void SetUp() override
    {
        camera = new SoOrthographicCamera;
        camera->ref();
    }

    void TearDown() override
    {
        camera->unref();
    }

    SoOrthographicCamera* camera {nullptr};
};

}  // namespace

TEST_F(ThumbnailCameraTest, aCubeIsFramedWithTheMargin)
{
    // The default orientation looks down -Z, so camera space is world space and the numbers
    // can be read straight off the box.
    const SbBox3f box(-1.0F, -1.0F, -1.0F, 1.0F, 1.0F, 1.0F);

    Thumbnail::fitToBox(*camera, box, 1.0F);

    EXPECT_FLOAT_EQ(camera->height.getValue(), 2.0F * Thumbnail::fitMargin);

    const SbVec3f position = camera->position.getValue();
    EXPECT_FLOAT_EQ(position[0], 0.0F);
    EXPECT_FLOAT_EQ(position[1], 0.0F);
    EXPECT_FLOAT_EQ(position[2], std::sqrt(3.0F));
}

TEST_F(ThumbnailCameraTest, noCornerIsClipped)
{
    // A different orientation and a non-square frame from the strict margin test below, so this
    // covers a configuration that one does not.
    camera->orientation.setValue(Gui::Camera::rotation(Gui::Camera::Dimetric));
    const SbBox3f box(-30.0F, -8.0F, -12.0F, 70.0F, 22.0F, 5.0F);

    Thumbnail::fitToBox(*camera, box, 1.6F);

    EXPECT_LE(largestNormalizedExtent(*camera, box, 1.6F), 1.0F);
}

TEST_F(ThumbnailCameraTest, theClippingPlanesClearTheScene)
{
    // The isometric view direction runs parallel to a cube's body diagonal, so a cubic box puts
    // its nearest and farthest corners hard against tangent clipping planes. They need headroom,
    // because geometry excluded from the bounding box is still rendered and still depth-clipped.
    camera->orientation.setValue(Gui::Camera::rotation(Gui::Camera::Isometric));
    const SbBox3f box(-1.0F, -1.0F, -1.0F, 1.0F, 1.0F, 1.0F);

    Thumbnail::fitToBox(*camera, box, 1.0F);

    EXPECT_LT(largestNormalizedDepth(*camera, box, 1.0F), 1.0F);
}

TEST_F(ThumbnailCameraTest, theContentReachesTheMargin)
{
    camera->orientation.setValue(Gui::Camera::rotation(Gui::Camera::Isometric));
    const SbBox3f box(-30.0F, -8.0F, -12.0F, 70.0F, 22.0F, 5.0F);

    Thumbnail::fitToBox(*camera, box, 1.0F);

    // A circumscribing-sphere fit leaves this well short of the margin.
    EXPECT_NEAR(largestNormalizedExtent(*camera, box, 1.0F), 1.0F / Thumbnail::fitMargin, 1e-4F);
}

TEST_F(ThumbnailCameraTest, aTranslatedBoxFramesIdentically)
{
    camera->orientation.setValue(Gui::Camera::rotation(Gui::Camera::Isometric));
    const SbBox3f box(-1.0F, -2.0F, -3.0F, 4.0F, 5.0F, 6.0F);
    const SbVec3f offset(100.0F, -50.0F, 25.0F);
    const SbBox3f moved(box.getMin() + offset, box.getMax() + offset);

    Thumbnail::fitToBox(*camera, box, 1.0F);
    const float heightAtOrigin = camera->height.getValue();
    const SbVec3f positionAtOrigin = camera->position.getValue();

    Thumbnail::fitToBox(*camera, moved, 1.0F);
    const SbVec3f movedPosition = camera->position.getValue();

    EXPECT_FLOAT_EQ(camera->height.getValue(), heightAtOrigin);
    EXPECT_NEAR(movedPosition[0], positionAtOrigin[0] + offset[0], 1e-3F);
    EXPECT_NEAR(movedPosition[1], positionAtOrigin[1] + offset[1], 1e-3F);
    EXPECT_NEAR(movedPosition[2], positionAtOrigin[2] + offset[2], 1e-3F);
}

TEST_F(ThumbnailCameraTest, anElongatedRodBeatsTheSphereFit)
{
    camera->orientation.setValue(Gui::Camera::rotation(Gui::Camera::Isometric));
    const SbBox3f rod(-0.5F, -0.5F, -50.0F, 0.5F, 0.5F, 50.0F);

    Thumbnail::fitToBox(*camera, rod, 1.0F);

    SbSphere circumscribed;
    circumscribed.circumscribe(rod);

    // Coin's viewBoundingBox would hand the rod a frame of 2 * radius and leave it swimming.
    EXPECT_LT(camera->height.getValue(), 2.0F * circumscribed.getRadius());
    EXPECT_NEAR(largestNormalizedExtent(*camera, rod, 1.0F), 1.0F / Thumbnail::fitMargin, 1e-4F);
}

TEST_F(ThumbnailCameraTest, theAspectRatioDecidesWhichExtentConstrains)
{
    // Default orientation again: half extents are 2 across and 1 up in camera space.
    const SbBox3f box(-2.0F, -1.0F, -1.0F, 2.0F, 1.0F, 1.0F);

    Thumbnail::fitToBox(*camera, box, 1.0F);
    EXPECT_FLOAT_EQ(camera->height.getValue(), 4.0F * Thumbnail::fitMargin);

    Thumbnail::fitToBox(*camera, box, 2.0F);
    EXPECT_FLOAT_EQ(camera->height.getValue(), 2.0F * Thumbnail::fitMargin);

    Thumbnail::fitToBox(*camera, box, 0.5F);
    EXPECT_FLOAT_EQ(camera->height.getValue(), 8.0F * Thumbnail::fitMargin);
}

TEST_F(ThumbnailCameraTest, anEmptyBoxLeavesTheCameraAlone)
{
    camera->orientation.setValue(Gui::Camera::rotation(Gui::Camera::Isometric));
    camera->position.setValue(7.0F, 8.0F, 9.0F);
    camera->height.setValue(42.0F);

    Thumbnail::fitToBox(*camera, SbBox3f(), 1.0F);

    EXPECT_FLOAT_EQ(camera->height.getValue(), 42.0F);

    const SbVec3f position = camera->position.getValue();
    EXPECT_FLOAT_EQ(position[0], 7.0F);
    EXPECT_FLOAT_EQ(position[1], 8.0F);
    EXPECT_FLOAT_EQ(position[2], 9.0F);
}

TEST_F(ThumbnailCameraTest, aPointBoxLeavesTheCameraAlone)
{
    camera->orientation.setValue(Gui::Camera::rotation(Gui::Camera::Isometric));
    camera->position.setValue(7.0F, 8.0F, 9.0F);
    camera->height.setValue(42.0F);

    // A single-point scene is a valid bounding box, not an empty one: Base::BoundBox3::IsValid
    // is Min <= Max, so getSceneBoundBox hands this straight through.
    Thumbnail::fitToBox(*camera, SbBox3f(3.0F, 4.0F, 5.0F, 3.0F, 4.0F, 5.0F), 1.0F);

    EXPECT_FLOAT_EQ(camera->height.getValue(), 42.0F);

    const SbVec3f position = camera->position.getValue();
    EXPECT_FLOAT_EQ(position[0], 7.0F);
    EXPECT_FLOAT_EQ(position[1], 8.0F);
    EXPECT_FLOAT_EQ(position[2], 9.0F);
}
