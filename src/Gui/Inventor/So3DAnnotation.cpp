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

#include <FCConfig.h>

#ifdef FC_OS_MACOSX
# include <OpenGL/gl.h>
#else
# ifdef FC_OS_WIN32
#  include <windows.h>
# endif
# include <GL/gl.h>
#endif

#include <Inventor/elements/SoCacheElement.h>
#include <Inventor/lists/SoPathList.h>
#include <algorithm>

#include "So3DAnnotation.h"
#include <Base/Tools.h>
#include <Gui/Selection/Selection.h>

using namespace Gui;

SO_ELEMENT_SOURCE(SoDelayedAnnotationsElement);

bool SoDelayedAnnotationsElement::isProcessingDelayedPaths = false;

void SoDelayedAnnotationsElement::init(SoState* state)
{
    SoElement::init(state);
    paths.clear();
}

void SoDelayedAnnotationsElement::initClass()
{
    SO_ELEMENT_INIT_CLASS(SoDelayedAnnotationsElement, inherited);

    SO_ENABLE(SoGLRenderAction, SoDelayedAnnotationsElement);
}

SoDelayedAnnotationsElement* SoDelayedAnnotationsElement::getElement(SoState* state)
{
    return static_cast<SoDelayedAnnotationsElement*>(state->getElementNoPush(classStackIndex));
}

void SoDelayedAnnotationsElement::addDelayedPath(SoState* state, SoPath* path, int priority)
{
    // add to unified storage with specified priority (default = 0)
    getElement(state)->paths.emplace_back(path, priority);
}

bool SoDelayedAnnotationsElement::hasDelayedPaths(SoState* state)
{
    return !getElement(state)->paths.empty();
}

void SoDelayedAnnotationsElement::processDelayedPathsWithPriority(SoState* state, SoGLRenderAction* action)
{
    auto elt = static_cast<SoDelayedAnnotationsElement*>(state->getElementNoPush(classStackIndex));

    if (elt->paths.empty()) {
        return;
    }

    std::stable_sort(
        elt->paths.begin(),
        elt->paths.end(),
        [](const PriorityPath& first, const PriorityPath& second) {
            return first.priority < second.priority;
        }
    );

    // Move the paths out of the element. Replay needs a list it owns, and the element
    // has to end up empty anyway so the next traversal starts from nothing; the swap
    // does both in one step. Nothing appends while the loop runs: So3DAnnotation::render
    // makes annotations draw inline instead of deferring, and the SoBrep*Set add-sites
    // are gated on isProcessingDelayedPaths.
    std::vector<PriorityPath> ordered;
    ordered.swap(elt->paths);

    Base::StateLocker processing(isProcessingDelayedPaths, true);

    bool clearedForHandles = false;

    // One apply per layer, not per path. Each apply runs its own nested delayed-path
    // phase on the way out, and that phase is where a nested SoFCPathAnnotation draws;
    // finishing a layer before starting the next is what keeps the layers ordered.
    for (auto layerBegin = ordered.begin(); layerBegin != ordered.end();) {
        const int currentLayer = layerBegin->priority;
        const auto layerEnd
            = std::find_if(layerBegin, ordered.end(), [currentLayer](const PriorityPath& candidate) {
                  return candidate.priority != currentLayer;
              });

        SoPathList batch;
        for (auto entry = layerBegin; entry != layerEnd; ++entry) {
            batch.append(entry->path);
        }

        // Handles have to beat everything under them, but the layers below do write depth
        // (a preview shape is a filled faceset) and every apply restores depth testing on
        // the way in. Clearing once, at the boundary, frees the handles from that without
        // changing how the layers below occlude each other.
        if (!clearedForHandles && currentLayer >= static_cast<int>(AnnotationLayer::Handle)) {
            glClear(GL_DEPTH_BUFFER_BIT);
            clearedForHandles = true;
        }

        // Apply without obeysrules: Coin would otherwise sort and split the batch by head
        // node, and SoCompactPathList requires every path in it to share one head. The
        // batch can hold paths from different roots, since the element outlives a single
        // apply and every root traversed with this action feeds the same one.
        action->apply(batch, FALSE);

        layerBegin = layerEnd;
    }
}

void SoDelayedAnnotationsElement::discardDelayedPaths(SoState* state)
{
    auto elt = static_cast<SoDelayedAnnotationsElement*>(state->getElementNoPush(classStackIndex));

    if (elt->paths.empty()) {
        return;
    }

    // The copies handed to addDelayedPath arrive at refcount 0 and are held as raw
    // pointers, so appending them to a list that refs on append and unrefs on destruction
    // is what actually frees them.
    SoPathList discarded;
    for (const PriorityPath& entry : elt->paths) {
        discarded.append(entry.path);
    }

    elt->paths.clear();
}

SO_NODE_SOURCE(So3DAnnotation);

bool So3DAnnotation::render = false;

So3DAnnotation::So3DAnnotation()
{
    SO_NODE_CONSTRUCTOR(So3DAnnotation);

    SO_NODE_ADD_FIELD(layer, (static_cast<int>(AnnotationLayer::Overlay)));
}

void So3DAnnotation::initClass()
{
    SO_NODE_INIT_CLASS(So3DAnnotation, SoSeparator, "3DAnnotation");
}

void So3DAnnotation::GLRender(SoGLRenderAction* action)
{
    switch (action->getCurPathCode()) {
        case SoAction::NO_PATH:
        case SoAction::BELOW_PATH:
            this->GLRenderBelowPath(action);
            break;
        case SoAction::OFF_PATH:
            // do nothing. Separator will reset state.
            break;
        case SoAction::IN_PATH:
            this->GLRenderInPath(action);
            break;
    }
}

void So3DAnnotation::GLRenderBelowPath(SoGLRenderAction* action)
{
    if (render) {
        inherited::GLRenderBelowPath(action);
    }
    else {
        SoCacheElement::invalidate(action->getState());
        SoDelayedAnnotationsElement::addDelayedPath(
            action->getState(),
            action->getCurPath()->copy(),
            layer.getValue()
        );
    }
}

void So3DAnnotation::GLRenderInPath(SoGLRenderAction* action)
{
    if (render) {
        inherited::GLRenderInPath(action);
    }
    else {
        SoCacheElement::invalidate(action->getState());
        SoDelayedAnnotationsElement::addDelayedPath(
            action->getState(),
            action->getCurPath()->copy(),
            layer.getValue()
        );
    }
}

void So3DAnnotation::GLRenderOffPath(SoGLRenderAction* /* action */)
{
    // should never render, this is a separator node
}
