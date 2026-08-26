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

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

#include <Base/Bitmask.h>
#include <FCGlobal.h>

class QWidget;

namespace Gui::StyleParameters
{

/**
 * @brief Widget components that participate in style token resolution.
 *
 * (used in token names) is the `.name` passed to registerDescriptor() in
 * ParameterDescriptorRegistry.cpp.
 */
enum class StyleComponent : uint8_t
{
    None,
    PushButton,
    ToolButton,
    ToolBar,        // QToolBar
    Select,         // QComboBox that is not editable; inherits Button
    ComboBox,       // QComboBox that is editable; inherits LineEdit
    LineEdit,       // QLineEdit and the edit frame of a QAbstractSpinBox
    TextEdit,       // QPlainTextEdit, QTextEdit and derivatives
    CheckBox,       // QCheckBox indicator
    RadioButton,    // QRadioButton indicator
    List,           // QListWidget, QListView and other item views
    DropdownList,   // QListView inside a QComboBox popup
    Tree,           // QTreeWidget, QTreeView
    Header,         // QHeaderView sections
    Menu,           // QMenu popup: context menus, menu bar and tool button dropdowns
    MenuBar,        // QMenuBar
    ToolBarButton,  // QToolButton in a QToolBar - semantically a different control
    COUNT
};

/**
 * @brief Sub-elements of widget component that participate in style token resolution.
 *
 * to specify part of the component that we are interested in, like Item of List.
 *
 * The cache key gives this enum 4 bits (16 values). Adding a 17th requires repacking
 * StyleContext::cacheKey(); the key is 64-bit and has room, but it is not automatic.
 */
enum class StyleComponentElement : uint8_t
{
    Root,  // Main component
    Item,  // One item of the component
    Row,   // One row of the component, spanning its full width
    Indicator, // Check or radio glyph belonging to the component
    Branch,    // Connector lines in the indent column of a tree view
    IconIndicator, // State box drawn behind a checkable menu item's icon
    Menu,  // Dropdown strip of a MenuButtonPopup tool button
    Separator, // Separator rule, and the label of an addSection() header, inside a menu
    Arrow, // Submenu arrow of a menu item
    Shortcut,  // Accelerator column of a menu item
    COUNT,
};

/**
 * @brief Visual type variant for button-like components.
 *
 * Derived from Qt widget properties:
 *   - Primary : QPushButton::isDefault() or QStyleOptionButton::DefaultButton feature
 *   - Link    : QPushButton::isFlat(), QToolButton::autoRaise(), or property("flat") == true
 *   - Default : everything else
 */
enum class ButtonType : uint8_t
{
    Default,
    Primary,
    Link,
    COUNT
};

/**
 * @brief Size variant, derived from the "controlSize" widget property.
 */
enum class ControlSize : uint8_t
{
    Default,
    Internal,  // compact 18px controls embedded inside composite widgets
    Small,
    Big,
    COUNT
};

/**
 * @brief Edge position: the edge at which a component attaches.
 *
 * North (0) is canonical. Geometric tokens are resolved with North and rotated to the
 * actual position; visual tokens are resolved with the actual position so per-position
 * colour overrides (e.g. TabBarTabSouthBackground) work naturally.
 */
enum class Position : uint8_t
{
    North = 0,  // canonical; maps to "" in token names (default variant)
    East,       // "East"
    South,      // "South"
    West,       // "West"
    COUNT
};

/**
 * @brief Interaction state bitmask — multiple flags may be active simultaneously.
 *
 * Token resolution expands active flags into a fallback prefix list in priority
 * order (highest priority first): Disabled > Pressed > Hovered > Checked > Selected > Focused.
 */
enum class StyleState : uint8_t
{
    Normal = 0,
    Focused = 1 << 0,
    Selected = 1 << 1,
    Checked = 1 << 2,
    Hovered = 1 << 3,
    Pressed = 1 << 4,
    Disabled = 1 << 5,
};

/**
 * @brief Selects the treatment a component uses when it must not paint an opaque surface.
 *
 * Transparent is reached two ways, which mean different things and must not be
 * conflated. A toolbar hosted in the status bar or as a QMenuBar corner widget
 * blends into an otherwise opaque host, so it suppresses its own chrome but the
 * surface beneath its children is still solid. A widget carrying the propagated
 * transparency tag genuinely sits over a see-through surface, and that is the only
 * form that its children inherit.
 */
enum class TransparencyMode : uint8_t
{
    Normal,
    Transparent,
    COUNT
};

/**
 * @brief Whether a checkable menu item belongs to an exclusive group.
 *
 * Mirrors QStyleOptionMenuItem::checkType. No theme states a value for it, so an exclusive
 * item looks exactly like a non-exclusive one; the slot exists so a theme can give the two
 * different shapes - a round well for a group, a square one for a checkbox.
 */
enum class CheckType : uint8_t
{
    Default,
    Exclusive,
    COUNT
};

/**
 * @brief Row parity variant for item-view components.
 *
 * Set to Alternate when QStyleOptionViewItem::Alternate is active so that alternate-row
 * tokens are tried before falling back to the default-row ones.
 */
enum class RowType : uint8_t
{
    Default,
    Alternate,
    COUNT
};

/**
 * @brief Registry of variant dimensions used in token names.
 *
 * Each slot corresponds to one enum dimension (ButtonType, ControlSize, …).
 */
enum class VariantSlot : uint8_t
{
    ButtonType,
    ControlSize,
    Position,
    RowType,           // Alternate row parity for item-view components
    CheckType,         // Exclusive (radio) vs non-exclusive check state on a menu item
    TransparencyMode,  // Applies to every component, so it is declared last
    COUNT
};

/**
 * @brief Holds one uint8_t value per VariantSlot, defaulting to 0 (the Default
 *        value of each dimension's enum).
 *
 * The array size is determined at compile time by VariantSlot::COUNT, so adding
 * a new slot automatically expands the array — no manual size management needed.
 */
struct VariantKey
{
    std::array<uint8_t, size_t(VariantSlot::COUNT)> slots = {};

    template<typename EnumT>
    void set(VariantSlot slot, EnumT value)
    {
        slots.at(size_t(slot)) = uint8_t(value);
    }

    uint8_t get(VariantSlot slot) const
    {
        return slots.at(size_t(slot));
    }

    bool operator==(const VariantKey&) const = default;
};

/**
 * @brief Style properties that can be resolved from tokens.
 *
 * Each value corresponds to a token suffix (e.g. Padding → "Padding").
 */
enum class StyleProperty : uint8_t
{
    Width,
    MinWidth,
    MaxWidth,
    Height,
    MinHeight,
    MaxHeight,
    BorderThickness,
    BorderRadius,
    BorderColor,
    Padding,
    Margin,
    Spacing,
    Overlap,
    IconSize,
    IconSpacing,
    FontSize,
    FontWeight,
    Background,
    TextColor,
    InnerShadow,
    IconColor,
    BackgroundEffect,
    BorderColorEffect,
    FrameWidth,
    IsTransparent,
    Placement,
    PlacementOffset,
    COUNT,
};

/**
 * @brief Fully describes the styling context for a widget in a given state.
 *
 * Built once per draw call via FreeCADStyle::contextOf(), then passed to
 * resolve() and resolveBoxBackground() for cached token lookup.
 */
struct GuiExport StyleContext
{
    StyleComponent component = StyleComponent::None;
    StyleComponentElement element = StyleComponentElement::Root;
    VariantKey variant = {};
    Base::Flags<StyleState> state;
    /**
     * @brief Optional component name override, read from the widget's "component" dynamic property.
     *
     * When non-empty, this string is prepended to the normal component chain as an additional
     * prefix, allowing widgets to opt into custom token namespaces (e.g. "ActionButton") while
     * still falling back to the standard chain (Button → FormControl).
     */
    std::string componentOverride;

    /**
     * @brief The widget this context describes, when there is one.
     *
     * Carried so token resolution can find the style overrides declared on the widget or
     * inherited from its ancestors. Which overrides those are, and how they are identified, is
     * internal to FreeCADStyle and ParameterManager.
     */
    const QWidget* widget = nullptr;

    bool operator==(const StyleContext&) const = default;

    /**
     * @brief Interns componentOverride strings to compact uint8_t IDs for cache key packing.
     *
     * Id 0 is reserved for "no override". Each unique string is assigned a new id
     * starting from 1 on first encounter. Call clear() when invalidating caches
     * that embed these ids (e.g. on theme change).
     */
    struct GuiExport Intern
    {
        /// Returns the process-wide singleton intern table.
        static Intern& global();

        uint8_t intern(const std::string& name);
        void clear();

    private:
        std::unordered_map<std::string, uint8_t> ids;
        uint8_t nextId = 1;
    };

    /**
     * @brief Returns a 64-bit context-only cache key (property bits left zero).
     *
     * The key does not cover `widget`. It identifies an entry *within* one override-set cache
     * bin; the bin itself is chosen by the caller from the widget.
     */
    uint64_t cacheKey() const;

    /// Returns a full 64-bit cache key including the property dimension.
    uint64_t cacheKey(StyleProperty property) const;

private:
    // clang-format off
    // Each field is given its natural width, so adding a component, element, state, property or
    // variant does not need these offsets revisited; cacheKey() asserts every field against them.
    // A new variant *dimension* is the one case that still needs an edit, though not here: its
    // COUNT has to join the per-slot assert in cacheKey(), as VariantSlot's docs describe.
    static constexpr uint8_t componentBitOffset = 0;   //  8 bits
    static constexpr uint8_t elementBitOffset   = 8;   //  4 bits
    static constexpr uint8_t stateBitOffset     = 12;  //  8 bits
    static constexpr uint8_t propertyBitOffset  = 20;  //  8 bits
    static constexpr uint8_t overrideBitOffset  = 28;  //  8 bits
    static constexpr uint8_t variantBitOffset   = 36;  // VariantSlot::COUNT * variantSlotBitWidth
    // clang-format on

    // The variant field is exactly full at 7 slots * 4 bits = 28, landing offset 36 on 64: there
    // is no headroom left above it. An 8th VariantSlot does not fit without repacking — widening
    // a field above, shrinking variantSlotBitWidth, or moving to a wider key — so budget that
    // work in whenever VariantSlot::COUNT is about to grow past 7.

    /// Width of one VariantSlot's field within the packed variant array.
    static constexpr uint8_t variantSlotBitWidth = 4;

    static uint64_t packVariant(const VariantKey& variant);
};

/**
 * @brief Returns the token-name suffix string for a style property.
 *
 * Returns an empty string_view for unknown properties.
 */
GuiExport std::string_view propertyString(StyleProperty property);

}  // namespace Gui::StyleParameters

ENABLE_BITMASK_OPERATORS(Gui::StyleParameters::StyleState)
