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

#include <FCGlobal.h>
#include <QString>

namespace Gui::StyleParameters
{

class ParameterManager;

/**
 * @brief Exports resolved design tokens to W3C Design Tokens Community Group format.
 *
 * Reads all parameters from the manager, resolves them to their concrete values, and
 * serialises them as a W3C DTCG-compatible JSON document. Each resolved token becomes
 * a leaf entry `{ "$type": "...", "$value": ... }` nested under its parameter name.
 * Tuple values are expanded recursively: named elements use their name as the key,
 * unnamed (positional) elements use their zero-based index.
 *
 * Only Color, Numeric, and plain-string values are exported; None values are silently
 * skipped.
 */
class GuiExport TokenExporter
{
public:
    explicit TokenExporter(const ParameterManager& manager);

    /// Resolve all tokens and return the W3C Design Tokens JSON as a pretty-printed string.
    QString exportW3CTokens() const;

private:
    const ParameterManager* manager;
};

}  // namespace Gui::StyleParameters
