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

#include "StyleOverrides.h"

namespace Gui::StyleParameters
{

namespace
{

const OverrideSet& emptySet()
{
    static const OverrideSet instance;
    return instance;
}

}  // namespace

uint32_t OverrideRegistry::intern(const OverrideSet& set)
{
    if (set.empty()) {
        return emptyId;
    }

    if (byId.empty()) {
        byId.push_back(&emptySet());
    }

    const auto [entry, inserted] = ids.try_emplace(set, static_cast<uint32_t>(byId.size()));
    if (inserted) {
        // std::map nodes are stable, so the key outlives every id handed out for it.
        byId.push_back(&entry->first);
    }

    return entry->second;
}

const OverrideSet& OverrideRegistry::get(uint32_t id) const
{
    if (id == emptyId || id >= byId.size()) {
        return emptySet();
    }

    return *byId[id];
}

std::size_t OverrideRegistry::size() const
{
    return ids.size();
}

}  // namespace Gui::StyleParameters
