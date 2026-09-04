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

#ifndef GUI_STYLEPARAMETERS_STYLEOVERRIDES_H
#define GUI_STYLEPARAMETERS_STYLEOVERRIDES_H

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <FCGlobal.h>

namespace Gui::StyleParameters
{

/**
 * @brief One widget's style token overrides, as token name to expression string.
 *
 * Ordered, so two sets that were built in a different order still compare equal and therefore
 * still deduplicate to one entry in the registry.
 */
using OverrideSet = std::map<std::string, std::string>;

/**
 * @brief Deduplicates override sets and identifies each one by a stable id.
 *
 * Widgets declaring the same overrides share an id, and therefore share one stored copy and one
 * resolution cache. Id 0 always means "no overrides" and is never assigned to a real set.
 *
 * Ids are handed out for the lifetime of the process and are never reused: widgets hold them,
 * so an id must not start meaning something else. In particular the registry is **not** cleared
 * on a theme reload — expressions do not change there, only the values they resolve to.
 */
class GuiExport OverrideRegistry
{
public:
    static constexpr uint32_t emptyId = 0;

    OverrideRegistry() = default;

    // byId holds pointers into ids's map nodes; a copy would point into the original's storage.
    // Moving a std::map preserves node addresses, so move stays safe and available.
    FC_DISABLE_COPY(OverrideRegistry);
    FC_DEFAULT_MOVE(OverrideRegistry);

    /// Returns the id for @p set, assigning a new one the first time that content is seen.
    uint32_t intern(const OverrideSet& set);

    /// Returns the set @p id names, or the empty set if it names nothing.
    const OverrideSet& get(uint32_t id) const;

    /// How many distinct non-empty sets are held.
    std::size_t size() const;

private:
    std::map<OverrideSet, uint32_t> ids;
    std::vector<const OverrideSet*> byId;
};

}  // namespace Gui::StyleParameters

#endif  // GUI_STYLEPARAMETERS_STYLEOVERRIDES_H
