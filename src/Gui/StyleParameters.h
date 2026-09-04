// SPDX-License-Identifier: LGPL-2.1-or-later

/****************************************************************************
 *                                                                          *
 *   Copyright (c) 2025 The FreeCAD Project Association AISBL               *
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

#include <Gui/StyleParameters/ParameterManager.h>

namespace Gui::StyleParameters
{
// The expression button is a fixed square rather than a themed box: it sits inside a line
// edit's own frame, so its size decides the text margin the edit has to leave clear.
DEFINE_STYLE_PARAMETER(ExpressionButtonSize, Numeric(18, "px"));

// Reference highlighting in the 3D view. Blue so it reads as distinct from selection green,
// preselection yellow, and the PartDesign preview colours. Each primitive kind carries its own
// alpha: a face is see-through so the surface under it still reads, while its boundary stays
// solid so the outline stays crisp.
DEFINE_STYLE_PARAMETER(GeometryHighlightReferenceFaceColor, Base::Color(0.20F, 0.55F, 1.00F, 0.35F));
DEFINE_STYLE_PARAMETER(GeometryHighlightReferenceEdgeColor, Base::Color(0.20F, 0.55F, 1.00F));
DEFINE_STYLE_PARAMETER(GeometryHighlightReferencePointColor, Base::Color(0.20F, 0.55F, 1.00F));
DEFINE_STYLE_PARAMETER(GeometryHighlightReferenceLineWidth, Numeric(3));
DEFINE_STYLE_PARAMETER(GeometryHighlightHoveredFaceColor, Base::Color(0.45F, 0.78F, 1.00F, 0.55F));
DEFINE_STYLE_PARAMETER(GeometryHighlightHoveredEdgeColor, Base::Color(0.45F, 0.78F, 1.00F));
DEFINE_STYLE_PARAMETER(GeometryHighlightHoveredPointColor, Base::Color(0.45F, 0.78F, 1.00F));
DEFINE_STYLE_PARAMETER(GeometryHighlightHoveredLineWidth, Numeric(4));
}  // namespace Gui::StyleParameters
