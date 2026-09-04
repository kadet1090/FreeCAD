// SPDX-License-Identifier: LGPL-2.1-or-later
/****************************************************************************
 *                                                                          *
 *   Copyright (c) 2025 Kacper Donat <kacper@kadet.net>                     *
 *                                                                          *
 *   This file is part of FreeCAD.                                          *
 *                                                                          *
 *   FreeCAD is free software: you can redistribute it and/or modify it     *
 *   under the terms of the GNU Lesser General Public License as            *
 *   published by the Free Software Foundation, either version 2.1 of the   *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   FreeCAD is distributed in the hope that it will be useful, but         *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with FreeCAD. If not, see                                *
 *   <https://www.gnu.org/licenses/>.                                       *
 *                                                                          *
 ***************************************************************************/

#include <memory>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <QDir>
#include <QString>
#include <QStringList>

#include "src/App/InitApplication.h"

#include <App/Application.h>
#include <Gui/StyleParameters/Insets.h>
#include <Gui/StyleParameters/ParameterManager.h>
#include <Gui/StyleParameters/Value.h>

#include "DiagnosticsCapture.h"

using namespace Gui::StyleParameters;

// Alias to avoid ambiguity with ParameterGrp::Manager() from Base/Parameter.h.
using StyleParameterManager = Gui::StyleParameters::ParameterManager;

namespace
{

// A reference as the parser builds it: '@' plus a parameter name, then any number of '.'
// member accesses into the tuple that name resolves to ("@Spacing.150", "@BaseRadius.medium").
// The two groups are kept apart because they fail in different ways — an unknown parameter
// yields the literal "@Name", an unknown member throws.
const std::regex referencePattern {R"(@([A-Za-z_][A-Za-z0-9_]*)((?:\.[A-Za-z0-9_]+)*))"};

struct Reference
{
    std::string parameter;
    std::vector<std::string> members;

    std::string spelling() const
    {
        std::string text = "@" + parameter;
        for (const std::string& member : members) {
            text += "." + member;
        }
        return text;
    }
};

std::vector<std::string> splitMembers(const std::string& trailer)
{
    std::vector<std::string> members;

    for (size_t start = 0; start < trailer.size();) {
        const size_t next = trailer.find('.', start + 1);
        members.push_back(trailer.substr(start + 1, next - start - 1));
        start = (next == std::string::npos) ? trailer.size() : next;
    }

    return members;
}

std::vector<Reference> referencesIn(const std::string& expression)
{
    std::vector<Reference> references;

    for (auto match = std::sregex_iterator(expression.begin(), expression.end(), referencePattern);
         match != std::sregex_iterator();
         ++match) {
        references.push_back({
            .parameter = (*match)[1].str(),
            .members = splitMembers((*match)[2].str()),
        });
    }

    return references;
}

// Walks a resolved reference down its member chain, reporting the first step that does not
// exist. An empty string means the whole reference is sound.
std::string reasonReferenceFails(const StyleParameterManager& manager, const Reference& reference)
{
    // The flat overload is what ParameterReference::evaluate() itself calls, so this asks
    // exactly the question the evaluator asks.
    std::optional<Value> value = manager.resolve(reference.parameter, {});

    if (!value) {
        return "no parameter named '" + reference.parameter + "'";
    }

    for (const std::string& member : reference.members) {
        if (!value->holds<Tuple>()) {
            return "'" + reference.parameter + "' is not a tuple, so '." + member + "' has no target";
        }

        const Value* element = value->get<Tuple>().find(member);
        if (element == nullptr) {
            return "'" + reference.parameter + "' has no member '" + member + "'";
        }

        value = *element;
    }

    return {};
}

}  // namespace

/**
 * @brief Resolves the YAML the application actually ships, rather than a fixture standing in
 *        for it.
 *
 * Every other style-parameter suite installs a hand-written InMemoryParameterSource, so a
 * theme file can name a token that exists nowhere and the whole suite still passes. This one
 * loads the shipped files through the same YamlParameterSource and the same "qss:parameters/…"
 * paths Gui::Application uses.
 */
class ShippedThemeTest: public ::testing::TestWithParam<std::string>
{
protected:
    void SetUp() override
    {
        tests::initApplication();

        previousSearchPaths = QDir::searchPaths(QStringLiteral("qss"));

        // The same search path StartupProcess::setStyleSheetPaths() installs, so the "qss:"
        // strings below are literally the ones Gui::Application passes to YamlParameterSource.
        QDir::setSearchPaths(
            QStringLiteral("qss"),
            {QString::fromStdString(App::Application::getResourceDir() + "Gui/Stylesheets/")}
        );
    }

    void TearDown() override
    {
        QDir::setSearchPaths(QStringLiteral("qss"), previousSearchPaths);
    }

    // The application's own stack of sources, in the application's own priority order: the
    // built-in parameters first, then the design system, then the theme on top. The theme
    // file pulls "FreeCAD Base.yaml" in itself through its _inherits key.
    static std::unique_ptr<StyleParameterManager> loadShippedTheme(const std::string& themeName)
    {
        auto manager = std::make_unique<StyleParameterManager>();

        manager->addSource(new BuiltInParameterSource({.name = "Built-in Parameters"}));
        manager->addSource(new YamlParameterSource(
            "qss:parameters/Design System.yaml",
            {.name = "Design System Parameters"}
        ));
        manager->addSource(
            new YamlParameterSource("qss:parameters/defaults.yaml", {.name = "Default Parameters"})
        );
        manager->addSource(new YamlParameterSource(
            "qss:parameters/" + themeName + ".yaml",
            {.name = "Theme Parameters"}
        ));

        return manager;
    }

private:
    QStringList previousSearchPaths;
};

// The same search paths as ShippedThemeTest, without the theme parameter — this case is about
// a theme that has no file at all, so there is nothing to parameterise over.
class ShippedThemeDefaultsTest: public ShippedThemeTest
{
};

// The files have to be found at all before anything below means anything: an unreadable path
// yields an empty source, and an empty source has no dangling references to report.
TEST_P(ShippedThemeTest, TheShippedFilesAreFoundAndNonEmpty)  // NOLINT
{
    const auto manager = loadShippedTheme(GetParam());

    EXPECT_GT(manager->parameters().size(), 100U);
    // One from the theme's inherited "FreeCAD Base.yaml", one from "Design System.yaml".
    EXPECT_TRUE(manager->parameter("BaseWindowBackground").has_value());
    EXPECT_TRUE(manager->parameter("Spacing").has_value());
}

// MainWindow hands the tab strip whatever MDIView::paneBackground() answers with, and a name
// no theme defines is stored without complaint and then resolves to nothing on every paint —
// the seam silently reverts to the default pane colour. Nothing in the style suites sees that,
// because none of them read these expressions. Each name below is stated verbatim by the view
// named beside it; changing one without changing the other is what this catches.
TEST_P(ShippedThemeTest, TheSurfacesTheMdiViewsNameAreDefined)  // NOLINT
{
    const auto manager = loadShippedTheme(GetParam());

    // Gui::MDIView::paneBackground(), the answer every view that states no surface of its own
    // keeps: Start, Spreadsheet, Image, Graphviz.
    EXPECT_TRUE(manager->resolve("BaseWindowBackground").has_value());

    // Gui::EditorView and Gui::TextDocumentEditorView, whose editor fills the view.
    EXPECT_TRUE(manager->resolve("LineEditBackground").has_value());

    // The token MainWindow overrides, and the tab fill that has to keep reading it for any of
    // the above to reach the seam at all.
    EXPECT_TRUE(manager->resolve("CurrentPaneBackground").has_value());
    EXPECT_THAT(
        manager->parameter("TabBarTabSelectedBackground")->value,
        testing::HasSubstr("@CurrentPaneBackground")
    );
}

// Every theme that ships a parameters file states the bar's own surface. A theme that ships none
// is a different case and deliberately not covered here: PE_PanelStatusBar declines, the base
// style paints the window background, and the bar looks exactly as it did before it was styled.
TEST_P(ShippedThemeTest, TheStatusBarSurfaceIsDefined)  // NOLINT
{
    const auto manager = loadShippedTheme(GetParam());

    EXPECT_TRUE(manager->resolve("StatusBarBackground").has_value());
}

// A theme that ships no parameters file at all is not hypothetical: the FreeCAD Classic
// preference pack names one that does not exist. A dock panel paints its own body and title bar,
// so without these two the panel chrome is not merely plain, it is absent - which is the whole
// reason the defaults layer exists. Nothing else is expected to survive here.
TEST_F(ShippedThemeDefaultsTest, AThemeWithNoFileStillPaintsPanelChrome)  // NOLINT
{
    const auto manager = loadShippedTheme("FreeCAD Classic");

    EXPECT_TRUE(manager->resolve("PanelBackground").has_value());
    EXPECT_TRUE(manager->resolve("PanelTitleBackground").has_value());
    EXPECT_TRUE(manager->resolve("PanelTitleTextColor").has_value());

    // The net is deliberately thin: what a theme's silence leaves plain rather than invisible is
    // not defaulted, and BaseBorderColor belongs to the theme layer that such a theme never loads.
    EXPECT_FALSE(manager->parameter("BaseBorderColor").has_value());
}

// The defaults layer may reach Design System.yaml, which is loaded beside it, and nothing else.
// A reference into FreeCAD Base.yaml would be invisible to exactly the themes this file exists
// for. Resolving is not enough to catch that: an unresolved "@Name" comes back as the literal
// string, which is an engaged value, so the token would look defaulted while painting nonsense.
// Sweeping the no-file manager is what turns the rule into something that fails.
TEST_F(ShippedThemeDefaultsTest, TheDefaultsLayerReferencesNothingItCannotSee)  // NOLINT
{
    const auto manager = loadShippedTheme("FreeCAD Classic");

    std::set<std::string> dangling;

    for (const Parameter& parameter : manager->parameters()) {
        for (const Reference& reference : referencesIn(parameter.value)) {
            if (const std::string reason = reasonReferenceFails(*manager, reference);
                !reason.empty()) {
                dangling.insert(parameter.name + " -> " + reference.spelling() + ": " + reason);
            }
        }
    }

    std::string report;
    for (const std::string& entry : dangling) {
        report += "\n  " + entry;
    }

    EXPECT_TRUE(dangling.empty()) << "Defaults reaching outside their own layer:" << report;
}

// The overlay layout reads this one straight off the manager rather than through a style
// context, and converts it to margins. A name starting with a digit is unusual enough to be
// worth pinning: the parser accepts it, but nothing else in the shipped files looks like this.
TEST_P(ShippedThemeTest, TheViewPaddingResolvesAsInsets)  // NOLINT
{
    const auto manager = loadShippedTheme(GetParam());

    const auto insets = valueAs<Insets>(manager->resolve("3DViewPadding"));
    ASSERT_TRUE(insets.has_value());

    // Stated once and applied to every edge, so all four have to come back with the same room.
    EXPECT_GT(insets->left().value, 0.0);
    EXPECT_EQ(insets->left().value, insets->top().value);
    EXPECT_EQ(insets->left().value, insets->right().value);
    EXPECT_EQ(insets->left().value, insets->bottom().value);
}

// ParameterReference::evaluate() answers an unresolved "@Name" with the literal string
// "@Name", which is an engaged Value — so a dangling reference does not fail loudly, it
// silently becomes a string that later converts to Qt::NoBrush and stops the token fallback
// chain dead. Sweeping every reference in the shipped files is the only cheap way to see it.
//
// Every "@ref" in the shipped theme resolves, and this test carries no waiver list. It used to:
// seven references were dangling at the point the sweep was first written, and all seven have
// since been repaired. Nothing here is expected to need waiving again — a failure means a real
// dangling reference was introduced, so fix the YAML rather than reinstating a list.
TEST_P(ShippedThemeTest, EveryParameterReferenceResolves)  // NOLINT
{
    const auto manager = loadShippedTheme(GetParam());

    std::set<std::string> dangling;

    for (const Parameter& parameter : manager->parameters()) {
        for (const Reference& reference : referencesIn(parameter.value)) {
            if (const std::string reason = reasonReferenceFails(*manager, reference);
                !reason.empty()) {
                dangling.insert(parameter.name + " -> " + reference.spelling() + ": " + reason);
            }
        }
    }

    std::string report;
    for (const std::string& entry : dangling) {
        report += "\n  " + entry;
    }

    EXPECT_TRUE(dangling.empty()) << "Unresolved parameter references:" << report;
}

// A parameter that does not parse still resolves: ParameterManager catches the ParserError,
// reports it, and falls back to the raw text — so the theme keeps working and the token is
// quietly wrong. Nothing else in the suite evaluates the shipped expressions, so a value the
// grammar cannot read ships unnoticed until someone sees the console.
TEST_P(ShippedThemeTest, EveryParameterEvaluates)  // NOLINT
{
    const auto manager = loadShippedTheme(GetParam());

    DiagnosticsCapture diagnostics;

    for (const Parameter& parameter : manager->parameters()) {
        manager->resolve(parameter.name, {});
    }

    std::string report;
    for (const std::string& message : diagnostics.messages()) {
        report += "\n  " + message;
    }

    EXPECT_TRUE(diagnostics.messages().empty()) << "Style parameters that failed:" << report;
}

// A dangling reference produces a plausible-looking std::string, so "it resolved" is not
// enough — these have to come back as the kind of value their consumer can use. All four feed
// resolveBoxStyle(), which hands them to Base::convertTo<QBrush>.
INSTANTIATE_TEST_SUITE_P(
    Themes,
    ShippedThemeTest,
    ::testing::Values(std::string("FreeCAD Light"), std::string("FreeCAD Dark")),
    [](const ::testing::TestParamInfo<std::string>& info) {
        std::string name = info.param;
        std::erase(name, ' ');
        return name;
    }
);
