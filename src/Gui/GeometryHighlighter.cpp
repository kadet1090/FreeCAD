// SPDX-License-Identifier: LGPL-2.1-or-later

#include <algorithm>
#include <array>
#include <cassert>
#include <map>
#include <set>
#include <utility>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Base/ServiceProvider.h>
#include <Gui/Application.h>
#include <Gui/Document.h>
#include <Gui/StyleParameters.h>
#include <Gui/StyleParameters/ParameterManager.h>
#include <Gui/View3DInventor.h>
#include <Gui/View3DInventorSelection.h>
#include <Gui/View3DInventorViewer.h>
#include <Gui/ViewProviderDocumentObject.h>

#include "GeometryHighlighter.h"

using namespace Gui;

std::vector<GeometryReference>& GeometryHighlightModel::slot(HighlightRole role)
{
    // COUNT sizes the storage, it does not name a slot, so reaching here with it is a
    // caller bug rather than a runtime condition: it trips the assertion in a debug
    // build and throws out of at() in a release one, never reading past the array.
    assert(role != HighlightRole::COUNT && "HighlightRole::COUNT is not a role");
    return _byRole.at(highlightRoleIndex(role));
}

const std::vector<GeometryReference>& GeometryHighlightModel::slot(HighlightRole role) const
{
    assert(role != HighlightRole::COUNT && "HighlightRole::COUNT is not a role");
    return _byRole.at(highlightRoleIndex(role));
}

void GeometryHighlightModel::setHighlighted(HighlightRole role, std::vector<GeometryReference> references)
{
    slot(role) = std::move(references);
}

void GeometryHighlightModel::clear(HighlightRole role)
{
    slot(role).clear();
}

void GeometryHighlightModel::clear()
{
    for (std::vector<GeometryReference>& references : _byRole) {
        references.clear();
    }
}

std::vector<GeometryReference> GeometryHighlightModel::effective(HighlightRole role) const
{
    // The hovered role is the most specific one, so it renders everything it holds.
    if (role == HighlightRole::Hovered) {
        return slot(role);
    }

    // A hovered reference is drawn once, in the hovered style. Nothing else is
    // subtracted: the application's own selection may well be painting a reference
    // green at the same time, but GroupHighlight is traversed after GroupOnTop, so
    // the reference highlight simply draws over it.
    const std::vector<GeometryReference>& hoveredReferences = slot(HighlightRole::Hovered);
    const std::vector<GeometryReference>& references = slot(role);

    std::vector<GeometryReference> result;
    result.reserve(references.size());
    for (const GeometryReference& reference : references) {
        if (std::ranges::find(hoveredReferences, reference) != hoveredReferences.end()) {
            continue;
        }
        result.push_back(reference);
    }
    return result;
}

void GeometryHighlightModel::dropObject(const App::DocumentObject* object)
{
    const auto matches = [object](const GeometryReference& reference) {
        return reference.object == object;
    };
    for (std::vector<GeometryReference>& references : _byRole) {
        std::erase_if(references, matches);
    }
}

void GeometryHighlightModel::dropDocument(const App::Document* document)
{
    const auto matches = [document](const GeometryReference& reference) {
        return reference.object && reference.object->getDocument() == document;
    };
    for (std::vector<GeometryReference>& references : _byRole) {
        std::erase_if(references, matches);
    }
}

void HighlightVisibility::setRevealed(const std::vector<App::DocumentObject*>& objects)
{
    // Settle the outgoing set before taking on the new one: erasing an entry destroys
    // its holder, and that is what restores the object.
    std::erase_if(_revealed, [&objects](const auto& entry) {
        return std::ranges::find(objects, entry.first) == objects.end();
    });

    // Every object is governed, whatever its visibility: the reveal writes nothing, so
    // one that was already visible is unaffected both now and when the reveal ends.
    for (App::DocumentObject* object : objects) {
        _revealed.try_emplace(object, object);
    }
}

namespace
{
/// Every 3D viewer showing @p document, resolved fresh: a viewer can be destroyed
/// at any time and takes its highlight group with it, so none are cached.
std::vector<View3DInventorViewer*> viewersOf(App::Document* document)
{
    std::vector<View3DInventorViewer*> viewers;
    if (!document || !Application::Instance) {
        return viewers;
    }
    Gui::Document* guiDocument = Application::Instance->getDocument(document);
    if (!guiDocument) {
        return viewers;
    }
    for (MDIView* view : guiDocument->getMDIViewsOfType(View3DInventor::getClassTypeId())) {
        if (auto* inventorView = freecad_cast<View3DInventor*>(view)) {
            viewers.push_back(inventorView->getViewer());
        }
    }
    return viewers;
}

/// One document's share of what each role has to render.
using ReferencesByRole = std::array<std::vector<GeometryReference>, highlightRoleCount>;

/// Splits everything @p model wants rendered by the document it lives in. An
/// annotation holds a path into its own document's scene graph, so it may only ever
/// be pushed into a viewer of that document. The Reference role contributes nothing
/// while @p selecting is false: a committed reference is only drawn for the
/// duration of a pick session, so outside one the hovered reference is the sole
/// highlight on screen rather than a shift between two similar blues.
std::map<App::Document*, ReferencesByRole> groupByDocument(
    const GeometryHighlightModel& model,
    bool selecting
)
{
    std::map<App::Document*, ReferencesByRole> byDocument;
    for (std::size_t index = 0; index < highlightRoleCount; ++index) {
        const auto role = static_cast<HighlightRole>(index);
        if (role == HighlightRole::Reference && !selecting) {
            continue;
        }
        for (const GeometryReference& reference : model.effective(role)) {
            App::Document* document = reference.object ? reference.object->getDocument() : nullptr;
            if (!document) {
                continue;
            }
            byDocument[document].at(index).push_back(reference);
        }
    }
    return byDocument;
}

/// One role's references, gathered by the object each names, in the order each
/// object was first seen. addHighlight() is then called once per object with every
/// one of its subNames together, rather than once per reference: its own dedup only
/// spans what a single call hands it, and most view providers resolve every element
/// of one object onto the same scene-graph path, so calling it per reference would
/// hand it one duplicate annotation per reference instead of the intended one shared
/// annotation per path.
std::vector<std::pair<App::DocumentObject*, std::vector<std::string>>> groupReferencesByObject(
    const std::vector<GeometryReference>& references
)
{
    std::vector<std::pair<App::DocumentObject*, std::vector<std::string>>> byObject;
    for (const GeometryReference& reference : references) {
        auto found = std::ranges::find_if(byObject, [&reference](const auto& entry) {
            return entry.first == reference.object;
        });
        if (found == byObject.end()) {
            byObject.emplace_back(reference.object, std::vector<std::string> {reference.subName});
        }
        else {
            found->second.push_back(reference.subName);
        }
    }
    return byObject;
}

/// Every object a highlight is about to be drawn on, once each — visibility is a
/// per-document view-provider concern, so it is decided per object and not per
/// viewer. Everything reaching here is inherently transient: Hovered lasts only as
/// long as the cursor stays put, and groupByDocument() already withholds Reference
/// outside a pick session, so whatever it does contribute is transient too.
std::vector<App::DocumentObject*> objectsToReveal(
    const std::map<App::Document*, ReferencesByRole>& byDocument
)
{
    std::vector<App::DocumentObject*> objects;
    for (const auto& entry : byDocument) {
        for (const std::vector<GeometryReference>& references : entry.second) {
            for (const GeometryReference& reference : references) {
                if (std::ranges::find(objects, reference.object) == objects.end()) {
                    objects.push_back(reference.object);
                }
            }
        }
    }
    return objects;
}
}  // namespace

GeometryHighlighter::GeometryHighlighter(QObject* parent)
    : QObject(parent)
{
    if (!Application::Instance) {
        return;
    }
    _objectDeletedConnection = Application::Instance->signalDeletedObject.connect(
        [this](const ViewProvider& viewProvider) {
            const auto* documentObject = freecad_cast<const ViewProviderDocumentObject*>(&viewProvider);
            if (!documentObject) {
                return;
            }
            _model.dropObject(documentObject->getObject());
            refresh();
        }
    );
    _documentDeletedConnection = Application::Instance->signalDeleteDocument.connect(
        [this](const Gui::Document& document) {
            _model.dropDocument(document.getDocument());
            refresh();
        }
    );
}

GeometryHighlighter::~GeometryHighlighter()
{
    clear();
}

void GeometryHighlighter::setHighlighted(HighlightRole role, std::vector<GeometryReference> references)
{
    _model.setHighlighted(role, std::move(references));
    refresh();
}

void GeometryHighlighter::clear(HighlightRole role)
{
    _model.clear(role);
    refresh();
}

void GeometryHighlighter::clear()
{
    _model.clear();
    refresh();
}

void GeometryHighlighter::setSelecting(bool selecting)
{
    if (_selecting == selecting) {
        return;
    }
    _selecting = selecting;
    // The Reference role's contribution to what is drawn changes with this flag, and
    // so does what is revealed for it — both are decided in refresh() alongside
    // everything else, so it is the one entry point.
    refresh();
}

void GeometryHighlighter::withdrawAndAdopt(std::set<App::Document*> documents)
{
    // The documents drawn in last time as well: a role emptied since then must stop
    // rendering in the views that used to show it. Those are compared by pointer
    // against the still-open documents rather than dereferenced, so one closed
    // meanwhile is simply dropped.
    const std::vector<App::Document*> open = App::GetApplication().getDocuments();
    std::set<App::Document*> stale;
    for (App::Document* document : _touchedDocuments) {
        if (std::ranges::find(open, document) != open.end()) {
            stale.insert(document);
        }
    }
    stale.insert(documents.begin(), documents.end());
    _touchedDocuments = std::move(documents);

    for (App::Document* document : stale) {
        for (View3DInventorViewer* viewer : viewersOf(document)) {
            View3DInventorSelection* selection = viewer->getInventorSelection();
            if (!selection) {
                continue;
            }
            for (std::size_t index = 0; index < highlightRoleCount; ++index) {
                selection->clearHighlight(static_cast<HighlightRole>(index), this);
            }
        }
    }
}

void GeometryHighlighter::refresh()
{
    const std::map<App::Document*, ReferencesByRole> byDocument = groupByDocument(_model, _selecting);

    std::set<App::Document*> documents;
    for (const auto& entry : byDocument) {
        documents.insert(entry.first);
    }
    // Withdraw before anything below can bail out, so a clear() never strands an
    // annotation in a view.
    withdrawAndAdopt(std::move(documents));
    // After the withdrawal, so an object that stopped being drawn goes back to hidden
    // in the same breath, and before any addHighlight() below, which ignores a hidden
    // object. Ahead of every early return, so clear() always restores.
    _visibility.setRevealed(objectsToReveal(byDocument));

    if (byDocument.empty()) {
        return;
    }

    auto* parameters = Base::provideService<Gui::StyleParameters::ParameterManager>();
    if (!parameters) {
        return;
    }

    // Resolved per rebuild rather than cached: highlights are transient, so a theme
    // change is picked up by the next one.
    struct RoleStyle
    {
        HighlightRole role;
        HighlightRoleColors colors;
        float lineWidth;
    };
    // resolve(ParameterDefinition<T>) returns T, so each colour arrives as a
    // Base::Color — alpha included — and the width as a Numeric.
    const std::array<RoleStyle, highlightRoleCount> styles {
        RoleStyle {
            .role = HighlightRole::Reference,
            .colors = {
                .face = parameters->resolve(StyleParameters::GeometryHighlightReferenceFaceColor),
                .edge = parameters->resolve(StyleParameters::GeometryHighlightReferenceEdgeColor),
                .point = parameters->resolve(StyleParameters::GeometryHighlightReferencePointColor),
            },
            .lineWidth = static_cast<float>(
                parameters->resolve(StyleParameters::GeometryHighlightReferenceLineWidth).value
            )
        },
        RoleStyle {
            .role = HighlightRole::Hovered,
            .colors = {
                .face = parameters->resolve(StyleParameters::GeometryHighlightHoveredFaceColor),
                .edge = parameters->resolve(StyleParameters::GeometryHighlightHoveredEdgeColor),
                .point = parameters->resolve(StyleParameters::GeometryHighlightHoveredPointColor),
            },
            .lineWidth = static_cast<float>(
                parameters->resolve(StyleParameters::GeometryHighlightHoveredLineWidth).value
            )
        },
    };

    for (const auto& [document, references] : byDocument) {
        for (View3DInventorViewer* viewer : viewersOf(document)) {
            View3DInventorSelection* selection = viewer->getInventorSelection();
            if (!selection) {
                continue;
            }
            for (const RoleStyle& style : styles) {
                selection->setHighlightStyle(style.role, style.colors, style.lineWidth);
                for (const auto& [object, subNames] :
                     groupReferencesByObject(references.at(highlightRoleIndex(style.role)))) {
                    selection->addHighlight(style.role, this, object, subNames);
                }
            }
        }
    }
}
