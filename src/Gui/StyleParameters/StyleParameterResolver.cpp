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

#include "StyleParameterResolver.h"

#include <set>
#include <string>

#include <Base/ScopeExit.h>

#include "ParameterManager.h"

namespace Gui::StyleParameters
{

// ─── NaiveParameterResolver ───────────────────────────────────────────────────

std::optional<Value> NaiveParameterResolver::resolve(
    const std::string& name,
    const ParameterManager* manager
) const
{
    return manager->resolve(name, {});
}

// ─── ChainedParameterResolver ─────────────────────────────────────────────────

ChainedParameterResolver::ChainedParameterResolver(std::vector<StyleParameterResolver*> resolvers)
    : _resolvers(std::move(resolvers))
{}

std::optional<Value> ChainedParameterResolver::resolve(
    const std::string& name,
    const ParameterManager* manager
) const
{
    for (StyleParameterResolver* resolver : _resolvers) {
        if (auto result = resolver->resolve(name, manager)) {
            return result;
        }
    }
    return std::nullopt;
}

void ChainedParameterResolver::refresh()
{
    for (StyleParameterResolver* resolver : _resolvers) {
        resolver->refresh();
    }
}

// ─── CachingParameterResolver ─────────────────────────────────────────────────

CachingParameterResolver::CachingParameterResolver(StyleParameterResolver* inner)
    : _inner(inner)
{}

std::optional<Value> CachingParameterResolver::resolve(
    const std::string& name,
    const ParameterManager* manager
) const
{
    const auto [it, inserted] = _cache.try_emplace(name);
    if (!inserted) {
        return it->second;
    }

    it->second = _inner->resolve(name, manager);
    return it->second;
}

void CachingParameterResolver::refresh()
{
    _cache.clear();
    _inner->refresh();
}

// ─── InheritingParameterResolver ─────────────────────────────────────────────

namespace
{
// Guard against re-entrant synthesis of the same name. Kept here rather than as a static member
// because MSVC rejects thread-local data members on a dll-exported class (C2492); nothing outside
// this file needs it anyway.
thread_local std::set<std::string> beingSynthesized;
}  // namespace

std::optional<Value> InheritingParameterResolver::resolve(
    const std::string& name,
    const ParameterManager* manager
) const
{
    // Re-entrancy guard: if we're already synthesizing this name, the chain
    // walker has looped back to the same virtual token — return nullopt so the
    // outer call can advance to the next prefix.
    if (beingSynthesized.contains(name)) {
        return std::nullopt;
    }

    const auto parsed = manager->descriptorRegistry().parse(name);
    if (!parsed) {
        return std::nullopt;
    }

    const std::vector<std::string> prefixes = manager->descriptorRegistry().buildPrefixesFromParsed(
        *parsed
    );

    beingSynthesized.insert(name);
    Base::ScopeExit cleanup([&] { beingSynthesized.erase(name); });

    for (const std::string& prefix : prefixes) {
        auto result = manager->resolve(prefix + parsed->property);
        if (!result) {
            continue;
        }
        return result;
    }

    return std::nullopt;
}

}  // namespace Gui::StyleParameters
