// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include <Gui/StyleParameters/ParameterDescriptorRegistry.h>

using namespace Gui::StyleParameters;

namespace
{

ParameterDescriptorRegistry builtinRegistry()
{
    ParameterDescriptorRegistry registry;
    populateBuiltinDescriptors(registry);
    return registry;
}

}  // namespace

TEST(DescriptorRegistryTest, ParsesTransparencyVariantOnAnyComponent)
{
    const ParameterDescriptorRegistry registry = builtinRegistry();

    const auto parsed = registry.parse("ToolBarTransparentBackground");

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->component, "ToolBar");
    EXPECT_EQ(parsed->property, "Background");
    ASSERT_TRUE(parsed->variants.contains("TransparencyMode"));
    EXPECT_EQ(parsed->variants.at("TransparencyMode"), "Transparent");
}

TEST(DescriptorRegistryTest, ParsedAndContextPrefixesAgree)
{
    const ParameterDescriptorRegistry registry = builtinRegistry();

    StyleContext context;
    context.component = StyleComponent::ToolBar;
    context.variant.set(VariantSlot::TransparencyMode, TransparencyMode::Transparent);

    const auto parsed = registry.parse("ToolBarTransparentBackground");
    ASSERT_TRUE(parsed.has_value());

    EXPECT_EQ(registry.buildPrefixes(context), registry.buildPrefixesFromParsed(*parsed));
}

TEST(DescriptorRegistryTest, IsTransparentPropertyParses)
{
    EXPECT_EQ(propertyString(StyleProperty::IsTransparent), "IsTransparent");

    const ParameterDescriptorRegistry registry = builtinRegistry();

    const auto plain = registry.parse("ListIsTransparent");
    ASSERT_TRUE(plain.has_value());
    EXPECT_EQ(plain->component, "List");
    EXPECT_EQ(plain->property, "IsTransparent");
    EXPECT_TRUE(plain->variants.empty());

    const auto qualified = registry.parse("ListTransparentIsTransparent");
    ASSERT_TRUE(qualified.has_value());
    EXPECT_EQ(qualified->component, "List");
    EXPECT_EQ(qualified->property, "IsTransparent");
    EXPECT_EQ(qualified->variants.at("TransparencyMode"), "Transparent");
}

// A Branch element names its own token namespace under every component in the tree's chain,
// which is what lets a theme style connector lines without touching the item tokens.
TEST(DescriptorRegistryTest, BranchElementNamesPrefixesAlongTheChain)
{
    const ParameterDescriptorRegistry registry = builtinRegistry();

    StyleContext context;
    context.component = StyleComponent::Tree;
    context.element = StyleComponentElement::Branch;

    const std::vector<std::string> expected {"TreeBranch", "ListBranch"};
    EXPECT_EQ(registry.buildPrefixes(context), expected);
}

TEST(DescriptorRegistryTest, MenuComponentBuildsItsTokenPrefixes)
{
    const ParameterDescriptorRegistry registry = builtinRegistry();

    StyleContext context;
    context.component = StyleComponent::Menu;
    context.element = StyleComponentElement::Item;
    context.state |= StyleState::Hovered;

    const auto prefixes = registry.buildPrefixes(context);

    // Most specific first, and standalone: nothing falls back to List.
    ASSERT_FALSE(prefixes.empty());
    EXPECT_EQ(prefixes.front(), "MenuItemHovered");
    EXPECT_NE(std::ranges::find(prefixes, "MenuItem"), prefixes.end());
    EXPECT_EQ(std::ranges::find(prefixes, "ListItem"), prefixes.end());
}

TEST(DescriptorRegistryTest, MenuSubElementsNameTheirOwnPrefixes)
{
    const ParameterDescriptorRegistry registry = builtinRegistry();

    const auto prefixFor = [&registry](StyleComponentElement element) {
        StyleContext context;
        context.component = StyleComponent::Menu;
        context.element = element;
        return registry.buildPrefixes(context).front();
    };

    EXPECT_EQ(prefixFor(StyleComponentElement::Root), "Menu");
    EXPECT_EQ(prefixFor(StyleComponentElement::Separator), "MenuSeparator");
    EXPECT_EQ(prefixFor(StyleComponentElement::Arrow), "MenuArrow");
    EXPECT_EQ(prefixFor(StyleComponentElement::Shortcut), "MenuShortcut");
    EXPECT_EQ(prefixFor(StyleComponentElement::IconIndicator), "MenuIconIndicator");
}

TEST(DescriptorRegistryTest, TooltipComponentBuildsItsTokenPrefixes)
{
    const ParameterDescriptorRegistry registry = builtinRegistry();

    StyleContext context;
    context.component = StyleComponent::Tooltip;

    const auto prefixes = registry.buildPrefixes(context);

    // Standalone like Menu: a tooltip is a surface and nothing else, so nothing it fails to
    // state may be picked up from another component's tokens.
    ASSERT_FALSE(prefixes.empty());
    EXPECT_EQ(prefixes.front(), "Tooltip");
    EXPECT_EQ(prefixes.size(), 1U);
}

TEST(DescriptorRegistryTest, TooltipTokenNamesParseBackToTheComponent)
{
    const ParameterDescriptorRegistry registry = builtinRegistry();

    const auto parsed = registry.parse("TooltipBorderThickness");

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->component, "Tooltip");
}

TEST(DescriptorRegistryTest, HoveredOutranksSelectedInTheFallbackChain)
{
    const ParameterDescriptorRegistry registry = builtinRegistry();

    StyleContext context;
    context.component = StyleComponent::List;
    context.element = StyleComponentElement::Row;
    context.state |= StyleState::Selected;
    context.state |= StyleState::Hovered;

    const auto prefixes = registry.buildPrefixes(context);

    // A selection must still react to the pointer: hover is the only state that answers "is this
    // thing live", so a row that is both takes the hovered colour. States do not compose, so
    // whichever prefix comes first is the one a theme's token is found under.
    const auto selected = std::ranges::find(prefixes, "ListRowSelected");
    const auto hovered = std::ranges::find(prefixes, "ListRowHovered");
    ASSERT_NE(selected, prefixes.end());
    ASSERT_NE(hovered, prefixes.end());
    EXPECT_LT(hovered - prefixes.begin(), selected - prefixes.begin());
}

TEST(DescriptorRegistryTest, PressedOutranksSelected)
{
    const ParameterDescriptorRegistry registry = builtinRegistry();

    StyleContext context;
    context.component = StyleComponent::List;
    context.element = StyleComponentElement::Row;
    context.state |= StyleState::Pressed;
    context.state |= StyleState::Selected;

    const auto prefixes = registry.buildPrefixes(context);

    const auto pressed = std::ranges::find(prefixes, "ListRowPressed");
    const auto selected = std::ranges::find(prefixes, "ListRowSelected");
    ASSERT_NE(pressed, prefixes.end());
    ASSERT_NE(selected, prefixes.end());
    EXPECT_LT(pressed - prefixes.begin(), selected - prefixes.begin());
}

TEST(DescriptorRegistryTest, AnActiveTabResolvesThroughSelected)
{
    const ParameterDescriptorRegistry registry = builtinRegistry();

    StyleContext context;
    context.component = StyleComponent::TabBar;
    context.element = StyleComponentElement::Tab;
    context.state |= StyleState::Selected;

    const auto prefixes = registry.buildPrefixes(context);

    // The active tab is a selection, not a toggle: the theme states TabBarTabSelected*, and
    // nothing should still be reachable under the Checked spelling it used to carry.
    EXPECT_NE(std::ranges::find(prefixes, "TabBarTabSelected"), prefixes.end());
    EXPECT_EQ(std::ranges::find(prefixes, "TabBarTabChecked"), prefixes.end());
}
