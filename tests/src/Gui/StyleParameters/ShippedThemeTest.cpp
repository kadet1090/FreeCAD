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

#include <gtest/gtest.h>

#include <QDir>
#include <QString>
#include <QStringList>

#include "src/App/InitApplication.h"

#include <App/Application.h>
#include <Gui/StyleParameters/ParameterManager.h>
#include <Gui/StyleParameters/Value.h>

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
        manager->addSource(new YamlParameterSource(
            "qss:parameters/" + themeName + ".yaml",
            {.name = "Theme Parameters"}
        ));

        return manager;
    }

private:
    QStringList previousSearchPaths;
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
