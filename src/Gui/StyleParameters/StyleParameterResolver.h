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

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <FCGlobal.h>

#include "Value.h"

namespace Gui::StyleParameters
{
class ParameterManager;
}

namespace Gui::StyleParameters
{

/**
 * @brief Abstract interface for resolving style parameters by name.
 *
 * Implementations may perform flat source lookup, chain-based synthesis,
 * caching, or any combination thereof. The interface intentionally carries
 * no cache; each implementation decides whether and how to cache internally.
 */
class GuiExport StyleParameterResolver
{
public:
    virtual ~StyleParameterResolver() = default;

    /**
     * @brief Resolves a parameter by name.
     *
     * @param name    The parameter name, e.g. "ButtonBorderColor".
     * @param manager The ParameterManager to use for flat lookups and sub-resolution.
     * @return The resolved value, or nullopt if not found.
     */
    virtual std::optional<Value> resolve(const std::string& name, const ParameterManager* manager) const
        = 0;

    /**
     * @brief Refreshes any internal state of the resolver, like clearing caches etc.
     *
     * The default implementation does nothing. Override in caching wrappers.
     */
    virtual void refresh()
    {}
};

/**
 * @brief Flat, exact-name resolver backed by ParameterManager sources.
 *
 * Performs source lookup + expression evaluation for the given name. Carries
 * no cache — wrap with CachingParameterResolver for cached access.
 */
class GuiExport NaiveParameterResolver: public StyleParameterResolver
{
public:
    std::optional<Value> resolve(const std::string& name, const ParameterManager* manager) const override;
};

/**
 * @brief Ordered resolver combinator — tries each delegate in sequence.
 *
 * Returns the first non-nullopt result from the list of resolvers. Carries
 * no cache of its own; cache individual resolvers externally if desired.
 */
class GuiExport ChainedParameterResolver: public StyleParameterResolver
{
public:
    explicit ChainedParameterResolver(std::vector<StyleParameterResolver*> resolvers);

    std::optional<Value> resolve(const std::string& name, const ParameterManager* manager) const override;
    void refresh() override;

private:
    std::vector<StyleParameterResolver*> _resolvers;
};

/**
 * @brief Cache decorator — wraps any StyleParameterResolver and memoises results.
 *
 * All lookups are cached in a flat name → optional<Value> map. The cache
 * is the single authoritative result store for string-name resolution;
 * it should sit at the outermost level so both naive and inheriting resolvers
 * benefit from it.
 */
class GuiExport CachingParameterResolver: public StyleParameterResolver
{
public:
    explicit CachingParameterResolver(StyleParameterResolver* inner);

    std::optional<Value> resolve(const std::string& name, const ParameterManager* manager) const override;

    /**
     * @brief Clears the name→value cache and propagates to the inner resolver.
     */
    void refresh() override;

private:
    StyleParameterResolver* _inner;
    mutable std::unordered_map<std::string, std::optional<Value>> _cache;
};

/**
 * @brief Descriptor-driven chain-synthesis resolver.
 *
 * For string-name resolution: parses the name via ParameterDescriptorRegistry,
 * builds the ordered fallback prefix list, and walks it by calling
 * manager->resolve() for each candidate name. Per-prefix lookups go through
 * the full chained resolver (including caching), so nested virtual parameters
 * are resolved correctly.
 *
 * Re-entrancy is prevented by a thread-local `beingSynthesized` set: when
 * InheritingParameterResolver begins synthesizing name X it adds X to the set;
 * any re-entrant resolve for X returns nullopt, allowing the chain to advance.
 * Stateless — carries no cache.
 */
class GuiExport InheritingParameterResolver: public StyleParameterResolver
{
public:
    InheritingParameterResolver() = default;
    FC_DISABLE_COPY(InheritingParameterResolver);

    /**
     * @brief Resolves a parameter name via descriptor-driven chain synthesis.
     *
     * Parses the name, builds the fallback prefix list, and returns the first result the
     * full resolver chain answers with. Returns nullopt if the name is unrecognised or the
     * chain is exhausted.
     */
    std::optional<Value> resolve(const std::string& name, const ParameterManager* manager) const override;
};

}  // namespace Gui::StyleParameters
