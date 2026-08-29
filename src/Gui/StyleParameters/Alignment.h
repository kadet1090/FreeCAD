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
#include <string_view>
#include <utility>

#include "Value.h"

namespace Gui::StyleParameters
{

/// Where a label sits along the direction its text runs.
enum class HorizontalAlign : std::uint8_t
{
    Left,
    Center,
    Right,
};

/// Where a label sits across the direction its text runs.
enum class VerticalAlign : std::uint8_t
{
    Top,
    Middle,
    Bottom,
};

// clang-format off
template<>
struct KeywordEnum<HorizontalAlign>
{
    static constexpr auto keywords = std::to_array<std::pair<std::string_view, HorizontalAlign>>({
        {"left",   HorizontalAlign::Left},
        {"start",  HorizontalAlign::Left},
        {"center", HorizontalAlign::Center},
        {"right",  HorizontalAlign::Right},
        {"end",    HorizontalAlign::Right},
    });
};

template<>
struct KeywordEnum<VerticalAlign>
{
    static constexpr auto keywords = std::to_array<std::pair<std::string_view, VerticalAlign>>({
        {"top",    VerticalAlign::Top},
        {"middle", VerticalAlign::Middle},
        {"bottom", VerticalAlign::Bottom},
    });
};
// clang-format on

}  // namespace Gui::StyleParameters
