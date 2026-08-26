// SPDX-License-Identifier: LGPL-2.1-or-later

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

#include <QApplication>
#include <QString>
#include <QTimer>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/GeoFeatureGroupExtension.h>
#include <App/Property.h>
#include <App/PropertyLinks.h>
#include <Gui/Application.h>
#include <Gui/GeometryHighlighter.h>
#include <Gui/Selection/Selection.h>
#include <Gui/Selection/SelectionFilter.h>

#include "GeometrySelection.h"
#include "GeometrySelectionGate.h"

using namespace Gui;

GeometrySelection::GeometrySelection(GeometryQuantity mode, QObject* parent)
    : QObject(parent)
    , Gui::SelectionObserver(false)
    , _quantity(mode)
    , _highlighter(std::make_unique<GeometryHighlighter>())
{}

GeometrySelection::~GeometrySelection()
{
    // Guaranteed-pairing contract: never leave a session dangling. Block our Qt
    // signals before unwinding: they are wired to slots on the owning widget, which
    // may already be partway through its own destruction (Qt clears a QObject's
    // connections only in ~QObject, which runs after ~QWidget has deleted its
    // children). Emitting selectionModeExited here would otherwise invoke a slot on
    // that half-destroyed object and abort.
    if (_selecting) {
        blockSignals(true);
        stopSelecting();
    }
}

void GeometrySelection::setQuantity(GeometryQuantity mode)
{
    _quantity = mode;
}

void GeometrySelection::setReferences(std::vector<GeometryReference> references)
{
    updateReferences(std::move(references));
}

void GeometrySelection::removeReference(std::size_t index)
{
    if (index >= _references.size()) {
        return;
    }
    std::vector<GeometryReference> next = _references;
    next.erase(next.begin() + static_cast<std::ptrdiff_t>(index));
    updateReferences(std::move(next));
}

void GeometrySelection::clear()
{
    if (_references.empty()) {
        return;
    }
    updateReferences({});
}

void GeometrySelection::setHoveredReference(int index)
{
    const bool valid = index >= 0 && index < static_cast<int>(_references.size());
    _hoveredIndex = valid ? index : -1;
    if (_hoveredIndex < 0) {
        publishHover({});
        return;
    }
    publishHover({_references[static_cast<std::size_t>(_hoveredIndex)]});
}

void GeometrySelection::setHoveredReferences(std::vector<GeometryReference> references)
{
    // Not index-bound: a later reference change must republish this untouched rather than
    // resolve _hoveredIndex against the new model.
    _hoveredIndex = -1;
    publishHover(std::move(references));
}

void GeometrySelection::publishHover(std::vector<GeometryReference> references)
{
    _hoveredReferences = std::move(references);
    if (_hoveredReferences.empty()) {
        _highlighter->clear(HighlightRole::Hovered);
        return;
    }
    _highlighter->setHighlighted(HighlightRole::Hovered, _hoveredReferences);
}

void GeometrySelection::refreshHighlight()
{
    _highlighter->setHighlighted(HighlightRole::Reference, _references);
    if (_hoveredIndex >= 0) {
        // Re-resolve the hover against the new model; a removal can invalidate it.
        setHoveredReference(_hoveredIndex);
        return;
    }
    // A free-standing hover names geometry the model never held, so nothing about a
    // model change can invalidate it — republish it as it stands.
    publishHover(std::move(_hoveredReferences));
}

void GeometrySelection::updateReferences(std::vector<GeometryReference> references)
{
    _references = std::move(references);
    if (_autoApply && isBound()) {
        writeToProperty();
    }
    refreshHighlight();
    Q_EMIT referencesChanged();
}

void GeometrySelection::setSelectionGate(GateFactory factory)
{
    _gateFactory = std::move(factory);
}

void GeometrySelection::setSelectionFilter(const QString& filter)
{
    const std::string filterString = filter.toStdString();
    _gateFactory = [filterString] {
        return std::make_unique<SelectionFilterGate>(filterString.c_str());
    };
}

void GeometrySelection::setAllowedKinds(GeometryKinds kinds, App::DocumentObject* support)
{
    _gateFactory = [kinds, support] {
        return std::make_unique<GeometryKindGate>(kinds, support);
    };
}

void GeometrySelection::startSelecting()
{
    if (_selecting) {
        return;
    }
    _referencesBeforeSelecting = _references;
    // Captured before seedViewportSelection() below discards it, so stopSelecting() can
    // put it back once the session ends.
    _priorSelection = capturePriorSelection();
    if (_gateFactory) {
        // Selection takes ownership and deletes on rmvSelectionGate.
        Gui::Selection().addSelectionGate(_gateFactory().release());
    }
    _selecting = true;
    // Let observers react first (e.g. hide this feature's preview and show the
    // previous feature). Only then seed the selection, so their scene rebuild
    // cannot clear it again.
    Q_EMIT selectionModeEntered();
    // Only now are the reference highlights transient, so only now may they reveal a
    // hidden object. Before the seeding, matching where the reveal used to sit, so the
    // visibility writes cannot be fed back into our own onSelectionChanged().
    _highlighter->setSelecting(true);
    seedViewportSelection();
    // Attach last, so the seeding picks above are not fed back into our own
    // onSelectionChanged().
    attachSelection();
    // Observers just rebuilt the scene graph; invalidate highlight paths and rebuild annotations.
    refreshHighlight();
}

void GeometrySelection::stopSelecting()
{
    if (!_selecting) {
        return;
    }
    _selecting = false;
    detachSelection();
    // Paired with startSelecting(): the session is over, so whatever it revealed goes
    // back to the visibility it had. Detached first, so those writes reach nobody.
    _highlighter->setSelecting(false);
    if (_gateFactory) {
        Gui::Selection().rmvSelectionGate();
    }
    // Paired with startSelecting(): put back the 3D selection the session displaced.
    // Restored only after detachSelection() (so these writes are not fed back into our
    // own onSelectionChanged()) and after the gate above is removed (so an item that
    // predates this session's gate is not rejected by it on the way back in). Restored
    // before the emit below, but not because the emit would otherwise undo it: every
    // selectionModeExited observer in the tree (TaskExtrudeParameters/TaskHelixParameters's
    // finishReferenceSelection, TaskPadParameters's showPreview/showPreviousFeature,
    // TaskTransform's dragger pick-style toggles, GeometrySelectorWidget's history capture
    // and row rebuild) only flips a ViewProvider's mode-switch index or touches
    // widget-local state; none of them call into Gui::Selection(), so there is nothing here
    // for the emit to undo either way.
    restorePriorSelection();
    Q_EMIT selectionModeExited();
    // Observers just rebuilt the scene graph in reverse; invalidate highlight paths and rebuild
    // annotations.
    refreshHighlight();
}

std::vector<GeometryReference> GeometrySelection::capturePriorSelection() const
{
    std::vector<GeometryReference> prior;
    // NoResolve: a nested object's selection is stored as (top-level anchor, full
    // subelement path) rather than the resolved leaf, and that anchor+path pair is
    // exactly what addSelection() below needs to reproduce it — resolving here would
    // hand back the leaf object alone, which the 3D view cannot match back to a path.
    for (const auto& selected : Gui::Selection().getCompleteSelection(Gui::ResolveMode::NoResolve)) {
        if (!selected.pObject) {
            continue;
        }
        prior.push_back(
            {.object = selected.pObject,
             .subName = selected.SubName ? selected.SubName : std::string()}
        );
    }
    return prior;
}

void GeometrySelection::restorePriorSelection() const
{
    // Cleared unconditionally: an empty prior selection restoring as empty is the
    // common case, not one to special-case away.
    Gui::Selection().clearSelection();
    for (const GeometryReference& reference : _priorSelection) {
        App::Document* document = reference.object ? reference.object->getDocument() : nullptr;
        if (!document) {
            continue;
        }
        Gui::Selection().addSelection(
            document->getName(),
            reference.object->getNameInDocument(),
            reference.subName.empty() ? nullptr : reference.subName.c_str()
        );
    }
}

void GeometrySelection::seedViewportSelection()
{
    // Cleared unconditionally, even with nothing to seed: an empty reference set must
    // still bring the viewport into line with the (now empty) control rather than leave
    // it holding whatever was selected before the session began.
    Gui::Selection().clearSelection();
    if (!Gui::Application::Instance || _references.empty()) {
        return;
    }
    // Mark the current references as selected so they can be modified in place (picked
    // to add, Ctrl-picked to drop).
    for (const GeometryReference& ref : _references) {
        App::Document* document = ref.object ? ref.object->getDocument() : nullptr;
        if (!document) {
            continue;
        }
        // An object inside a GeoFeatureGroup (a PartDesign Body, a Part…) is rendered
        // under that group's coordinate node, so the selection must be anchored at the
        // top-level container with a path down to the object. Anchoring at the bare
        // object stores a valid entry that the 3D view cannot match, so nothing
        // highlights. Walk up every enclosing group to build that path.
        App::DocumentObject* anchor = ref.object;
        std::string subName = ref.subName;
        while (App::DocumentObject* group = App::GeoFeatureGroupExtension::getGroupOfObject(anchor)) {
            subName = std::string(anchor->getNameInDocument()) + "." + subName;
            anchor = group;
        }
        Gui::Selection().addSelection(
            document->getName(),
            anchor->getNameInDocument(),
            subName.empty() ? nullptr : subName.c_str()
        );
    }
}

void GeometrySelection::cancelSelecting()
{
    if (!_selecting) {
        return;
    }
    // Set for the duration of stopSelecting() below, so its selectionModeExited emission lets a
    // listener ask wasCancelled() directly. The references are restored only after: stopSelecting()
    // is what detaches the selection observer and removes the gate, and doing that unwind before
    // mutating _references keeps a recompute triggered by the restore from re-entering
    // onSelectionChanged() while the session still looks armed.
    _cancelling = true;
    stopSelecting();
    _cancelling = false;
    if (_references != _referencesBeforeSelecting) {
        updateReferences(_referencesBeforeSelecting);
    }
}

bool GeometrySelection::appendRequested() const
{
    return _quantity == GeometryQuantity::AllowMultiple
        && (QApplication::keyboardModifiers() & Qt::ControlModifier) != 0;
}

std::optional<GeometryReference> GeometrySelection::referenceFromChange(
    const Gui::SelectionChanges& msg
) const
{
    App::Document* document = App::GetApplication().getDocument(msg.pDocName);
    App::DocumentObject* object = document ? document->getObject(msg.pObjectName) : nullptr;
    if (!object) {
        return std::nullopt;
    }
    return GeometryReference {
        .object = object,
        .subName = msg.pSubName ? std::string(msg.pSubName) : std::string()
    };
}

void GeometrySelection::handleDeselect(const Gui::SelectionChanges& msg)
{
    // Only a multi-value selection lets the user toggle individual references off.
    if (_quantity != GeometryQuantity::AllowMultiple) {
        return;
    }
    std::optional<GeometryReference> removed = referenceFromChange(msg);
    if (!removed) {
        return;
    }
    std::vector<GeometryReference> next = _references;
    auto found = std::ranges::find(next, *removed);
    if (found == next.end()) {
        return;
    }
    next.erase(found);
    updateReferences(std::move(next));
}

void GeometrySelection::scheduleConfirmOnClear()
{
    _confirmOnClearPending = true;
    // Fire after the current synchronous selection burst: a replacing pick emits the
    // clear and its AddSelection back-to-back, and that AddSelection clears the flag
    // below before this runs. A standalone clear (empty-space click) survives to here.
    QTimer::singleShot(0, this, [this] {
        if (_confirmOnClearPending && _selecting) {
            _confirmOnClearPending = false;
            stopSelecting();
        }
    });
}

void GeometrySelection::onSelectionChanged(const Gui::SelectionChanges& msg)
{
    if (!_selecting) {
        return;
    }
    Q_EMIT pickSelectionChanged(msg);
    if (msg.Type == Gui::SelectionChanges::ClrSelection) {
        scheduleConfirmOnClear();
        return;
    }
    // Any other change belongs to an active pick, so cancel a pending empty-space confirm.
    _confirmOnClearPending = false;
    if (msg.Type == Gui::SelectionChanges::RmvSelection) {
        handleDeselect(msg);
        return;
    }
    if (msg.Type != Gui::SelectionChanges::AddSelection) {
        return;
    }

    std::optional<GeometryReference> picked = referenceFromChange(msg);
    if (!picked) {
        return;
    }

    const bool append = appendRequested();
    std::vector<GeometryReference> next;
    if (append) {
        next = _references;
        if (std::ranges::find(next, *picked) == next.end()) {
            next.push_back(std::move(*picked));
        }
    }
    else {
        next.push_back(std::move(*picked));
    }
    updateReferences(std::move(next));

    // A replacing pick completes the intent of a single selection, so the
    // session ends; an appending pick (AllowMultiple + modifier) keeps it open.
    if (!append) {
        stopSelecting();
    }
}

void GeometrySelection::bind(App::Property& prop)
{
    unbind();
    _boundProperty = &prop;

    if (auto* container = dynamic_cast<App::DocumentObject*>(prop.getContainer())) {
        _boundContainer = container;
        App::Document* document = container->getDocument();
        _propertyChangedConnection = document->signalChangedObject.connect(
            [this](const App::DocumentObject&, const App::Property& changed) {
                if (&changed == _boundProperty && !_writingBack) {
                    reloadFromProperty();
                }
            }
        );
        // If the owning object is deleted (e.g. an aborted create command), drop the
        // binding at once so the changed-signal handler can never touch a dead property.
        _objectDeletedConnection = document->signalDeletedObject.connect(
            [this](const App::DocumentObject& deleted) {
                if (&deleted == _boundContainer) {
                    _boundProperty = nullptr;
                    _boundContainer = nullptr;
                }
            }
        );
    }
    reloadFromProperty();
}

void GeometrySelection::unbind()
{
    _propertyChangedConnection.disconnect();
    _objectDeletedConnection.disconnect();
    _boundProperty = nullptr;
    _boundContainer = nullptr;
}

bool GeometrySelection::apply()
{
    return writeToProperty();
}

void GeometrySelection::reloadFromProperty()
{
    std::vector<GeometryReference> loaded;
    if (auto* link = dynamic_cast<App::PropertyLinkSub*>(_boundProperty)) {
        App::DocumentObject* object = link->getValue();
        if (object) {
            const std::vector<std::string>& subs = link->getSubValues();
            if (subs.empty()) {
                loaded.push_back({.object = object, .subName = ""});
            }
            else {
                for (const std::string& sub : subs) {
                    loaded.push_back({.object = object, .subName = sub});
                }
            }
        }
    }
    else if (auto* linkList = dynamic_cast<App::PropertyLinkSubList*>(_boundProperty)) {
        const std::vector<App::DocumentObject*>& objects = linkList->getValues();
        const std::vector<std::string>& subs = linkList->getSubValues();
        for (std::size_t index = 0; index < objects.size(); ++index) {
            loaded.push_back(
                {.object = objects[index],
                 .subName = index < subs.size() ? subs[index] : std::string()}
            );
        }
    }
    // Assign directly — not via updateReferences — so reloads never trigger a write-back.
    _references = std::move(loaded);
    Q_EMIT referencesChanged();
}

bool GeometrySelection::writeToProperty()
{
    if (!isBound()) {
        return false;
    }

    _writingBack = true;
    if (auto* link = dynamic_cast<App::PropertyLinkSub*>(_boundProperty)) {
        if (_references.empty()) {
            link->setValue(nullptr, std::vector<std::string> {});
        }
        else {
            // PropertyLinkSub: single object; collect its sub names. A whole-object
            // reference (empty subName) contributes no sub, so a whole sketch links
            // as (object, {}) — not (object, {""}), which downstream code would try
            // to resolve as a real subelement.
            App::DocumentObject* object = _references.front().object;
            std::vector<std::string> subNames;
            subNames.reserve(_references.size());
            for (const GeometryReference& ref : _references) {
                if (!ref.subName.empty()) {
                    subNames.push_back(ref.subName);
                }
            }
            link->setValue(object, subNames);
        }
    }
    else if (auto* linkList = dynamic_cast<App::PropertyLinkSubList*>(_boundProperty)) {
        std::vector<App::DocumentObject*> objects;
        std::vector<std::string> subNames;
        objects.reserve(_references.size());
        subNames.reserve(_references.size());
        for (const GeometryReference& ref : _references) {
            objects.push_back(ref.object);
            subNames.push_back(ref.subName);
        }
        linkList->setValues(objects, subNames);
    }
    _writingBack = false;
    return true;
}
