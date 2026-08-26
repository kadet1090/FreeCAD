// SPDX-License-Identifier: LGPL-2.1-or-later
/****************************************************************************
 *                                                                          *
 *   Copyright (c) 2026 Kacper Donat <kacper@kadet.net>                     *
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

#include <type_traits>

#include <gtest/gtest.h>

#include <Gui/StyleParameters/ParameterManager.h>
#include <Gui/StyleParameters/StyleOverrides.h>
#include <Gui/StyleParameters/Value.h>

using namespace Gui::StyleParameters;

// byId holds pointers into ids's map nodes; a copy would leave the copy's byId pointing into the
// original's storage. Move is safe because std::map move preserves node addresses, and
// ParameterManager's own move constructor depends on that staying available.
static_assert(!std::is_copy_constructible_v<OverrideRegistry>);
static_assert(!std::is_copy_assignable_v<OverrideRegistry>);
static_assert(std::is_move_constructible_v<OverrideRegistry>);
static_assert(std::is_move_assignable_v<OverrideRegistry>);

TEST(OverrideRegistryTest, EmptySetIsIdZeroAndIsNotStored)
{
    OverrideRegistry registry;

    EXPECT_EQ(registry.intern(OverrideSet {}), OverrideRegistry::emptyId);
    EXPECT_EQ(registry.size(), 0U);
    EXPECT_TRUE(registry.get(OverrideRegistry::emptyId).empty());
}

TEST(OverrideRegistryTest, IdenticalSetsShareOneId)
{
    OverrideRegistry registry;

    const uint32_t first = registry.intern({{"PaneBackground", "@ListBackground"}});
    const uint32_t second = registry.intern({{"PaneBackground", "@ListBackground"}});

    EXPECT_EQ(first, second);
    EXPECT_NE(first, OverrideRegistry::emptyId);
    EXPECT_EQ(registry.size(), 1U);
}

TEST(OverrideRegistryTest, DifferentSetsGetDifferentIds)
{
    OverrideRegistry registry;

    const uint32_t first = registry.intern({{"PaneBackground", "@ListBackground"}});
    const uint32_t second = registry.intern({{"PaneBackground", "@TreeBackground"}});

    EXPECT_NE(first, second);
    EXPECT_EQ(registry.size(), 2U);
}

// The set is ordered, so two widgets that declared the same overrides in a different order are
// still one set and still share a resolution cache.
TEST(OverrideRegistryTest, InsertionOrderDoesNotAffectIdentity)
{
    OverrideRegistry registry;

    OverrideSet forward;
    forward.emplace("Alpha", "1px");
    forward.emplace("Beta", "2px");

    OverrideSet reversed;
    reversed.emplace("Beta", "2px");
    reversed.emplace("Alpha", "1px");

    EXPECT_EQ(registry.intern(forward), registry.intern(reversed));
    EXPECT_EQ(registry.size(), 1U);
}

TEST(OverrideRegistryTest, GetReturnsTheStoredContent)
{
    OverrideRegistry registry;

    const uint32_t identifier = registry.intern({{"PaneBackground", "@ListBackground"}});

    ASSERT_EQ(registry.get(identifier).size(), 1U);
    EXPECT_EQ(registry.get(identifier).at("PaneBackground"), "@ListBackground");
}

// A widget can outlive nothing here, but a stale or garbage id must not index out of bounds.
TEST(OverrideRegistryTest, UnknownIdReturnsTheEmptySet)
{
    OverrideRegistry registry;
    registry.intern({{"PaneBackground", "@ListBackground"}});

    EXPECT_TRUE(registry.get(9999).empty());
}

class OverriddenResolutionTest: public ::testing::Test
{
protected:
    void SetUp() override
    {
        source = std::make_unique<InMemoryParameterSource>(
            std::list<Parameter> {
                {"PaneBackground", "#112233"},
                {"PanelBackground", "@PaneBackground"},
                {"PanelBorderColor", "#aabbcc"},
            },
            ParameterSource::Metadata {"Override Fixture"}
        );
        manager.addSource(source.get());
    }

    /// The id of a set holding exactly one override, ready to drop into a ResolveContext.
    uint32_t setOf(const std::string& name, const std::string& expression)
    {
        return manager.overrideRegistry().intern({{name, expression}});
    }

    static std::string colorOf(const std::optional<Value>& value)
    {
        return std::get<Base::Color>(*value).asHexString();
    }

    Gui::StyleParameters::ParameterManager manager;
    std::unique_ptr<ParameterSource> source;
};

TEST_F(OverriddenResolutionTest, ADirectOverrideBeatsTheSource)
{
    const ResolveContext context {
        .visited = {},
        .overrides = setOf("PaneBackground", "#445566"),
    };

    EXPECT_EQ(colorOf(manager.resolve("PaneBackground", context)), "#445566");
}

// The whole point of the mechanism: a token that merely *references* the overridden one
// changes too, so a theme can route several tokens through one overridable name.
TEST_F(OverriddenResolutionTest, ANestedReferenceSeesTheOverride)
{
    const ResolveContext context {
        .visited = {},
        .overrides = setOf("PaneBackground", "#445566"),
    };

    EXPECT_EQ(colorOf(manager.resolve("PanelBackground", context)), "#445566");
}

TEST_F(OverriddenResolutionTest, AnUnrelatedTokenIsUnaffected)
{
    const ResolveContext context {
        .visited = {},
        .overrides = setOf("PaneBackground", "#445566"),
    };

    EXPECT_EQ(colorOf(manager.resolve("PanelBorderColor", context)), "#AABBCC");
}

// An overridden resolution must not be written into the shared _resolved map, or the first
// widget to paint would decide the colour for every other widget in the application.
TEST_F(OverriddenResolutionTest, OverriddenResolutionDoesNotPoisonThePlainCache)
{
    const ResolveContext context {
        .visited = {},
        .overrides = setOf("PaneBackground", "#445566"),
    };

    manager.resolve("PanelBackground", context);

    EXPECT_EQ(colorOf(manager.resolve("PanelBackground", {})), "#112233");
}

// ...and the reverse order: a value already in the shared cache must not shadow an override.
TEST_F(OverriddenResolutionTest, ThePlainCacheDoesNotShadowAnOverride)
{
    manager.resolve("PanelBackground", {});

    const ResolveContext context {
        .visited = {},
        .overrides = setOf("PaneBackground", "#445566"),
    };

    EXPECT_EQ(colorOf(manager.resolve("PanelBackground", context)), "#445566");
}

TEST_F(OverriddenResolutionTest, TwoSetsResolveIndependently)
{
    const ResolveContext first {
        .visited = {},
        .overrides = setOf("PaneBackground", "#445566"),
    };
    const ResolveContext second {
        .visited = {},
        .overrides = setOf("PaneBackground", "#778899"),
    };

    EXPECT_EQ(colorOf(manager.resolve("PanelBackground", first)), "#445566");
    EXPECT_EQ(colorOf(manager.resolve("PanelBackground", second)), "#778899");
}

// An override may name a token the theme never defines; it simply supplies it.
TEST_F(OverriddenResolutionTest, AnOverrideCanSupplyATokenTheSourceLacks)
{
    const ResolveContext context {
        .visited = {},
        .overrides = setOf("PaneOutline", "#445566"),
    };

    EXPECT_EQ(colorOf(manager.resolve("PaneOutline", context)), "#445566");
}

// A broken override is treated as if it had never been declared, so the widget falls back to
// the theme rather than painting a literal string.
TEST_F(OverriddenResolutionTest, AnUnparseableOverrideFallsBackToTheSource)
{
    const ResolveContext context {
        .visited = {},
        .overrides = setOf("PaneBackground", "rgb(((("),
    };

    EXPECT_EQ(colorOf(manager.resolve("PaneBackground", context)), "#112233");
}

// Self-reference must terminate. Without the visited guard covering the override's own
// expression this recurses until the stack runs out.
TEST_F(OverriddenResolutionTest, ASelfReferentialOverrideTerminates)
{
    const ResolveContext context {
        .visited = {},
        .overrides = setOf("PaneBackground", "@PaneBackground"),
    };

    EXPECT_NO_FATAL_FAILURE(manager.resolve("PaneBackground", context));
}

// PanelBorderColor is not itself overridden by this context, so resolving it through the
// overridden path falls through to the theme and caches that fallen-through value in
// _overrideResolved. If reload() only cleared the plain _resolved cache, this stale value would
// survive a theme change for as long as the widget's override set stayed alive.
TEST_F(OverriddenResolutionTest, ReloadDropsCachedValuesThatFellThroughToTheSource)
{
    const ResolveContext context {
        .visited = {},
        .overrides = setOf("PaneBackground", "#445566"),
    };

    EXPECT_EQ(colorOf(manager.resolve("PanelBorderColor", context)), "#AABBCC");

    source->define({"PanelBorderColor", "#ffffff"});
    manager.reload();

    EXPECT_EQ(colorOf(manager.resolve("PanelBorderColor", context)), "#FFFFFF");
}

TEST_F(OverriddenResolutionTest, ReloadPreservesTheOverrideSetId)
{
    const ResolveContext context {
        .visited = {},
        .overrides = setOf("PaneBackground", "#445566"),
    };

    EXPECT_EQ(colorOf(manager.resolve("PaneBackground", context)), "#445566");

    manager.reload();

    // The id must survive the reload — widgets hold it — so the same context still resolves.
    EXPECT_EQ(colorOf(manager.resolve("PaneBackground", context)), "#445566");
}
