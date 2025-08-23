#include <gtest/gtest.h>
#include <Mod/Mesh/App/Edge.h>
#include <Mod/Mesh/App/Facet.h>
#include <Mod/Mesh/App/Mesh.h>
#include <Mod/Mesh/App/Core/Grid.h>

// NOLINTBEGIN(cppcoreguidelines-*,readability-*)
class MeshElementTest: public ::testing::Test
{
protected:
    Base::Reference<Mesh::MeshObject> getMesh() const
    {
        MeshCore::MeshKernel kernel;
        Base::Vector3f p1 {0, 0, 0};
        Base::Vector3f p2 {1, 0, 0};
        Base::Vector3f p3 {0, 1, 0};
        Base::Vector3f p4 {1, 1, 0};
        kernel.AddFacet(MeshCore::MeshGeomFacet(p1, p2, p3));
        kernel.AddFacet(MeshCore::MeshGeomFacet(p3, p2, p4));

        Base::Reference<Mesh::MeshObject> mesh(new Mesh::MeshObject(kernel));
        return mesh;
    }
};

TEST_F(MeshElementTest, TestAssignFacet)
{
    Base::Reference<Mesh::MeshObject> mesh = getMesh();

    Mesh::Facet facet = mesh->getMeshFacet(0);

    Mesh::Facet copy;
    copy = facet;

    EXPECT_TRUE(copy.isBound());
    EXPECT_EQ(copy.Index, 0);

    EXPECT_EQ(copy.PIndex[0], 0);
    EXPECT_EQ(copy.PIndex[1], 1);
    EXPECT_EQ(copy.PIndex[2], 2);

    EXPECT_EQ(copy.NIndex[0], MeshCore::FACET_INDEX_MAX);
    EXPECT_EQ(copy.NIndex[1], 1);
    EXPECT_EQ(copy.NIndex[2], MeshCore::FACET_INDEX_MAX);
}

TEST_F(MeshElementTest, TestAssignEdge)
{
    Base::Reference<Mesh::MeshObject> mesh = getMesh();

    Mesh::Facet facet = mesh->getMeshFacet(0);
    Mesh::Edge edge = facet.getEdge(1);

    Mesh::Edge copy;
    copy = edge;

    EXPECT_TRUE(copy.isBound());
    EXPECT_EQ(copy.Index, 1);

    EXPECT_EQ(copy.PIndex[0], 1);
    EXPECT_EQ(copy.PIndex[1], 2);

    EXPECT_EQ(copy.NIndex[0], 0);
    EXPECT_EQ(copy.NIndex[1], 1);
}
// NOLINTEND(cppcoreguidelines-*,readability-*)
