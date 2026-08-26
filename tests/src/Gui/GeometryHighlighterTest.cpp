// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <src/App/InitApplication.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Gui/GeometryHighlighter.h>

using Gui::GeometryHighlightModel;
using Gui::GeometryReference;
using Gui::HighlightRole;

namespace
{
class GeometryHighlighterTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        App::DocumentInitFlags createFlags;
        createFlags.createView = false;
        _docName = App::GetApplication().getUniqueDocumentName("geomhl_test");
        _doc = App::GetApplication().newDocument(_docName.c_str(), "testUser", createFlags);
        _objectA = _doc->addObject("App::FeatureTest", "ObjectA");
        _objectB = _doc->addObject("App::FeatureTest", "ObjectB");
    }

    void TearDown() override
    {
        if (App::GetApplication().getDocument(_docName.c_str())) {
            App::GetApplication().closeDocument(_docName.c_str());
        }
    }

    std::string _docName;
    App::Document* _doc {};
    App::DocumentObject* _objectA {};
    App::DocumentObject* _objectB {};
};
}  // namespace

TEST_F(GeometryHighlighterTest, setHighlightedReplacesRatherThanAccumulates)
{
    GeometryHighlightModel model;

    model.setHighlighted(HighlightRole::Reference, {{.object = _objectA, .subName = "Face1"}});
    model.setHighlighted(HighlightRole::Reference, {{.object = _objectB, .subName = "Face2"}});

    const std::vector<GeometryReference> effective = model.effective(HighlightRole::Reference);
    ASSERT_EQ(effective.size(), 1U);
    EXPECT_EQ(effective.front().object, _objectB);
    EXPECT_EQ(effective.front().subName, "Face2");
}

TEST_F(GeometryHighlighterTest, hoveredReferenceIsExcludedFromReferenceRole)
{
    GeometryHighlightModel model;
    const GeometryReference first {.object = _objectA, .subName = "Face1"};
    const GeometryReference second {.object = _objectB, .subName = "Face2"};

    model.setHighlighted(HighlightRole::Reference, {first, second});
    model.setHighlighted(HighlightRole::Hovered, {second});

    const std::vector<GeometryReference> effective = model.effective(HighlightRole::Reference);
    ASSERT_EQ(effective.size(), 1U);
    EXPECT_EQ(effective.front(), first);
    EXPECT_EQ(model.effective(HighlightRole::Hovered), std::vector {second});
}

TEST_F(GeometryHighlighterTest, clearingHoverRestoresTheReference)
{
    GeometryHighlightModel model;
    const GeometryReference only {.object = _objectA, .subName = "Face1"};

    model.setHighlighted(HighlightRole::Reference, {only});
    model.setHighlighted(HighlightRole::Hovered, {only});
    ASSERT_TRUE(model.effective(HighlightRole::Reference).empty());

    model.clear(HighlightRole::Hovered);

    EXPECT_EQ(model.effective(HighlightRole::Reference), std::vector {only});
}

TEST_F(GeometryHighlighterTest, dropObjectRemovesItFromEveryRole)
{
    GeometryHighlightModel model;
    const GeometryReference doomed {.object = _objectA, .subName = "Face1"};
    const GeometryReference survivor {.object = _objectB, .subName = "Face2"};

    model.setHighlighted(HighlightRole::Reference, {doomed, survivor});
    model.setHighlighted(HighlightRole::Hovered, {doomed});

    model.dropObject(_objectA);

    EXPECT_EQ(model.effective(HighlightRole::Reference), std::vector {survivor});
    EXPECT_TRUE(model.effective(HighlightRole::Hovered).empty());
}

TEST_F(GeometryHighlighterTest, dropDocumentRemovesEveryReferenceInThatDocument)
{
    App::DocumentInitFlags createFlags;
    createFlags.createView = false;
    const std::string otherName = App::GetApplication().getUniqueDocumentName("geomhl_other");
    App::Document* otherDocument
        = App::GetApplication().newDocument(otherName.c_str(), "testUser", createFlags);
    App::DocumentObject* otherObject = otherDocument->addObject("App::FeatureTest", "ObjectC");
    const GeometryReference survivor {.object = otherObject, .subName = "Face3"};

    GeometryHighlightModel model;
    model.setHighlighted(
        HighlightRole::Reference,
        {{.object = _objectA, .subName = "Face1"}, {.object = _objectB, .subName = "Face2"}, survivor}
    );

    model.dropDocument(_doc);

    // Only the closed document's references go; another document's are untouched.
    EXPECT_EQ(model.effective(HighlightRole::Reference), std::vector {survivor});

    App::GetApplication().closeDocument(otherName.c_str());
}

TEST_F(GeometryHighlighterTest, clearEmptiesEveryRole)
{
    GeometryHighlightModel model;

    model.setHighlighted(HighlightRole::Reference, {{.object = _objectA, .subName = "Face1"}});
    model.setHighlighted(HighlightRole::Hovered, {{.object = _objectB, .subName = "Face2"}});

    model.clear();

    EXPECT_TRUE(model.effective(HighlightRole::Reference).empty());
    EXPECT_TRUE(model.effective(HighlightRole::Hovered).empty());
}

#include <memory>
#include <string>

#include <QDir>
#include <QString>
#include <QStringList>

#include <Gui/StyleParameters.h>
#include <Gui/StyleParameters/ParameterManager.h>

// Guards the compiled-in defaults in StyleParameters.h, not the authored theme. A bare
// ParameterManager with no source added has nothing to resolve these six names against,
// so resolve(ParameterDefinition<T>) falls back to each definition's own defaultValue. What
// this test actually pins is that a face defaults to translucent while an edge and a vertex
// default to solid.
//
// The shipped theme's own values are exercised below, by ShippedGeometryHighlightTest.
TEST_F(GeometryHighlighterTest, theSixHighlightColourTokensResolve)
{
    const Gui::StyleParameters::ParameterManager parameters;

    const Base::Color referenceFace = parameters.resolve(
        Gui::StyleParameters::GeometryHighlightReferenceFaceColor
    );
    const Base::Color referenceEdge = parameters.resolve(
        Gui::StyleParameters::GeometryHighlightReferenceEdgeColor
    );
    const Base::Color referencePoint = parameters.resolve(
        Gui::StyleParameters::GeometryHighlightReferencePointColor
    );
    const Base::Color hoveredFace = parameters.resolve(
        Gui::StyleParameters::GeometryHighlightHoveredFaceColor
    );
    const Base::Color hoveredEdge = parameters.resolve(
        Gui::StyleParameters::GeometryHighlightHoveredEdgeColor
    );
    const Base::Color hoveredPoint = parameters.resolve(
        Gui::StyleParameters::GeometryHighlightHoveredPointColor
    );

    EXPECT_LT(referenceFace.a, 1.0F);
    EXPECT_LT(hoveredFace.a, 1.0F);
    EXPECT_FLOAT_EQ(referenceEdge.a, 1.0F);
    EXPECT_FLOAT_EQ(referencePoint.a, 1.0F);
    EXPECT_FLOAT_EQ(hoveredEdge.a, 1.0F);
    EXPECT_FLOAT_EQ(hoveredPoint.a, 1.0F);
}

namespace
{
/// The application's own source stack, in the application's own priority order: built-in
/// parameters, then the design system, then the named theme on top — which pulls in
/// "FreeCAD Base.yaml" itself through its own _inherits key. Modelled on
/// ShippedThemeTest::loadShippedTheme() in tests/src/Gui/StyleParameters/ShippedThemeTest.cpp.
std::unique_ptr<Gui::StyleParameters::ParameterManager> loadShippedTheme(const std::string& themeName)
{
    auto manager = std::make_unique<Gui::StyleParameters::ParameterManager>();

    manager->addSource(
        new Gui::StyleParameters::BuiltInParameterSource({.name = "Built-in Parameters"})
    );
    manager->addSource(new Gui::StyleParameters::YamlParameterSource(
        "qss:parameters/Design System.yaml",
        {.name = "Design System Parameters"}
    ));
    manager->addSource(new Gui::StyleParameters::YamlParameterSource(
        "qss:parameters/" + themeName + ".yaml",
        {.name = "Theme Parameters"}
    ));

    return manager;
}
}  // namespace

// A test fixture *can* read the shipped theme YAML with no Gui::Application at all: the
// "qss:" search path StartupProcess::setStyleSheetPaths() installs during real GUI startup is
// just a Qt search path, and installing the identical one here is all it takes — exactly what
// ShippedThemeTest::SetUp() does in tests/src/Gui/StyleParameters/ShippedThemeTest.cpp, and
// what this fixture does too.
//
// Without this, nothing evaluates what "FreeCAD Base.yaml" actually assigns the six
// GeometryHighlight*Color tokens. ShippedThemeTest's EveryParameterReferenceResolves only
// extracts "@name.member" substrings and checks each resolves to something — it does not
// evaluate the surrounding expression, so misspelling e.g. opacity(@Blue.500, 30%) into a bare
// @Blue.500 would ship an opaque face highlight with that test still green.
class ShippedGeometryHighlightTest: public ::testing::TestWithParam<std::string>
{
protected:
    void SetUp() override
    {
        tests::initApplication();

        previousSearchPaths = QDir::searchPaths(QStringLiteral("qss"));
        QDir::setSearchPaths(
            QStringLiteral("qss"),
            {QString::fromStdString(App::Application::getResourceDir() + "Gui/Stylesheets/")}
        );
    }

    void TearDown() override
    {
        QDir::setSearchPaths(QStringLiteral("qss"), previousSearchPaths);
    }

private:
    QStringList previousSearchPaths;
};

// Pins the alpha shape the highlighter depends on: a face reads translucent so the geometry
// under it stays visible, while an edge and a point read fully opaque.
TEST_P(ShippedGeometryHighlightTest, TheSixTokensResolveWithTheExpectedAlpha)  // NOLINT
{
    const auto manager = loadShippedTheme(GetParam());

    const Base::Color referenceFace = manager->resolve(
        Gui::StyleParameters::GeometryHighlightReferenceFaceColor
    );
    const Base::Color referenceEdge = manager->resolve(
        Gui::StyleParameters::GeometryHighlightReferenceEdgeColor
    );
    const Base::Color referencePoint = manager->resolve(
        Gui::StyleParameters::GeometryHighlightReferencePointColor
    );
    const Base::Color hoveredFace = manager->resolve(
        Gui::StyleParameters::GeometryHighlightHoveredFaceColor
    );
    const Base::Color hoveredEdge = manager->resolve(
        Gui::StyleParameters::GeometryHighlightHoveredEdgeColor
    );
    const Base::Color hoveredPoint = manager->resolve(
        Gui::StyleParameters::GeometryHighlightHoveredPointColor
    );

    EXPECT_LT(referenceFace.a, 1.0F);
    EXPECT_LT(hoveredFace.a, 1.0F);
    EXPECT_FLOAT_EQ(referenceEdge.a, 1.0F);
    EXPECT_FLOAT_EQ(referencePoint.a, 1.0F);
    EXPECT_FLOAT_EQ(hoveredEdge.a, 1.0F);
    EXPECT_FLOAT_EQ(hoveredPoint.a, 1.0F);
}

INSTANTIATE_TEST_SUITE_P(
    Themes,
    ShippedGeometryHighlightTest,
    ::testing::Values(std::string("FreeCAD Light"), std::string("FreeCAD Dark")),
    [](const ::testing::TestParamInfo<std::string>& info) {
        std::string name = info.param;
        std::erase(name, ' ');
        return name;
    }
);
