#include <gtest/gtest.h>
#include <Base/Exception.h>
#include <Mod/Mesh/App/Mesh.h>
#include <Mod/Mesh/App/Core/Grid.h>
#include <Mod/Mesh/App/Core/MeshIO.h>

// NOLINTBEGIN(cppcoreguidelines-*,readability-*)
TEST(MeshTest, TestDefault)
{
    MeshCore::MeshKernel kernel;
    Base::Vector3f p1 {0, 0, 0};
    Base::Vector3f p2 {0, 0, 1};
    Base::Vector3f p3 {0, 1, 0};
    kernel.AddFacet(MeshCore::MeshGeomFacet(p1, p2, p3));

    EXPECT_EQ(kernel.CountPoints(), 3);
    EXPECT_EQ(kernel.CountEdges(), 3);
    EXPECT_EQ(kernel.CountFacets(), 1);
}

TEST(MeshTest, TestGrid1OfPlanarMesh)
{
    MeshCore::MeshKernel kernel;
    Base::Vector3f p1 {0, 0, 0};
    Base::Vector3f p2 {1, 0, 0};
    Base::Vector3f p3 {0, 1, 0};
    Base::Vector3f p4 {1, 1, 0};
    kernel.AddFacet(MeshCore::MeshGeomFacet(p1, p2, p3));
    kernel.AddFacet(MeshCore::MeshGeomFacet(p3, p2, p4));

    MeshCore::MeshFacetGrid grid(kernel, 10);
    unsigned long countX {};
    unsigned long countY {};
    unsigned long countZ {};
    grid.GetCtGrids(countX, countY, countZ);
    EXPECT_EQ(countX, 1);
    EXPECT_EQ(countY, 1);
    EXPECT_EQ(countZ, 1);
}

TEST(MeshTest, TestGrid2OfPlanarMesh)
{
    MeshCore::MeshKernel kernel;
    Base::Vector3f p1 {0, 0, 0};
    Base::Vector3f p2 {1, 0, 0};
    Base::Vector3f p3 {0, 1, 0};
    Base::Vector3f p4 {1, 1, 0};
    kernel.AddFacet(MeshCore::MeshGeomFacet(p1, p2, p3));
    kernel.AddFacet(MeshCore::MeshGeomFacet(p3, p2, p4));

    MeshCore::MeshFacetGrid grid(kernel);
    unsigned long countX {};
    unsigned long countY {};
    unsigned long countZ {};
    grid.GetCtGrids(countX, countY, countZ);
    EXPECT_EQ(countX, 1);
    EXPECT_EQ(countY, 1);
    EXPECT_EQ(countZ, 1);
}

TEST(MeshTest, TestGrid1OfAlmostPlanarMesh)
{
    MeshCore::MeshKernel kernel;
    Base::Vector3f p1 {0, 0, 0};
    Base::Vector3f p2 {1, 0, 0};
    Base::Vector3f p3 {0, 1, 0};
    Base::Vector3f p4 {1, 1, 1.0e-18F};
    kernel.AddFacet(MeshCore::MeshGeomFacet(p1, p2, p3));
    kernel.AddFacet(MeshCore::MeshGeomFacet(p3, p2, p4));

    MeshCore::MeshFacetGrid grid(kernel, 10);
    unsigned long countX {};
    unsigned long countY {};
    unsigned long countZ {};
    grid.GetCtGrids(countX, countY, countZ);
    EXPECT_EQ(countX, 1);
    EXPECT_EQ(countY, 1);
    EXPECT_EQ(countZ, 1);
}

TEST(MeshTest, TestGrid2OfAlmostPlanarMesh)
{
    MeshCore::MeshKernel kernel;
    Base::Vector3f p1 {0, 0, 0};
    Base::Vector3f p2 {1, 0, 0};
    Base::Vector3f p3 {0, 1, 0};
    Base::Vector3f p4 {1, 1, 1.0e-18F};
    kernel.AddFacet(MeshCore::MeshGeomFacet(p1, p2, p3));
    kernel.AddFacet(MeshCore::MeshGeomFacet(p3, p2, p4));

    MeshCore::MeshFacetGrid grid(kernel);
    unsigned long countX {};
    unsigned long countY {};
    unsigned long countZ {};
    grid.GetCtGrids(countX, countY, countZ);
    EXPECT_EQ(countX, 1);
    EXPECT_EQ(countY, 1);
    EXPECT_EQ(countZ, 1);
}

TEST(MeshTest, TestSupportedInputFormats)
{
    std::vector<std::string> formats = MeshCore::MeshInput::supportedMeshFormats();
    EXPECT_NE(std::find(formats.begin(), formats.end(), "bms"), formats.end());
    EXPECT_NE(std::find(formats.begin(), formats.end(), "iv"), formats.end());
    EXPECT_NE(std::find(formats.begin(), formats.end(), "3mf"), formats.end());
    EXPECT_NE(std::find(formats.begin(), formats.end(), "smf"), formats.end());
    EXPECT_NE(std::find(formats.begin(), formats.end(), "off"), formats.end());
    EXPECT_NE(std::find(formats.begin(), formats.end(), "bdf"), formats.end());
    EXPECT_NE(std::find(formats.begin(), formats.end(), "nas"), formats.end());
    EXPECT_NE(std::find(formats.begin(), formats.end(), "obj"), formats.end());
    EXPECT_NE(std::find(formats.begin(), formats.end(), "ast"), formats.end());
    EXPECT_NE(std::find(formats.begin(), formats.end(), "stl"), formats.end());
    EXPECT_NE(std::find(formats.begin(), formats.end(), "ply"), formats.end());
}

TEST(MeshTest, TestInputFormat)
{
    EXPECT_EQ(MeshCore::MeshInput::getFormat("test.bms"), MeshCore::MeshIO::Format::BMS);
    EXPECT_EQ(MeshCore::MeshInput::getFormat("test.ply"), MeshCore::MeshIO::Format::PLY);
    EXPECT_EQ(MeshCore::MeshInput::getFormat("test.stl"), MeshCore::MeshIO::Format::STL);
    EXPECT_EQ(MeshCore::MeshInput::getFormat("test.ast"), MeshCore::MeshIO::Format::ASTL);
    EXPECT_EQ(MeshCore::MeshInput::getFormat("test.OBJ"), MeshCore::MeshIO::Format::OBJ);
    EXPECT_EQ(MeshCore::MeshInput::getFormat("test.nas"), MeshCore::MeshIO::Format::NAS);
    EXPECT_EQ(MeshCore::MeshInput::getFormat("test.bdf"), MeshCore::MeshIO::Format::NAS);
    EXPECT_EQ(MeshCore::MeshInput::getFormat("test.OFF"), MeshCore::MeshIO::Format::OFF);
    EXPECT_EQ(MeshCore::MeshInput::getFormat("test.smf"), MeshCore::MeshIO::Format::SMF);
    EXPECT_EQ(MeshCore::MeshInput::getFormat("test.3mf"), MeshCore::MeshIO::Format::ThreeMF);
    EXPECT_EQ(MeshCore::MeshInput::getFormat("test.iv"), MeshCore::MeshIO::Format::IV);
    EXPECT_THROW(MeshCore::MeshInput::getFormat("test.amf"), Base::FileException);
}

TEST(MeshTest, TestSupportedOutputFormats)
{
    std::vector<std::string> formats = MeshCore::MeshOutput::supportedMeshFormats();
    EXPECT_NE(std::find(formats.begin(), formats.end(), "bms"), formats.end());
    EXPECT_NE(std::find(formats.begin(), formats.end(), "iv"), formats.end());
    EXPECT_NE(std::find(formats.begin(), formats.end(), "3mf"), formats.end());
    EXPECT_NE(std::find(formats.begin(), formats.end(), "smf"), formats.end());
    EXPECT_NE(std::find(formats.begin(), formats.end(), "off"), formats.end());
    EXPECT_NE(std::find(formats.begin(), formats.end(), "bdf"), formats.end());
    EXPECT_NE(std::find(formats.begin(), formats.end(), "nas"), formats.end());
    EXPECT_NE(std::find(formats.begin(), formats.end(), "obj"), formats.end());
    EXPECT_NE(std::find(formats.begin(), formats.end(), "ast"), formats.end());
    EXPECT_NE(std::find(formats.begin(), formats.end(), "stl"), formats.end());
    EXPECT_NE(std::find(formats.begin(), formats.end(), "ply"), formats.end());
    EXPECT_NE(std::find(formats.begin(), formats.end(), "x3d"), formats.end());
    EXPECT_NE(std::find(formats.begin(), formats.end(), "x3dz"), formats.end());
    EXPECT_NE(std::find(formats.begin(), formats.end(), "xhtml"), formats.end());
    EXPECT_NE(std::find(formats.begin(), formats.end(), "vrml"), formats.end());
    EXPECT_NE(std::find(formats.begin(), formats.end(), "wrl"), formats.end());
    EXPECT_NE(std::find(formats.begin(), formats.end(), "wrz"), formats.end());
    EXPECT_NE(std::find(formats.begin(), formats.end(), "amf"), formats.end());
    EXPECT_NE(std::find(formats.begin(), formats.end(), "asy"), formats.end());
    EXPECT_NE(std::find(formats.begin(), formats.end(), "idtf"), formats.end());
    EXPECT_NE(std::find(formats.begin(), formats.end(), "mgl"), formats.end());
    EXPECT_NE(std::find(formats.begin(), formats.end(), "py"), formats.end());
}

TEST(MeshTest, TestOutputFormat)
{
    EXPECT_EQ(MeshCore::MeshOutput::GetFormat("test.bms"), MeshCore::MeshIO::Format::BMS);
    EXPECT_EQ(MeshCore::MeshOutput::GetFormat("test.ply"), MeshCore::MeshIO::Format::PLY);
    EXPECT_EQ(MeshCore::MeshOutput::GetFormat("test.stl"), MeshCore::MeshIO::Format::BSTL);
    EXPECT_EQ(MeshCore::MeshOutput::GetFormat("test.ast"), MeshCore::MeshIO::Format::ASTL);
    EXPECT_EQ(MeshCore::MeshOutput::GetFormat("test.OBJ"), MeshCore::MeshIO::Format::OBJ);
    EXPECT_EQ(MeshCore::MeshOutput::GetFormat("test.nas"), MeshCore::MeshIO::Format::NAS);
    EXPECT_EQ(MeshCore::MeshOutput::GetFormat("test.bdf"), MeshCore::MeshIO::Format::NAS);
    EXPECT_EQ(MeshCore::MeshOutput::GetFormat("test.OFF"), MeshCore::MeshIO::Format::OFF);
    EXPECT_EQ(MeshCore::MeshOutput::GetFormat("test.smf"), MeshCore::MeshIO::Format::SMF);
    EXPECT_EQ(MeshCore::MeshOutput::GetFormat("test.3mf"), MeshCore::MeshIO::Format::ThreeMF);
    EXPECT_EQ(MeshCore::MeshOutput::GetFormat("test.iv"), MeshCore::MeshIO::Format::IV);
    EXPECT_EQ(MeshCore::MeshOutput::GetFormat("test.py"), MeshCore::MeshIO::Format::PY);
    EXPECT_EQ(MeshCore::MeshOutput::GetFormat("test.X3D"), MeshCore::MeshIO::Format::X3D);
    EXPECT_EQ(MeshCore::MeshOutput::GetFormat("test.x3dz"), MeshCore::MeshIO::Format::X3DZ);
    EXPECT_EQ(MeshCore::MeshOutput::GetFormat("test.xhtml"), MeshCore::MeshIO::Format::X3DOM);
    EXPECT_EQ(MeshCore::MeshOutput::GetFormat("test.vrml"), MeshCore::MeshIO::Format::VRML);
    EXPECT_EQ(MeshCore::MeshOutput::GetFormat("test.wrl"), MeshCore::MeshIO::Format::VRML);
    EXPECT_EQ(MeshCore::MeshOutput::GetFormat("test.wrz"), MeshCore::MeshIO::Format::WRZ);
    EXPECT_EQ(MeshCore::MeshOutput::GetFormat("test.amf"), MeshCore::MeshIO::Format::AMF);
    EXPECT_EQ(MeshCore::MeshOutput::GetFormat("test.asy"), MeshCore::MeshIO::Format::ASY);
    EXPECT_EQ(MeshCore::MeshOutput::GetFormat("test.idtf"), MeshCore::MeshIO::Format::IDTF);
    EXPECT_EQ(MeshCore::MeshOutput::GetFormat("test.mgl"), MeshCore::MeshIO::Format::MGL);
    EXPECT_EQ(MeshCore::MeshOutput::GetFormat("test.uml"), MeshCore::MeshIO::Format::Undefined);
}

// NOLINTEND(cppcoreguidelines-*,readability-*)
