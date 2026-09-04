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

#include <utility>

namespace Base
{

/**
 * @brief Executes a callable when the ScopeExit object goes out of scope.
 *
 * Ensures cleanup actions run on all exit paths, including exception paths,
 * without depending on Boost or C++23.
 *
 * Example:
 * @code
 * set.insert(name);
 * Base::ScopeExit cleanup([&] { set.erase(name); });
 * // ... code that may throw ...
 * @endcode
 */
template<typename F>
class ScopeExit
{
    F _func;

public:
    explicit ScopeExit(F func)
        : _func(std::move(func))
    {}

    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;
    ScopeExit(ScopeExit&&) = delete;
    ScopeExit& operator=(ScopeExit&&) = delete;

    ~ScopeExit()
    {
        _func();
    }
};

template<typename F>
ScopeExit(F) -> ScopeExit<F>;

}  // namespace Base
