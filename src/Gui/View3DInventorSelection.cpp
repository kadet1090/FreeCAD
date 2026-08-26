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


#include <Inventor/SoPath.h>
#include <Inventor/details/SoDetail.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoPickStyle.h>
#include <Inventor/nodes/SoSeparator.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "Application.h"
#include "Document.h"
#include "Inventor/So3DAnnotation.h"
#include "SoFCUnifiedSelection.h"
#include "Utilities.h"
#include "View3DInventorSelection.h"
#include "ViewProviderDocumentObject.h"
#include <App/Document.h>
#include <App/GeoFeature.h>
#include <App/GeoFeatureGroupExtension.h>
#include <Base/Console.h>

FC_LOG_LEVEL_INIT("3DViewerSelection", true, true)

using namespace Gui;

namespace
{
/// The scene-graph node name a highlight role's subgroup carries, for the sake of
/// anyone reading the graph in the scene inspector.
const char* highlightRoleName(HighlightRole role)
{
    switch (role) {
        case HighlightRole::Reference:
            return "HighlightReference";
        case HighlightRole::Hovered:
            return "HighlightHovered";
        case HighlightRole::COUNT:
            break;
    }
    return "Highlight";
}

/// Whether an annotation already replays exactly @p candidate, and so can carry
/// its detail instead of a second annotation being made for it.
bool sameNodes(SoPath* stored, const SoTempPath& candidate)
{
    if (!stored || stored->getLength() != candidate.getLength()) {
        return false;
    }
    for (int index = 0; index < candidate.getLength(); ++index) {
        if (stored->getNode(index) != candidate.getNode(index)) {
            return false;
        }
    }
    return true;
}

/// Writes one element's index into the secondary selection context of every node
/// @p path reaches, below @p node. A null @p det means the whole object rather
/// than one of its elements.
void applyHighlightDetail(
    SoGroup* roleGroup,
    SoGroup* ownerGroup,
    SoFCPathAnnotation* node,
    SoTempPath& path,
    const SoDetail* det,
    const SbColor& color
)
{
    // The secondary context is what narrows the render down to the detail; an
    // overriding material would instead repaint the whole object. It carries the
    // colour too, which is what a shape node without a primary context reads.
    SoSelectionElementAction action(
        det ? SoSelectionElementAction::Append : SoSelectionElementAction::All,
        true
    );
    action.setColor(color);
    action.setElement(det);

    SoTempPath tmpPath(path.getLength() + 3);
    tmpPath.ref();
    tmpPath.append(roleGroup);
    tmpPath.append(ownerGroup);
    tmpPath.append(node);
    tmpPath.append(&path);
    action.apply(&tmpPath);
    tmpPath.unrefNoDelete();
}

/// Runs @p action over everything @p ownerGroup replays, entered through
/// @p roleGroup. The path matters as much as the destination: a selection context
/// is keyed by the chain of SoFCSelectionRoot nodes leading to the querying node,
/// so an action applied to the owner subgroup alone would write a context the
/// render traversal — which arrives through the role group — never looks up.
void applyToOwnerGroup(SoGroup* roleGroup, SoGroup* ownerGroup, SoSelectionElementAction& action)
{
    SoTempPath path(2);
    path.ref();
    path.append(roleGroup);
    path.append(ownerGroup);
    action.apply(&path);
    path.unrefNoDelete();
}

/// The colour @p colors gives an element named @p elementName, chosen by its kind.
Base::Color colorForElement(const HighlightRoleColors& colors, const std::string& elementName)
{
    if (elementName.starts_with("Edge")) {
        return colors.edge;
    }
    if (elementName.starts_with("Vertex")) {
        return colors.point;
    }
    return colors.face;
}

/// The per-element colour map @p elements resolve to. A named element takes the colour of
/// its kind. A whole-object reference — an empty name — becomes one bare prefix per kind,
/// which each shape node reads as "all of mine", so the three kinds keep their own alphas
/// instead of sharing one wildcard.
std::map<std::string, Base::Color> highlightColorMap(
    const std::vector<std::string>& elements,
    const HighlightRoleColors& colors
)
{
    std::map<std::string, Base::Color> byElement;
    for (const std::string& element : elements) {
        if (element.empty()) {
            byElement.emplace("Face", colors.face);
            byElement.emplace("Edge", colors.edge);
            byElement.emplace("Vertex", colors.point);
            continue;
        }
        byElement.emplace(element, colorForElement(colors, element));
    }
    return byElement;
}

/// Writes a per-element colour, alpha included, into the secondary context of every node
/// @p node replays. Scoped to that one annotation rather than the whole owner group: an
/// owner group holds every object's annotations under this reference, and broadcasting the
/// action across it (as applyToOwnerGroup() does) would let a later object's call overwrite
/// an earlier object's colours through the shared secondary context Coin keys by node path.
/// Must run after the Append/All actions that narrow the render: the shape nodes' Color
/// handlers create a select-all context when none exists yet, and a select-all context
/// renders the whole object rather than the picked element.
void applyHighlightElementColors(
    SoGroup* roleGroup,
    SoGroup* ownerGroup,
    SoFCPathAnnotation* node,
    const std::map<std::string, Base::Color>& byElement
)
{
    if (byElement.empty()) {
        return;
    }
    SoSelectionElementAction action(SoSelectionElementAction::Color, true);
    action.setColors(byElement);

    SoPath* path = node->getPath();
    SoTempPath tmpPath(3 + (path ? path->getLength() : 0));
    tmpPath.ref();
    tmpPath.append(roleGroup);
    tmpPath.append(ownerGroup);
    tmpPath.append(node);
    tmpPath.append(path);
    action.apply(&tmpPath);
    tmpPath.unrefNoDelete();
}
}  // namespace

View3DInventorSelection::View3DInventorSelection(SoFCUnifiedSelection* root)
    : selectionRoot(root)
{
    selectionRoot->ref();

    pcGroupOnTop = new SoSeparator;
    pcGroupOnTop->ref();
    pcGroupOnTop->setName("GroupOnTop");

    // The overlay groups share one delayed-path pipeline with the preview shapes and
    // the draggers. The layer is what orders them against those: a preview must not
    // paint over a selection, and a dragger must stay reachable above both.
    pcGroupOnTopLayer = new So3DAnnotation;
    pcGroupOnTopLayer->ref();
    pcGroupOnTopLayer->setName("GroupOnTopLayer");
    pcGroupOnTopLayer->layer = static_cast<int>(AnnotationLayer::Selection);
    pcGroupOnTopLayer->addChild(pcGroupOnTop);
    root->addChild(pcGroupOnTopLayer);

    auto pcGroupOnTopPickStyle = new SoPickStyle;
    pcGroupOnTopPickStyle->style = SoPickStyle::UNPICKABLE;
    pcGroupOnTopPickStyle->setOverride(true);
    pcGroupOnTopPickStyle->setName("GroupOnTopPickStyle");
    pcGroupOnTop->addChild(pcGroupOnTopPickStyle);

    coin_setenv("COIN_SEPARATE_DIFFUSE_TRANSPARENCY_OVERRIDE", "1", TRUE);
    auto pcGroupOnTopMaterial = new SoMaterial;
    pcGroupOnTopMaterial->transparency = 0.5;
    pcGroupOnTopMaterial->diffuseColor.setIgnored(true);
    pcGroupOnTopMaterial->setOverride(true);
    pcGroupOnTopMaterial->setName("GroupOnTopMaterial");
    pcGroupOnTop->addChild(pcGroupOnTopMaterial);

    {
        auto selRoot = new SoFCSelectionRoot;
        selRoot->selectionStyle = SoFCSelectionRoot::PassThrough;
        pcGroupOnTopSel = selRoot;
        pcGroupOnTopSel->setName("GroupOnTopSel");
        pcGroupOnTopSel->ref();
        pcGroupOnTop->addChild(pcGroupOnTopSel);
    }

    {
        auto selRoot = new SoFCSelectionRoot;
        selRoot->selectionStyle = SoFCSelectionRoot::PassThrough;
        pcGroupOnTopPreSel = selRoot;
        pcGroupOnTopPreSel->setName("GroupOnTopPreSel");
        pcGroupOnTopPreSel->ref();
        pcGroupOnTop->addChild(pcGroupOnTopPreSel);
    }

    // A sibling of GroupOnTop rather than a child: GroupOnTopMaterial forces
    // transparency 0.5 with override, which would wash out every highlight. Added
    // after it so a reference highlight draws over an ordinary selection highlight.
    pcGroupHighlight = new SoSeparator;
    pcGroupHighlight->ref();
    pcGroupHighlight->setName("GroupHighlight");

    pcGroupHighlightLayer = new So3DAnnotation;
    pcGroupHighlightLayer->ref();
    pcGroupHighlightLayer->setName("GroupHighlightLayer");
    pcGroupHighlightLayer->layer = static_cast<int>(AnnotationLayer::Highlight);
    pcGroupHighlightLayer->addChild(pcGroupHighlight);
    root->addChild(pcGroupHighlightLayer);

    auto pcHighlightPickStyle = new SoPickStyle;
    pcHighlightPickStyle->style = SoPickStyle::UNPICKABLE;
    pcHighlightPickStyle->setOverride(true);
    pcHighlightPickStyle->setName("GroupHighlightPickStyle");
    pcGroupHighlight->addChild(pcHighlightPickStyle);

    const auto makeRoleGroup = [this](const char* name, HighlightRoleNodes& role) {
        auto selRoot = new SoFCSelectionRoot;
        selRoot->selectionStyle = SoFCSelectionRoot::PassThrough;
        role.group = selRoot;
        role.group->setName(name);
        role.group->ref();

        role.style = new SoDrawStyle;
        // Only the line width is ours to force. An override applies to every field
        // that is not ignored, which would otherwise also pin the replayed geometry
        // to FILLED, a zero point size and a solid line pattern — a vertex reference
        // would lose its point size and a dashed view provider its pattern.
        role.style->style.setIgnored(true);
        role.style->pointSize.setIgnored(true);
        role.style->linePattern.setIgnored(true);
        role.style->setOverride(true);
        role.group->addChild(role.style);

        pcGroupHighlight->addChild(role.group);
    };

    // Every slot, not the two named ones: a role added later that was missed here
    // would leave a null group for the destructor to unref.
    for (std::size_t index = 0; index < highlightRoleCount; ++index) {
        makeRoleGroup(highlightRoleName(static_cast<HighlightRole>(index)), highlightRoles.at(index));
    }
}

View3DInventorSelection::~View3DInventorSelection()
{
    selectionRoot->unref();
    pcGroupOnTopLayer->unref();
    pcGroupOnTop->unref();
    pcGroupOnTopPreSel->unref();
    pcGroupOnTopSel->unref();
    pcGroupHighlightLayer->unref();
    pcGroupHighlight->unref();
    for (HighlightRoleNodes& role : highlightRoles) {
        role.group->unref();
    }
}

void View3DInventorSelection::checkGroupOnTop(const SelectionChanges& Reason)
{
    if (Reason.Type == SelectionChanges::SetSelection
        || Reason.Type == SelectionChanges::ClrSelection) {
        clearGroupOnTop();
        if (Reason.Type == SelectionChanges::ClrSelection) {
            return;
        }
    }
    if (Reason.Type == SelectionChanges::RmvPreselect
        || Reason.Type == SelectionChanges::RmvPreselectSignal) {
        SoSelectionElementAction action(SoSelectionElementAction::None, true);
        action.apply(pcGroupOnTopPreSel);
        coinRemoveAllChildren(pcGroupOnTopPreSel);
        objectsOnTopPreSel.clear();
        return;
    }
    if (!getDocument() || !Reason.pDocName || !Reason.pDocName[0] || !Reason.pObjectName) {
        return;
    }
    auto obj = getDocument()->getDocument()->getObject(Reason.pObjectName);
    if (!obj || !obj->isAttachedToDocument()) {
        return;
    }
    std::string key(obj->getNameInDocument());
    key += '.';
    auto subname = Reason.pSubName;
    App::ElementNamePair element;
    App::GeoFeature::resolveElement(obj, Reason.pSubName, element);
    if (Data::isMappedElement(subname) && !element.oldName.empty()) {  // If we have a shortened
                                                                       // element name
        subname = element.oldName.c_str();                             // use if
    }
    if (subname) {
        key += subname;
    }
    if (Reason.Type == SelectionChanges::RmvSelection) {
        auto& objs = objectsOnTop;
        auto pcGroup = pcGroupOnTopSel;
        auto it = objs.find(key.c_str());
        if (it == objs.end()) {
            return;
        }
        int index = pcGroup->findChild(it->second);
        if (index >= 0) {
            auto node = static_cast<SoFCPathAnnotation*>(it->second);
            SoSelectionElementAction action(
                node->getDetail() ? SoSelectionElementAction::Remove : SoSelectionElementAction::None,
                true
            );
            auto path = node->getPath();
            SoTempPath tmpPath(2 + (path ? path->getLength() : 0));
            tmpPath.ref();
            tmpPath.append(pcGroup);
            tmpPath.append(node);
            tmpPath.append(node->getPath());
            action.setElement(node->getDetail());
            action.apply(&tmpPath);
            tmpPath.unrefNoDelete();
            pcGroup->removeChild(index);
            FC_LOG("remove annotation " << Reason.Type << " " << key);
        }
        else {
            FC_LOG("remove annotation object " << Reason.Type << " " << key);
        }
        objs.erase(it);
        return;
    }

    auto& objs = Reason.Type == SelectionChanges::SetPreselect ? objectsOnTopPreSel : objectsOnTop;
    auto pcGroup = Reason.Type == SelectionChanges::SetPreselect ? pcGroupOnTopPreSel
                                                                 : pcGroupOnTopSel;

    if (objs.find(key.c_str()) != objs.end()) {
        return;
    }
    auto vp = freecad_cast<ViewProviderDocumentObject*>(Application::Instance->getViewProvider(obj));
    if (!vp || !vp->isSelectable() || !vp->isShow()) {
        return;
    }
    auto svp = vp;
    if (subname && *subname) {
        auto sobj = obj->getSubObject(subname);
        if (!sobj || !sobj->isAttachedToDocument()) {
            return;
        }
        if (sobj != obj) {
            svp = freecad_cast<ViewProviderDocumentObject*>(
                Application::Instance->getViewProvider(sobj)
            );
            if (!svp || !svp->isSelectable()) {
                return;
            }
        }
    }
    int onTop;
    // onTop==2 means on top only if whole object is selected,
    // onTop==3 means on top only if some sub-element is selected
    // onTop==1 means either
    if (vp->OnTopWhenSelected.getValue()) {
        onTop = vp->OnTopWhenSelected.getValue();
    }
    else {
        onTop = svp->OnTopWhenSelected.getValue();
    }
    if (Reason.Type == SelectionChanges::SetPreselect) {
        SoHighlightElementAction action;
        action.setHighlighted(true);
        action.setColor(selectionRoot->colorHighlight.getValue());
        action.apply(pcGroupOnTopPreSel);
        if (!onTop) {
            onTop = 2;
        }
    }
    else {
        if (!onTop) {
            return;
        }
        SoSelectionElementAction action(SoSelectionElementAction::All);
        action.setColor(selectionRoot->colorSelection.getValue());
        action.apply(pcGroupOnTopSel);
    }
    if (onTop == 2 || onTop == 3) {
        if (subname && *subname) {
            size_t len = strlen(subname);
            if (subname[len - 1] == '.') {
                // ending with '.' means whole object selection
                if (onTop == 3) {
                    return;
                }
            }
            else if (onTop == 2) {
                return;
            }
        }
        else if (onTop == 3) {
            return;
        }
    }

    SoTempPath path(10);
    path.ref();

    if (!appendGroupPath(vp, path)) {
        path.unrefNoDelete();
        return;
    }

    SoDetail* det = nullptr;
    if (vp->getDetailPath(subname, &path, true, det) && path.getLength()) {
        auto node = new SoFCPathAnnotation;
        node->setPath(&path);
        pcGroup->addChild(node);
        if (det) {
            SoSelectionElementAction action(SoSelectionElementAction::Append, true);
            action.setElement(det);
            SoTempPath tmpPath(path.getLength() + 2);
            tmpPath.ref();
            tmpPath.append(pcGroup);
            tmpPath.append(node);
            tmpPath.append(&path);
            action.apply(&tmpPath);
            tmpPath.unrefNoDelete();
            node->setDetail(det);
            det = nullptr;
        }
        FC_LOG("add annotation " << Reason.Type << " " << key);
        objs[key.c_str()] = node;
    }
    delete det;
    path.unrefNoDelete();
}

bool View3DInventorSelection::appendGroupPath(ViewProviderDocumentObject* vp, SoTempPath& path) const
{
    std::vector<ViewProvider*> groups;
    auto grpVp = vp;
    std::set<ViewProvider*> visited;
    for (auto childVp = vp;; childVp = grpVp) {
        auto grp = App::GeoFeatureGroupExtension::getGroupOfObject(childVp->getObject());
        if (!grp || !grp->isAttachedToDocument()) {
            break;
        }

        grpVp = freecad_cast<ViewProviderDocumentObject*>(Application::Instance->getViewProvider(grp));
        if (!grpVp) {
            break;
        }

        // avoid endless-loops
        if (!visited.insert(childVp).second) {
            break;
        }

        auto childRoot = grpVp->getChildRoot();
        auto modeSwitch = grpVp->getModeSwitch();
        auto idx = modeSwitch->whichChild.getValue();
        if (idx < 0 || idx >= modeSwitch->getNumChildren() || modeSwitch->getChild(idx) != childRoot) {
            FC_LOG("skip " << vp->getObject()->getFullName() << ", hidden inside geo group");
            return false;
        }
        if (childRoot->findChild(childVp->getRoot()) < 0) {
            FC_LOG(
                "cannot find '" << childVp->getObject()->getFullName() << "' in geo group '"
                                << grp->getNameInDocument() << "'"
            );
            break;
        }
        groups.push_back(grpVp);
    }

    for (auto it = groups.rbegin(); it != groups.rend(); ++it) {
        auto grpVp = *it;
        path.append(grpVp->getRoot());
        path.append(grpVp->getModeSwitch());
        path.append(grpVp->getChildRoot());
    }
    return true;
}

View3DInventorSelection::HighlightRoleNodes* View3DInventorSelection::highlightRole(HighlightRole role)
{
    const std::size_t index = highlightRoleIndex(role);
    return index < highlightRoles.size() ? &highlightRoles.at(index) : nullptr;
}

void View3DInventorSelection::setHighlightStyle(
    HighlightRole role,
    const HighlightRoleColors& colors,
    float lineWidth
)
{
    HighlightRoleNodes* nodes = highlightRole(role);
    if (!nodes) {
        return;
    }
    nodes->style->lineWidth = lineWidth;
    nodes->colors = colors;
}

SoGroup* View3DInventorSelection::highlightOwnerGroup(HighlightRoleNodes& nodes, const void* owner)
{
    auto found = nodes.owners.find(owner);
    if (found != nodes.owners.end()) {
        return found->second;
    }
    // A selection root rather than a plain separator: the secondary selection context
    // carrying the highlight colour is keyed by the whole chain of SoFCSelectionRoot
    // nodes on the path, so without one of its own two owners annotating the same
    // subelement would share a context and either clearing it would strip the other.
    auto selRoot = new SoFCSelectionRoot;
    selRoot->selectionStyle = SoFCSelectionRoot::PassThrough;
    selRoot->setName("HighlightOwner");
    nodes.group->addChild(selRoot);
    nodes.owners.emplace(owner, selRoot);
    return selRoot;
}

void View3DInventorSelection::addHighlight(
    HighlightRole role,
    const void* owner,
    App::DocumentObject* object,
    const std::vector<std::string>& subNames
)
{
    HighlightRoleNodes* nodes = highlightRole(role);
    if (!nodes) {
        return;
    }
    if (!object) {
        return;
    }
    if (!object->isAttachedToDocument()) {
        return;
    }
    if (!Application::Instance) {
        return;
    }

    auto vp = freecad_cast<ViewProviderDocumentObject*>(Application::Instance->getViewProvider(object));
    if (!vp) {
        return;
    }
    // A hidden object contributes nothing: the stored path runs through its mode
    // switch, and Coin's in-path traversal honours whichChild.
    if (!vp->isSelectable()) {
        return;
    }
    if (!vp->isShow()) {
        return;
    }
    // Public API, so the document is checked rather than assumed. An annotation holds
    // a path into its own document's scene graph; hung under another document's viewer
    // it would render that document's geometry here and expose its nodes to this
    // viewer's traversals.
    if (!guiDocument) {
        return;
    }
    if (vp->getDocument() != guiDocument) {
        return;
    }

    // Every subName's own elements are gathered into one list, rather than calling
    // addHighlightElements() once per subName: its dedup by scene-graph path only
    // spans the elements one call hands it, and a view provider that does not
    // distinguish elements in the path it returns — most of them, since only a
    // subname's SoDetail usually differs — would otherwise get one duplicate,
    // alpha-compounding annotation per subName instead of one shared annotation.
    std::vector<std::string> elements;
    elements.reserve(subNames.size());
    for (const std::string& subName : subNames) {
        // A face drawn alone leaves its boundary invisible: edges live on a node of
        // their own, separate from faces. Its boundary elements are drawn alongside
        // it, under the same owner and role, so a highlighted face still reads as a
        // face rather than a bare patch.
        auto boundaryElements = vp->getBoundaryElements(subName.c_str());
        elements.push_back(subName);
        for (std::string& boundaryElement : boundaryElements) {
            elements.push_back(std::move(boundaryElement));
        }
    }

    addHighlightElements(*nodes, owner, *vp, elements);
}

void View3DInventorSelection::addHighlightElements(
    HighlightRoleNodes& nodes,
    const void* owner,
    ViewProviderDocumentObject& vp,
    const std::vector<std::string>& elements
)
{
    // Every annotation made for this reference so far. An element whose path is
    // already covered joins that annotation rather than getting one of its own:
    // annotations that replay the same path share their nodes' secondary
    // selection contexts, and a second annotation applying a detail of a type a
    // node does not carry leaves that node's context empty, which the Part shape
    // nodes read as "draw nothing".
    std::vector<SoFCPathAnnotation*> annotations;
    // Left null until an element actually resolves, so an owner whose elements all
    // fail to resolve does not leave an empty subgroup behind.
    SoGroup* ownerGroup = nullptr;
    // Only the elements that resolved get a colour: an unresolved name would still map onto
    // an index and tint whatever geometry happens to sit there.
    std::vector<std::string> resolvedElements;

    for (const std::string& element : elements) {
        SoTempPath path(10);
        path.ref();
        if (!appendGroupPath(&vp, path)) {
            path.unrefNoDelete();
            continue;
        }

        SoDetail* det = nullptr;
        // The annotation holds a plain SoPath into the view provider's live nodes. A
        // recompute that rebuilds that scene graph makes Coin's path auditing truncate
        // the path rather than leave it dangling, so the highlight silently stops
        // rendering until the next refresh. checkGroupOnTop() has exactly the same
        // exposure.
        const bool resolved = vp.getDetailPath(element.c_str(), &path, true, det);

        if (!resolved || !path.getLength()) {
            delete det;
            path.unrefNoDelete();
            continue;
        }

        // Keyed in the shape nodes' own namespace: a name that only exists in the view
        // provider's element space (e.g. a sketch's internal geometry) would otherwise
        // fail every node's prefix filter below and be silently dropped from the colour
        // map, even though it resolved to a real detail right above.
        resolvedElements.push_back(vp.mapElementNameForColor(element));

        auto found = std::ranges::find_if(annotations, [&path](SoFCPathAnnotation* candidate) {
            return sameNodes(candidate->getPath(), path);
        });

        SoGroup* grp = highlightOwnerGroup(nodes, owner);
        ownerGroup = grp;
        if (found == annotations.end()) {
            auto node = new SoFCPathAnnotation;
            node->setPath(&path);
            grp->addChild(node);
            annotations.push_back(node);

            applyHighlightDetail(nodes.group, grp, node, path, det, nodes.colors.face.asValue<SbColor>());
            // The annotation frees exactly one detail, and it is this one: the first
            // element to reach this path is the one whose detail decides how the
            // annotation renders — with a detail it replays the geometry, without one
            // it falls back to whole-object rendering.
            node->setDetail(det);
            det = nullptr;
        }
        else if ((*found)->getDetail()) {
            applyHighlightDetail(
                nodes.group,
                grp,
                *found,
                path,
                det,
                nodes.colors.face.asValue<SbColor>()
            );
        }
        else {
            // The annotation on this path already draws the whole object; appending
            // an element would narrow it back down to that element alone.
        }

        delete det;
        path.unrefNoDelete();
    }

    // After the annotations exist, and again on every rebuild: the colour is written
    // into contexts the nodes below hold, and each teardown drops them. Applied per
    // annotation rather than once for the whole owner group: ownerGroup is shared by
    // every object this reference touches, and a broadcast action would let this
    // object's colours bleed onto — or be overwritten by — a sibling object's
    // annotations under the same owner.
    //
    // ownerGroup is non-null here only because some element resolved, and every
    // resolved element (including a whole-object "" expanding to its three bare
    // prefixes) pushes into resolvedElements first, so elementColors is never empty
    // at this point.
    const std::map<std::string, Base::Color> elementColors
        = highlightColorMap(resolvedElements, nodes.colors);
    for (SoFCPathAnnotation* annotation : annotations) {
        applyHighlightElementColors(nodes.group, ownerGroup, annotation, elementColors);
    }
}

void View3DInventorSelection::clearHighlight(HighlightRole role, const void* owner)
{
    HighlightRoleNodes* nodes = highlightRole(role);
    if (!nodes) {
        return;
    }
    auto found = nodes->owners.find(owner);
    if (found == nodes->owners.end()) {
        return;
    }
    SoGroup* grp = found->second;
    nodes->owners.erase(found);

    // Drop both contexts this owner's annotations created before the nodes go away.
    // Neither clear covers the other: they live in different maps, keyed differently.
    // A surviving primary context would keep colouring whatever the subgroup's address
    // is reused for, since the key holds the subgroup by pointer.
    SoSelectionElementAction clearSecondary(SoSelectionElementAction::None, true);
    applyToOwnerGroup(nodes->group, grp, clearSecondary);
    SoSelectionElementAction clearPrimary(SoSelectionElementAction::None);
    applyToOwnerGroup(nodes->group, grp, clearPrimary);

    nodes->group->removeChild(grp);
}

void View3DInventorSelection::clearGroupOnTop()
{
    if (!objectsOnTop.empty() || !objectsOnTopPreSel.empty()) {
        objectsOnTop.clear();
        objectsOnTopPreSel.clear();
        SoSelectionElementAction action(SoSelectionElementAction::None, true);
        action.apply(pcGroupOnTopPreSel);
        action.apply(pcGroupOnTopSel);
        coinRemoveAllChildren(pcGroupOnTopSel);
        coinRemoveAllChildren(pcGroupOnTopPreSel);
        FC_LOG("clear annotation");
    }
}
