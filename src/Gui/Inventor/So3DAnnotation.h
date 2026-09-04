// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2024 Kacper Donat <kacper@kadet.net>                    *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/
#pragma once

#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/elements/SoElement.h>
#include <Inventor/elements/SoSubElement.h>
#include <Inventor/fields/SoSFInt32.h>
#include <FCGlobal.h>
#include <vector>

namespace Gui
{

/*! @brief Draw order of an annotation within the delayed-path overlay pipeline.
 *
 * Everything that draws over the scene shares one pipeline; the layer is what
 * orders an annotation against the others. Lower layers draw first, so a higher
 * layer appears on top.
 *
 * These are spaced so that a caller with several related annotations can offset
 * from a named layer without colliding with the next one.
 *
 * The layer is only honoured on the outermost So3DAnnotation of a subtree. An
 * annotation nested below another one is drawn as part of its ancestor's layer
 * and its own layer value is ignored, so put the layer on the outermost node.
 */
enum class AnnotationLayer : int
{
    Overlay = 0,       //!< Preview shapes and datums. The default.
    Selection = 1000,  //!< The on-top selection and preselection groups.
    Highlight = 2000,  //!< The reference and hover highlight groups.
    Handle = 3000,     //!< Draggers, gizmos, the axis cross, placement indicators.
};

class GuiExport SoDelayedAnnotationsElement: public SoElement
{
    using inherited = SoElement;

    SO_ELEMENT_HEADER(SoDelayedAnnotationsElement);

protected:
    ~SoDelayedAnnotationsElement() override = default;

    SoDelayedAnnotationsElement& operator=(const SoDelayedAnnotationsElement& other) = default;
    SoDelayedAnnotationsElement& operator=(SoDelayedAnnotationsElement&& other) noexcept = default;

    // internal structure to hold path with it's rendering
    // priority (lower renders first)
    struct PriorityPath
    {
        SoPath* path;
        int priority;

        PriorityPath(SoPath* p, int pr = 0)
            : path(p)
            , priority(pr)
        {}
    };

public:
    SoDelayedAnnotationsElement(const SoDelayedAnnotationsElement& other) = delete;
    SoDelayedAnnotationsElement(SoDelayedAnnotationsElement&& other) noexcept = delete;

    void init(SoState* state) override;

    static void initClass();

    static void addDelayedPath(SoState* state, SoPath* path, int priority = 0);

    static bool hasDelayedPaths(SoState* state);

    static void processDelayedPathsWithPriority(SoState* state, SoGLRenderAction* action);

    /*! @brief Drop the paths accumulated on @p state without drawing them.
     *
     * For renderers that deliberately produce no overlay, such as the offscreen
     * ones behind saved pictures and thumbnails. Without this the deferred paths
     * are never released and pile up on the render action for its whole lifetime.
     */
    static void discardDelayedPaths(SoState* state);

    static bool isProcessingDelayedPaths;

    SbBool matches([[maybe_unused]] const SoElement* element) const override
    {
        return FALSE;
    }

    SoElement* copyMatchInfo() const override
    {
        return nullptr;
    }

private:
    static SoDelayedAnnotationsElement* getElement(SoState* state);

    std::vector<PriorityPath> paths;
};

/*! @brief 3D Annotation Node - Annotation with depth buffer
 *
 * This class is just like SoAnnotation with the difference that it does not disable
 * the depth buffer instead it clears it and renders on top of everything with proper
 * depth control.
 *
 * It should be used with caution as it does clear the depth buffer for each annotation!
 */
class GuiExport So3DAnnotation: public SoSeparator
{
    using inherited = SoSeparator;

    SO_NODE_HEADER(So3DAnnotation);

public:
    static bool render;

    /// Where this annotation draws relative to the other overlay annotations.
    SoSFInt32 layer;

    So3DAnnotation();

    So3DAnnotation(const So3DAnnotation& other) = delete;
    So3DAnnotation(So3DAnnotation&& other) noexcept = delete;
    So3DAnnotation& operator=(const So3DAnnotation& other) = delete;
    So3DAnnotation& operator=(So3DAnnotation&& other) noexcept = delete;

    static void initClass();

    void GLRender(SoGLRenderAction* action) override;
    void GLRenderBelowPath(SoGLRenderAction* action) override;
    void GLRenderInPath(SoGLRenderAction* action) override;
    void GLRenderOffPath(SoGLRenderAction* action) override;

protected:
    ~So3DAnnotation() override = default;
};

}  // namespace Gui
