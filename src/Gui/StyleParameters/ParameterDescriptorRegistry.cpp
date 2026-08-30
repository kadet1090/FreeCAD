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

#include "ParameterDescriptorRegistry.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <map>
#include <numeric>
#include <ranges>

namespace Gui::StyleParameters
{

namespace
{

// All tables use std::map so entries are self-documenting and order-independent.

// Returns a const reference to map[key] if present, or to a static empty
// default otherwise. Returning by reference avoids copies and is safe to
// wrap in std::span since the default outlives the call.
template<typename Map>
auto lookup(const Map& map, const typename Map::key_type& key) -> const typename Map::mapped_type&
{
    static const typename Map::mapped_type empty {};
    const auto found = map.find(key);
    return found != map.end() ? found->second : empty;
}

// Root is deliberately absent: elementString returns "" for it, so a component's own tokens
// read as {Component}{Property} with nothing between them.

// clang-format off
const std::map<StyleComponentElement, std::string_view> elementNames = {
    {StyleComponentElement::Item, "Item"},
    {StyleComponentElement::Row, "Row"},
    {StyleComponentElement::Indicator, "Indicator"},
    {StyleComponentElement::Branch, "Branch"},
    {StyleComponentElement::Tab, "Tab"},
    {StyleComponentElement::Base, "Base"},
    {StyleComponentElement::CloseButton, "CloseButton"},
    {StyleComponentElement::IconIndicator, "IconIndicator"},
    {StyleComponentElement::Menu, "Menu"},
    {StyleComponentElement::Separator, "Separator"},
    {StyleComponentElement::Arrow, "Arrow"},
    {StyleComponentElement::Shortcut, "Shortcut"},
    {StyleComponentElement::Title, "Title"},
};
// clang-format on

std::string_view elementString(StyleComponentElement element)
{
    return lookup(elementNames, element);
}

// Default (0) is absent from each inner map; variantSlotString returns "" for it.
// These are the canonical source of truth for variant value strings — also used
// by registerBuiltinVariants() to populate the ParameterDescriptorRegistry.

// clang-format off
const std::map<VariantSlot, std::map<uint8_t, std::string_view>> variantSlotNames = {
    {VariantSlot::ButtonType, {
        {static_cast<uint8_t>(ButtonType::Primary), "Primary"},
        {static_cast<uint8_t>(ButtonType::Link),    "Link"},
    }},
    {VariantSlot::ControlSize, {
        {static_cast<uint8_t>(ControlSize::Internal), "Internal"},
        {static_cast<uint8_t>(ControlSize::Small),    "Small"},
        {static_cast<uint8_t>(ControlSize::Big),      "Big"},
    }},
    {VariantSlot::Position, {
        {static_cast<uint8_t>(Position::East),  "East"},
        {static_cast<uint8_t>(Position::South), "South"},
        {static_cast<uint8_t>(Position::West),  "West"},
    }},
    {VariantSlot::RowType, {
        {static_cast<uint8_t>(RowType::Alternate), "Alternate"},
    }},
    {VariantSlot::FrameType, {
        {static_cast<uint8_t>(FrameType::Flat), "Flat"},
    }},
    {VariantSlot::CheckType, {
        {static_cast<uint8_t>(CheckType::Exclusive), "Exclusive"},
    }},
    {VariantSlot::TransparencyMode, {
        {static_cast<uint8_t>(TransparencyMode::Transparent), "Transparent"},
    }},
};
// clang-format on

std::string_view variantSlotString(VariantSlot slot, uint8_t value)
{
    return lookup(lookup(variantSlotNames, slot), value);
}

// The non-default variant value strings, in slot order.
// e.g. ButtonType=Primary, ControlSize=Internal → {"Primary", "Internal"}.
std::vector<std::string> variantFragments(const VariantKey& variant)
{
    std::vector<std::string> fragments;
    for (size_t index = 0; index < variant.slots.size(); ++index) {
        const std::string_view fragment
            = variantSlotString(static_cast<VariantSlot>(index), variant.slots.at(index));
        if (!fragment.empty()) {
            fragments.emplace_back(fragment);
        }
    }
    return fragments;
}

// Every combination of the variant fragments, most-specific first, dropping lower-priority
// (later-slot) fragments before higher-priority ones; the empty (base) combination is last.
// e.g. {"Primary", "Internal"} → {"PrimaryInternal", "Primary", "Internal", ""}.
//
// This makes variants *compose* in the fallback: a token defined for a single variant (say
// ButtonInternalHeight) is still reached when another variant (Primary) is also active, so a
// primary internal button gets both its accent colour and its 18px height. For zero or one
// active fragment the output is {fragment, ""} (or just {""}), identical to a plain
// concatenation, so single-variant widgets are completely unaffected.
std::vector<std::string> variantCombinations(const std::vector<std::string>& fragments)
{
    const size_t count = fragments.size();
    std::vector<std::uint32_t> masks(size_t {1} << count);
    std::iota(masks.begin(), masks.end(), std::uint32_t {0});
    // More fragments (more specific) first; ties broken so earlier slots are kept longer.
    std::ranges::sort(masks, [](std::uint32_t lhs, std::uint32_t rhs) {
        const int lhsBits = std::popcount(lhs);
        const int rhsBits = std::popcount(rhs);
        return lhsBits != rhsBits ? lhsBits > rhsBits : lhs < rhs;
    });

    std::vector<std::string> combinations;
    combinations.reserve(masks.size());
    for (const std::uint32_t mask : masks) {
        std::string combination;
        for (size_t bit = 0; bit < count; ++bit) {
            if ((mask & (std::uint32_t {1} << bit)) != 0) {
                combination += fragments[bit];
            }
        }
        combinations.push_back(std::move(combination));
    }
    return combinations;
}

// clang-format off
const std::map<StyleState, std::string_view> stateNames = {
    {StyleState::Disabled, "Disabled"},
    {StyleState::Pressed,  "Pressed"},
    {StyleState::Hovered,  "Hovered"},
    {StyleState::Checked,  "Checked"},
    {StyleState::Selected, "Selected"},
    {StyleState::Focused,  "Focused"},
};
// clang-format on

std::string_view stateString(StyleState state)
{
    return lookup(stateNames, state);
}

// Priority order — highest first. Mirrors enum declaration order (Disabled > Pressed > …).
constexpr auto statePriorityOrder = std::to_array({
    StyleState::Disabled,
    StyleState::Pressed,
    StyleState::Hovered,
    StyleState::Checked,
    StyleState::Selected,
    StyleState::Focused,
});

// Maps each VariantSlot to its display name (used in token names), in enum order.
// clang-format off
constexpr std::array<std::string_view, size_t(VariantSlot::COUNT)> variantSlotDisplayNames = {
    "ButtonType",  // VariantSlot::ButtonType
    "ControlSize", // VariantSlot::ControlSize
    "Position",    // VariantSlot::Position
    "RowType",     // VariantSlot::RowType
    "FrameType",   // VariantSlot::FrameType
    "CheckType",   // VariantSlot::CheckType
    "TransparencyMode", // VariantSlot::TransparencyMode
};
// clang-format on

// The array is sized by VariantSlot::COUNT, so a slot added without a name here would leave
// an empty string behind and silently shift every later slot's name onto the wrong dimension.
static_assert(
    std::ranges::none_of(variantSlotDisplayNames, [](std::string_view name) { return name.empty(); }),
    "Every VariantSlot needs a display name, in enum order."
);

// clang-format off
const std::map<StyleProperty, std::string_view> propertyNames = {
    {StyleProperty::Width,              "Width"},
    {StyleProperty::MinWidth,           "MinWidth"},
    {StyleProperty::MaxWidth,           "MaxWidth"},
    {StyleProperty::Height,             "Height"},
    {StyleProperty::MinHeight,          "MinHeight"},
    {StyleProperty::MaxHeight,          "MaxHeight"},
    {StyleProperty::BorderThickness,    "BorderThickness"},
    {StyleProperty::BorderRadius,       "BorderRadius"},
    {StyleProperty::BorderColor,        "BorderColor"},
    {StyleProperty::Padding,            "Padding"},
    {StyleProperty::Margin,             "Margin"},
    {StyleProperty::Spacing,            "Spacing"},
    {StyleProperty::Overlap,            "Overlap"},
    {StyleProperty::IconSize,           "IconSize"},
    {StyleProperty::IconSpacing,        "IconSpacing"},
    {StyleProperty::FontSize,           "FontSize"},
    {StyleProperty::FontWeight,         "FontWeight"},
    {StyleProperty::FontFamily,         "FontFamily"},
    {StyleProperty::FontStyle,          "FontStyle"},
    {StyleProperty::Background,         "Background"},
    {StyleProperty::TextColor,          "TextColor"},
    {StyleProperty::InnerShadow,        "InnerShadow"},
    {StyleProperty::IconColor,          "IconColor"},
    {StyleProperty::BackgroundEffect,   "BackgroundEffect"},
    {StyleProperty::BorderColorEffect,  "BorderColorEffect"},
    {StyleProperty::FrameWidth,         "FrameWidth"},
    {StyleProperty::IsTransparent,      "IsTransparent"},
    {StyleProperty::Placement,          "Placement"},
    {StyleProperty::PlacementOffset,    "PlacementOffset"},
    {StyleProperty::HorizontalAlign,    "HorizontalAlign"},
    {StyleProperty::VerticalAlign,      "VerticalAlign"},
};
// clang-format on

}  // namespace

std::string_view propertyString(StyleProperty property)
{
    return lookup(propertyNames, property);
}

void ParameterDescriptorRegistry::registerVariant(ParameterVariant variant)
{
    _variants[variant.name] = std::move(variant);
}

void ParameterDescriptorRegistry::registerGlobalVariant(ParameterVariant variant)
{
    if (std::ranges::find(_globalVariantNames, variant.name) == _globalVariantNames.end()) {
        _globalVariantNames.push_back(variant.name);
    }

    registerVariant(std::move(variant));
}

void ParameterDescriptorRegistry::registerDescriptor(
    ParameterDescriptor descriptor,
    StyleComponent component
)
{
    const std::string name = descriptor.name;

    if (component != StyleComponent::COUNT) {
        // Descriptor must be inserted first so resolveChainNames() can follow
        // the inherits list when building the full chain.
        _descriptors[name] = std::move(descriptor);
        _componentChains[component] = resolveChainNames(name);
        _componentByName[name] = component;
    }
    else {
        _descriptors[name] = std::move(descriptor);
    }

    _sortedNamesDirty = true;
}

std::optional<ParameterDescriptorRegistry::ComponentSplit> ParameterDescriptorRegistry::splitComponentPrefix(
    const std::string& name
) const
{
    // sortedNames() is longest-first, so the first match is the most specific component.
    for (const std::string& candidate : sortedNames()) {
        if (name.starts_with(candidate)) {
            return ComponentSplit {
                .component = candidate,
                .remaining = name.substr(candidate.size()),
            };
        }
    }

    return std::nullopt;
}

std::string ParameterDescriptorRegistry::takeVariantValues(
    const ParameterDescriptor& descriptor,
    std::string remaining,
    std::map<std::string, std::string>& variants
) const
{
    for (const std::string& variantName : variantNamesFor(descriptor)) {
        const auto variantIt = _variants.find(variantName);
        if (variantIt == _variants.end()) {
            continue;
        }

        for (const std::string& value : variantIt->second.values) {
            if (remaining.starts_with(value)) {
                variants[variantName] = value;
                remaining = remaining.substr(value.size());
                break;
            }
        }
    }

    return remaining;
}

std::optional<ParsedParameterName> ParameterDescriptorRegistry::parse(const std::string& name) const
{
    const auto split = splitComponentPrefix(name);
    if (!split) {
        return std::nullopt;
    }

    const ParameterDescriptor* descriptor = this->descriptor(split->component);
    if (!descriptor) {
        return std::nullopt;
    }

    ParsedParameterName result;
    result.component = split->component;
    result.property = takeVariantValues(*descriptor, split->remaining, result.variants);

    // A name that is nothing but a component and its variants states no property.
    if (result.property.empty()) {
        return std::nullopt;
    }

    return result;
}

std::vector<std::string> ParameterDescriptorRegistry::buildPrefixes(const StyleContext& context) const
{
    const std::string elementSuffix = std::string(elementString(context.element));
    const std::vector<std::string> fragments = variantFragments(context.variant);

    std::vector<std::string> activeStates;
    for (const StyleState stateFlag : statePriorityOrder) {
        if (context.state.testFlag(stateFlag)) {
            activeStates.push_back(std::string(stateString(stateFlag)));
        }
    }

    std::vector<std::string> prefixes;

    if (!context.componentOverride.empty()) {
        appendPrefixEntries(prefixes, context.componentOverride + elementSuffix, fragments, activeStates);
    }

    for (const std::string& componentName : chain(context.component)) {
        appendPrefixEntries(prefixes, componentName + elementSuffix, fragments, activeStates);
    }

    return prefixes;
}

ParameterDescriptorRegistry::MatchedVariants ParameterDescriptorRegistry::matchedVariantsOf(
    const ParameterDescriptor& descriptor,
    const ParsedParameterName& parsed
) const
{
    MatchedVariants matched;

    for (const std::string& variantName : variantNamesFor(descriptor)) {
        const auto variantIt = _variants.find(variantName);
        if (variantIt == _variants.end()) {
            continue;
        }

        const auto valueIt = parsed.variants.find(variantName);
        if (valueIt == parsed.variants.end()) {
            continue;  // variant absent, so the default contributes nothing
        }

        if (variantIt->second.kind == ParameterVariantKind::Variant) {
            matched.fragments.push_back(valueIt->second);
        }
        else {
            matched.activeStates.push_back(valueIt->second);
        }
    }

    return matched;
}

std::vector<std::string> ParameterDescriptorRegistry::buildPrefixesFromParsed(
    const ParsedParameterName& parsed
) const
{
    const ParameterDescriptor* descriptor = this->descriptor(parsed.component);
    if (!descriptor) {
        return {};
    }

    const MatchedVariants matched = matchedVariantsOf(*descriptor, parsed);

    std::vector<std::string> prefixes;
    for (const std::string& componentBase : resolveChainNames(parsed.component)) {
        appendPrefixEntries(prefixes, componentBase, matched.fragments, matched.activeStates);
    }

    return prefixes;
}

std::span<const std::string> ParameterDescriptorRegistry::chain(StyleComponent component) const
{
    const auto it = _componentChains.find(component);
    if (it == _componentChains.end()) {
        static const std::vector<std::string> empty;
        return empty;
    }
    return it->second;
}

const ParameterDescriptor* ParameterDescriptorRegistry::descriptor(const std::string& name) const
{
    const auto it = _descriptors.find(name);
    return it != _descriptors.end() ? &it->second : nullptr;
}

std::optional<StyleComponent> ParameterDescriptorRegistry::findComponent(std::string_view name) const
{
    const auto iterator = _componentByName.find(name);
    if (iterator == _componentByName.end()) {
        return std::nullopt;
    }
    return iterator->second;
}

std::optional<StyleComponentElement> ParameterDescriptorRegistry::findElement(std::string_view name) const
{
    const auto found = std::ranges::find_if(elementNames, [name](const auto& entry) {
        return entry.second == name;
    });

    if (found == elementNames.end()) {
        return std::nullopt;
    }

    return found->first;
}

const std::map<std::string, ParameterVariant>& ParameterDescriptorRegistry::variants() const
{
    return _variants;
}

const std::vector<std::string>& ParameterDescriptorRegistry::sortedNames() const
{
    if (_sortedNamesDirty) {
        _sortedNames.clear();
        _sortedNames.reserve(_descriptors.size());
        for (const auto& [name, _] : _descriptors) {
            _sortedNames.push_back(name);
        }
        // Longest name first to ensure greedy prefix matching is unambiguous.
        std::ranges::sort(_sortedNames, [](const std::string& lhs, const std::string& rhs) {
            return lhs.size() > rhs.size();
        });
        _sortedNamesDirty = false;
    }
    return _sortedNames;
}

void ParameterDescriptorRegistry::appendPrefixEntries(
    std::vector<std::string>& prefixes,
    const std::string& componentBase,
    const std::vector<std::string>& variantFragments,
    const std::vector<std::string>& activeStates
) const
{
    // Each variant combination (most specific first) is tried with each active state, then
    // without a state. The final combination is always the empty (base) one, so this emits
    // the plain component and component+state entries last.
    for (const std::string& variant : variantCombinations(variantFragments)) {
        for (const std::string& stateStr : activeStates) {
            prefixes.push_back(componentBase + variant + stateStr);
        }
        prefixes.push_back(componentBase + variant);
    }
}

std::vector<std::string> ParameterDescriptorRegistry::resolveChainNames(const std::string& name) const
{
    std::vector<std::string> chain;
    chain.push_back(name);

    const ParameterDescriptor* desc = descriptor(name);
    if (!desc) {
        return chain;
    }

    for (const std::string& parent : desc->inherits) {
        chain.push_back(parent);
    }

    return chain;
}

std::vector<std::string> ParameterDescriptorRegistry::variantNamesFor(
    const ParameterDescriptor& descriptor
) const
{
    std::vector<std::string> names = descriptor.variants;
    names.insert(names.end(), _globalVariantNames.begin(), _globalVariantNames.end());

    return names;
}

void registerBuiltinVariants(ParameterDescriptorRegistry& registry)
{
    // TransparencyMode applies to every component: buildPrefixes() walks all variant slots
    // regardless of the descriptor, so parse() has to recognise it everywhere too.
    constexpr auto globalSlots = std::to_array({VariantSlot::TransparencyMode});

    // variantNamesFor() appends global variants after the descriptor's own, so parse() only
    // matches them in declaration order if every global slot is declared last. A global slot
    // placed before a per-descriptor one silently mis-parses qualified token names.
    constexpr size_t firstGlobalSlot = static_cast<size_t>(VariantSlot::COUNT) - globalSlots.size();
    static_assert(
        std::ranges::all_of(
            globalSlots,
            [](VariantSlot slot) { return static_cast<size_t>(slot) >= firstGlobalSlot; }
        ),
        "Global variant slots must occupy the last VariantSlot positions - move them to the "
        "end of the enum, or teach variantNamesFor() to interleave them by slot order."
    );

    // Variant-kind dimensions — derived from the canonical variantSlotNames tables.
    for (size_t index = 0; index < variantSlotDisplayNames.size(); ++index) {
        const auto variantSlot = static_cast<VariantSlot>(index);
        const auto& valueMap = lookup(variantSlotNames, variantSlot);

        ParameterVariant variant;
        variant.name = std::string(variantSlotDisplayNames.at(index));
        variant.kind = ParameterVariantKind::Variant;
        variant.values.reserve(valueMap.size());
        for (const auto& [key, name] : valueMap) {
            variant.values.emplace_back(name);
        }

        const bool isGlobal = std::ranges::find(globalSlots, variantSlot) != globalSlots.end();

        if (isGlobal) {
            registry.registerGlobalVariant(std::move(variant));
        }
        else {
            registry.registerVariant(std::move(variant));
        }
    }

    // State-kind dimension — derived from the canonical stateNames table.
    // Order matches statePriorityOrder (highest priority first).
    ParameterVariant stateVariant;
    stateVariant.name = "State";
    stateVariant.kind = ParameterVariantKind::State;
    stateVariant.values.reserve(statePriorityOrder.size());
    for (const StyleState stateFlag : statePriorityOrder) {
        stateVariant.values.emplace_back(stateNames.at(stateFlag));
    }
    registry.registerVariant(std::move(stateVariant));
}

void populateBuiltinDescriptors(ParameterDescriptorRegistry& registry)
{
    // clang-format off
    registerBuiltinVariants(registry);

    // Virtual bases (no StyleComponent mapping).
    registry.registerDescriptor({
        .name     = "FormControl",
        .variants = {"ControlSize", "State"},
        .inherits = {},
    });

    // Concrete components mapped to StyleComponent enum values.
    registry.registerDescriptor({
        .name     = "Button",
        .variants = {"ButtonType", "ControlSize", "State"},
        .inherits = {"FormControl"},
    }, StyleComponent::PushButton);

    registry.registerDescriptor({
        .name     = "ToolButton",
        .variants = {"ControlSize", "State"},
        .inherits = {"Button", "FormControl"},
    }, StyleComponent::ToolButton);

    registry.registerDescriptor({
        .name     = "ToolBarButton",
        .variants = {"ControlSize", "State"},
        .inherits = {"ToolButton", "Button", "FormControl"},
    }, StyleComponent::ToolBarButton);

    registry.registerDescriptor({
        .name     = "GeometrySelector",
        .variants = {"ControlSize", "State", "RowType"},
        .inherits = {"List"},
    }, StyleComponent::GeometrySelector);

    // Simplified, solid-filled action button for custom composite widgets. It
    // inherits FormControl directly (not Button) so it carries none of Button's
    // gradient background or hover/pressed effects.
    registry.registerDescriptor({
        .name     = "InternalButton",
        .variants = {"ControlSize", "State"},
        .inherits = {"FormControl"},
    }, StyleComponent::InternalButton);

    registry.registerDescriptor({
        .name     = "GroupBox",
        .variants = {"FrameType", "ControlSize", "State"},
        .inherits = {},
    }, StyleComponent::GroupBox);

    registry.registerDescriptor({
        .name     = "Tooltip",
        .variants = {"State"},
        .inherits = {},
    }, StyleComponent::Tooltip);

    registry.registerDescriptor({
        .name     = "ToolBar",
        .variants = {"Position", "State"},
        .inherits = {},
    }, StyleComponent::ToolBar);

    // A dock panel paints on its content widget rather than on the QDockWidget, whose body a
    // docked layout leaves fully covered. Position is the dock area, so a single canonical
    // North token rotates into an edge that always faces the central widget.
    registry.registerDescriptor({
        .name     = "Panel",
        .variants = {"Position", "State"},
        .inherits = {},
    }, StyleComponent::Panel);

    registry.registerDescriptor({
        .name     = "MenuBar",
        .variants = {"State"},
        .inherits = {},
    }, StyleComponent::MenuBar);

    registry.registerDescriptor({
        .name     = "LineEdit",
        .variants = {"ControlSize", "State"},
        .inherits = {"FormControl"},
    }, StyleComponent::LineEdit);

    registry.registerDescriptor({
        .name     = "TextEdit",
        .variants = {"ControlSize", "State"},
        .inherits = {"LineEdit", "FormControl"},
    }, StyleComponent::TextEdit);

    registry.registerDescriptor({
        .name     = "Select",
        .variants = {"ControlSize", "State"},
        .inherits = {"Button", "FormControl"},
    }, StyleComponent::Select);

    registry.registerDescriptor({
        .name     = "ComboBox",
        .variants = {"ControlSize", "State"},
        .inherits = {"LineEdit", "FormControl"},
    }, StyleComponent::ComboBox);

    registry.registerDescriptor({
        .name     = "List",
        .variants = {"RowType", "State"},
        .inherits = {},
    }, StyleComponent::List);

    registry.registerDescriptor({
        .name     = "DropdownList",
        .variants = {"RowType", "State"},
        .inherits = {"List"},
    }, StyleComponent::DropdownList);

    registry.registerDescriptor({
        .name     = "Tree",
        .variants = {"RowType", "State"},
        .inherits = {"List"},
    }, StyleComponent::Tree);

    registry.registerDescriptor({
        .name     = "TabBar",
        .variants = {"Position", "State"},
        .inherits = {},
    }, StyleComponent::TabBar);

    registry.registerDescriptor({
        .name     = "TabWidget",
        .variants = {"Position", "State"},
        .inherits = {},
    }, StyleComponent::TabWidget);

    registry.registerDescriptor({
        .name     = "Header",
        .variants = {"State"},
        .inherits = {},
    }, StyleComponent::Header);

    // A menu is deliberately standalone rather than inheriting List: it has no rows, no
    // alternate parity and no selection model, and List's hover and selection colours live
    // on the Row element, which a MenuItem context could never reach. The theme borrows
    // List's values instead, which leaves menus independently tunable.
    registry.registerDescriptor({
        .name     = "Menu",
        .variants = {"CheckType", "State"},
        .inherits = {},
    }, StyleComponent::Menu);

    registry.registerDescriptor({
        .name     = "CheckBox",
        .variants = {"ControlSize", "State"},
        .inherits = {"FormControl"},
    }, StyleComponent::CheckBox);

    registry.registerDescriptor({
        .name     = "RadioButton",
        .variants = {"ControlSize", "State"},
        .inherits = {"CheckBox", "FormControl"},
    }, StyleComponent::RadioButton);
    // clang-format on
}

}  // namespace Gui::StyleParameters
