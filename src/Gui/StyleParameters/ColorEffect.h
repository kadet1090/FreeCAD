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

#ifndef STYLEPARAMETERS_COLOREFFECT_H
#define STYLEPARAMETERS_COLOREFFECT_H

#include <algorithm>
#include <optional>

#include <Base/Exception.h>
#include <Base/OkLch.h>
#include <fmt/format.h>

#include "Diagnostics.h"
#include "Value.h"

namespace Gui::StyleParameters
{

/**
 * @brief Wrapper for ColorEffect tuples.
 *
 * Represents a perceptually-uniform lightness modification in OkLch space.
 * Created via the effect() parser function.
 *
 * Parameters:
 *   - lighten: dimensionless amount to add to OkLch lightness (default 0.0)
 *   - darken:  dimensionless amount to subtract from OkLch lightness (default 0.0)
 *
 * The net lightness delta is lighten - darken, clamped to [0, 1] after application.
 * Because OkLch is perceptually uniform, the same delta produces the same perceived
 * change regardless of the base color — unlike overlay-based approaches.
 */
class ColorEffect
{
public:
    /**
     * @brief Constructs from a raw args Tuple (as received from the parser).
     *
     * If the tuple already has kind ColorEffect, it is used as-is.
     * Otherwise it is treated as positional/named arguments and expanded.
     */
    explicit ColorEffect(const Tuple& args)
        : tuple_(args.kind == TupleKind::ColorEffect ? args : expand(args))
        , lightnessShift_(lighten() - darken())
    {
        if (tuple_.kind != TupleKind::ColorEffect) {
            THROWM(
                Base::TypeError,
                fmt::format("Expected color_effect tuple, got {}", tupleKindName(tuple_.kind))
            );
        }
    }

    /**
     * @brief Constructs from a Value (convenience overload for token resolution).
     */
    explicit ColorEffect(const Value& value)
        : ColorEffect(asTuple(value))
    {}

    /// Amount to add to OkLch lightness (dimensionless, default 0.0).
    float lighten() const
    {
        return static_cast<float>(tuple_.get<Numeric>("lighten"));
    }

    /// Amount to subtract from OkLch lightness (dimensionless, default 0.0).
    float darken() const
    {
        return static_cast<float>(tuple_.get<Numeric>("darken"));
    }

    /**
     * @brief Applies the effect to a base color using OkLch lightness manipulation.
     *
     * Converts the base color to OkLch, adjusts lightness by (lighten - darken),
     * clamps to [0, 1], and converts back to sRGB. Alpha is preserved.
     */
    Base::Color apply(const Base::Color& base) const
    {
        Base::OkLch lch = Base::toOkLch(base);
        lch.lightness = std::clamp(lch.lightness + lightnessShift_, 0.0f, 1.0f);
        return Base::fromOkLch(lch, base.a);
    }

    const Tuple& tuple() const
    {
        return tuple_;
    }

    static constexpr TupleKind kind()
    {
        return TupleKind::ColorEffect;
    }

    /**
     * @brief Builds a ColorEffect from a value, or nothing when the value is not one.
     *
     * The entry point resolve<ColorEffect>() validates through. A token of some other shape yields
     * nothing, leaving the caller's own default in place rather than throwing out of a paint path.
     */
    static std::optional<ColorEffect> tryFrom(const Value& value)
    {
        const Tuple source = asTuple(value);

        if (source.kind != TupleKind::ColorEffect) {
            Diagnostics::report(
                "Expected {} tuple, got {}",
                tupleKindName(TupleKind::ColorEffect),
                tupleKindName(source.kind)
            );
            return std::nullopt;
        }

        return ColorEffect(source);
    }

private:
    Tuple tuple_;
    float lightnessShift_;

    static Tuple expand(const Tuple& args)
    {
        Tuple resolved = ArgumentParser {
            {.name = "lighten", .defaultValue = Numeric {.value = 0.0, .unit = ""}},
            {.name = "darken",  .defaultValue = Numeric {.value = 0.0, .unit = ""}},
        }.resolve(args);

        resolved.kind = TupleKind::ColorEffect;
        return resolved;
    }
};

}  // namespace Gui::StyleParameters

#endif  // STYLEPARAMETERS_COLOREFFECT_H
