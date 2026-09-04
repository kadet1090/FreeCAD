/****************************************************************************
 *   Copyright (c) 2022 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 *                                                                          *
 ****************************************************************************/

#pragma once

#include <array>
#include <cstddef>
#include <map>
#include <string>
#include <vector>
#include <Inventor/SbColor.h>
#include <Base/Color.h>
#include <Gui/GeometryReference.h>
#include <Gui/Selection/Selection.h>

class SoDrawStyle;
class SoGroup;
class SoNode;
class SoSeparator;
class SoTempPath;

namespace App
{
class DocumentObject;
}

namespace Gui
{

class Document;
class SoFCUnifiedSelection;
class So3DAnnotation;
class ViewProviderDocumentObject;

/// One highlight role's colours, one per primitive kind, so a face can be see-through
/// while the edges and vertices bounding it stay solid.
struct GuiExport HighlightRoleColors
{
    Base::Color face;
    Base::Color edge;
    Base::Color point;
};

class GuiExport View3DInventorSelection
{
public:
    View3DInventorSelection(SoFCUnifiedSelection* root);
    ~View3DInventorSelection();

    void setDocument(Gui::Document* pcDocument)
    {
        guiDocument = pcDocument;
    }
    Gui::Document* getDocument() const
    {
        return guiDocument;
    }

    void checkGroupOnTop(const SelectionChanges& Reason);
    void clearGroupOnTop();

    /// Sets the colours and line width every subsequent highlight of @p role uses.
    void setHighlightStyle(HighlightRole role, const HighlightRoleColors& colors, float lineWidth);
    /// Renders @p object's @p subNames on top in @p role's style, attributed to
    /// @p owner so it can be withdrawn without disturbing anyone else's. A
    /// whole-object reference is an empty entry in @p subNames. Every name is
    /// resolved together in one call so that references sharing a scene-graph path
    /// — every element of one object, when its view provider does not distinguish
    /// them in the path it returns — share a single annotation instead of one
    /// replaying the object per reference. Silently does nothing when the reference
    /// cannot be resolved, when its object is hidden, or when it belongs to a
    /// document other than this one.
    void addHighlight(
        HighlightRole role,
        const void* owner,
        App::DocumentObject* object,
        const std::vector<std::string>& subNames
    );
    /// Removes the highlights @p owner added under @p role, leaving every other
    /// owner's in place.
    void clearHighlight(HighlightRole role, const void* owner);

private:
    /// Prefixes @p path with the scene-graph path through every enclosing
    /// geo-feature group. False when @p vp is hidden inside one of them, which
    /// means no on-top rendering is possible.
    bool appendGroupPath(ViewProviderDocumentObject* vp, SoTempPath& path) const;

    /// One highlight role's scene-graph slot: the draw style shared by every
    /// annotation of that role, the colour subsequent ones are given, and one
    /// subgroup per owner so each owner's annotations can be withdrawn alone.
    struct HighlightRoleNodes
    {
        SoGroup* group = nullptr;
        SoDrawStyle* style = nullptr;
        HighlightRoleColors colors {
            .face = Base::Color(0.20F, 0.55F, 1.00F, 0.35F),
            .edge = Base::Color(0.20F, 0.55F, 1.00F),
            .point = Base::Color(0.20F, 0.55F, 1.00F),
        };
        std::map<const void*, SoGroup*> owners;
    };

    HighlightRoleNodes* highlightRole(HighlightRole role);
    /// The subgroup holding @p owner's annotations of a role, created on first use.
    static SoGroup* highlightOwnerGroup(HighlightRoleNodes& nodes, const void* owner);
    /// Draws @p elements of @p vp under @p owner's subgroup of @p nodes. Elements
    /// that resolve onto the same scene-graph path share one annotation, so their
    /// secondary selection contexts are written together rather than one element
    /// blanking the nodes another one targets. Elements that do not resolve are
    /// skipped.
    void addHighlightElements(
        HighlightRoleNodes& nodes,
        const void* owner,
        ViewProviderDocumentObject& vp,
        const std::vector<std::string>& elements
    );

    /// Parents pcGroupOnTop so it draws in the Selection layer of the overlay pipeline.
    So3DAnnotation* pcGroupOnTopLayer;
    SoGroup* pcGroupOnTop;
    SoGroup* pcGroupOnTopSel;
    SoGroup* pcGroupOnTopPreSel;
    /// Parents pcGroupHighlight so it draws in the Highlight layer, above pcGroupOnTop.
    So3DAnnotation* pcGroupHighlightLayer;
    SoGroup* pcGroupHighlight;
    std::array<HighlightRoleNodes, highlightRoleCount> highlightRoles;
    SoFCUnifiedSelection* selectionRoot;
    std::map<std::string, SoNode*> objectsOnTop;
    std::map<std::string, SoNode*> objectsOnTopPreSel;
    Gui::Document* guiDocument = nullptr;
};

}  // namespace Gui
