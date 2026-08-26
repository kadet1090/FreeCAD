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

#include "StyleContext.h"

#include <algorithm>
#include <limits>
#include <type_traits>

namespace Gui::StyleParameters
{

// ─── StyleContext::Intern ─────────────────────────────────────────────────────

StyleContext::Intern& StyleContext::Intern::global()
{
    static Intern instance;
    return instance;
}

uint8_t StyleContext::Intern::intern(const std::string& name)
{
    if (const auto found = ids.find(name); found != ids.end()) {
        return found->second;
    }

    const uint8_t id = nextId++;
    ids.emplace(name, id);
    return id;
}

void StyleContext::Intern::clear()
{
    ids.clear();
    nextId = 1;
}

// ─── StyleContext cache key packing ───────────────────────────────────────────

/*static*/ uint64_t StyleContext::packVariant(const VariantKey& variant)
{
    uint64_t packed = 0;
    for (size_t index = 0; index < variant.slots.size(); ++index) {
        packed |= static_cast<uint64_t>(variant.slots.at(index)) << (index * variantSlotBitWidth);
    }
    return packed;
}

uint64_t StyleContext::cacheKey() const
{
    static_assert(
        static_cast<uint64_t>(StyleComponent::COUNT)
            <= (uint64_t {1} << (elementBitOffset - componentBitOffset)),
        "StyleComponent no longer fits in the component field packed at componentBitOffset"
    );
    static_assert(
        static_cast<uint64_t>(StyleComponentElement::COUNT)
            <= (uint64_t {1} << (stateBitOffset - elementBitOffset)),
        "StyleComponentElement no longer fits in the element field packed at elementBitOffset"
    );
    // A full byte, so every StyleState flag fits by construction rather than by inspection.
    static_assert(
        propertyBitOffset - stateBitOffset
            >= std::numeric_limits<std::underlying_type_t<StyleState>>::digits,
        "StyleState no longer fits in the state field packed at stateBitOffset"
    );
    static_assert(
        static_cast<uint64_t>(StyleProperty::COUNT)
            <= (uint64_t {1} << (overrideBitOffset - propertyBitOffset)),
        "StyleProperty no longer fits in the property field packed at propertyBitOffset"
    );
    static_assert(
        std::numeric_limits<uint8_t>::digits <= variantBitOffset - overrideBitOffset,
        "The interned component override no longer fits in the field packed at overrideBitOffset"
    );
    static_assert(
        static_cast<uint64_t>(VariantSlot::COUNT) * variantSlotBitWidth <= 64 - variantBitOffset,
        "VariantKey no longer fits in the variant field packed at variantBitOffset"
    );
    // Slot widths are checked separately from the field as a whole: a dimension that outgrows its
    // own slot would overflow into its neighbour's without changing the size of the variant field.
    static_assert(
        std::max({
            static_cast<uint64_t>(ButtonType::COUNT),
            static_cast<uint64_t>(ControlSize::COUNT),
            static_cast<uint64_t>(Position::COUNT),
            static_cast<uint64_t>(RowType::COUNT),
            static_cast<uint64_t>(CheckType::COUNT),
            static_cast<uint64_t>(TransparencyMode::COUNT),
        }) <= (uint64_t {1} << variantSlotBitWidth),
        "A variant dimension no longer fits in its slot of the variant field"
    );

    const uint8_t overrideId = componentOverride.empty()
        ? static_cast<uint8_t>(0)
        : Intern::global().intern(componentOverride);

    // clang-format off
    return (static_cast<uint64_t>(component)                << componentBitOffset)
         | (static_cast<uint64_t>(element)                  << elementBitOffset)
         | (static_cast<uint64_t>(state.toUnderlyingType()) << stateBitOffset)
         | (static_cast<uint64_t>(overrideId)               << overrideBitOffset)
         | (packVariant(variant)                            << variantBitOffset);
    // clang-format on
}

uint64_t StyleContext::cacheKey(StyleProperty property) const
{
    return cacheKey() | (static_cast<uint64_t>(property) << propertyBitOffset);
}

}  // namespace Gui::StyleParameters
