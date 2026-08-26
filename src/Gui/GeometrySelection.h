// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <QObject>

#include <fastsignals/signal.h>

#include <FCGlobal.h>
#include <Gui/GeometryHighlighter.h>
#include <Gui/GeometryReference.h>
#include <Gui/GeometrySelectionGate.h>
#include <Gui/Selection/Selection.h>

class QString;

namespace App
{
class Document;
class DocumentObject;
class Property;
}  // namespace App

namespace Gui
{
class SelectionGate;
}

namespace Gui
{

enum class GeometryQuantity
{
    Single,         // exactly one reference
    AllowMultiple,  // intent is one; Ctrl-pick forces more
    // Multiple is a separate future widget that reuses this core.
};

/**
 * Widget-agnostic core of the geometry selector. Owns the selected-reference
 * model and quantity mode; later tasks add the selection session, gate, and
 * property binding. Emits referencesChanged() whenever the model changes.
 */
class GuiExport GeometrySelection: public QObject, public Gui::SelectionObserver
{
    Q_OBJECT

public:
    explicit GeometrySelection(
        GeometryQuantity mode = GeometryQuantity::Single,
        QObject* parent = nullptr
    );
    ~GeometrySelection() override;

    GeometryQuantity quantity() const
    {
        return _quantity;
    }
    void setQuantity(GeometryQuantity mode);

    const std::vector<GeometryReference>& references() const
    {
        return _references;
    }
    void setReferences(std::vector<GeometryReference> references);
    void removeReference(std::size_t index);
    void clear();

    /// Marks the reference at @p index as hovered, or nothing when @p index is
    /// -1 or out of range.
    void setHoveredReference(int index);

    /// Highlights @p references as hovered, whether or not the model holds them, so a
    /// caller offering geometry the selection has not committed to — a dropdown row — can
    /// preview it. An empty list clears the hover.
    void setHoveredReferences(std::vector<GeometryReference> references);

    /// The 3D-view highlight this selection drives. Never null.
    GeometryHighlighter* highlighter() const
    {
        return _highlighter.get();
    }

    using GateFactory = std::function<std::unique_ptr<Gui::SelectionGate>()>;

    void setSelectionGate(GateFactory factory);
    void setSelectionFilter(const QString& filter);
    void setAllowedKinds(GeometryKinds kinds, App::DocumentObject* support = nullptr);

    void startSelecting();
    void stopSelecting();
    /// Ends the session and restores the references held when it began, so a
    /// cancelled pick keeps the previous selection (or stays empty).
    void cancelSelecting();
    bool isSelecting() const
    {
        return _selecting;
    }
    /// True for the duration of a cancelSelecting() call, so a selectionModeExited listener can
    /// tell a cancelled session from a finished one directly, without inferring it from whether
    /// the references changed.
    bool wasCancelled() const
    {
        return _cancelling;
    }

    void bind(App::Property& prop);
    void unbind();
    bool isBound() const
    {
        return _boundProperty != nullptr;
    }
    void setAutoApply(bool on)
    {
        _autoApply = on;
    }
    bool autoApply() const
    {
        return _autoApply;
    }
    bool apply();

Q_SIGNALS:
    void referencesChanged();
    void selectionModeEntered();
    void selectionModeExited();
    /// Forwards each raw selection change observed during an active picking
    /// session, before the reference model reduces it. A consumer that needs
    /// more than the {object, subName} reference — the picked point for
    /// snapping, the link path for instance placement — connects here.
    void pickSelectionChanged(const Gui::SelectionChanges& msg);

protected:
    // Single place every model mutation routes through, so later tasks (binding)
    // can react in one spot.
    void updateReferences(std::vector<GeometryReference> references);

    void onSelectionChanged(const Gui::SelectionChanges& msg) override;
    // Whether the current pick should append (AllowMultiple + modifier) vs replace.
    virtual bool appendRequested() const;
    // Resolves the picked object/subelement of a change into a reference, or
    // nullopt if the object no longer exists.
    std::optional<GeometryReference> referenceFromChange(const Gui::SelectionChanges& msg) const;

    std::vector<GeometryReference> _references;
    GeometryQuantity _quantity;

private:
    GateFactory _gateFactory;
    bool _selecting = false;
    /// References captured at startSelecting(), restored by cancelSelecting().
    std::vector<GeometryReference> _referencesBeforeSelecting;
    /// True only while cancelSelecting() is unwinding the session; backs wasCancelled().
    bool _cancelling = false;

    /// The 3D selection captured at startSelecting(), before seedViewportSelection()
    /// discards it; put back by stopSelecting() once the session has fully unwound.
    std::vector<GeometryReference> _priorSelection;
    /// Snapshots the current 3D selection, unresolved (so a nested object's anchor and
    /// full subelement path come back exactly as they were added), for later replay by
    /// restorePriorSelection().
    std::vector<GeometryReference> capturePriorSelection() const;
    /// Replaces the 3D selection with what capturePriorSelection() captured — including
    /// replacing it with nothing, when that is what was captured.
    void restorePriorSelection() const;

    /// Mirrors the current references into the 3D selection so they appear picked
    /// and can be toggled off with Ctrl (AllowMultiple only).
    void seedViewportSelection();
    /// Drops the reference matching a Ctrl-deselect in the 3D view (AllowMultiple only).
    void handleDeselect(const Gui::SelectionChanges& msg);
    /// Confirms (commits + ends) the session on the next event-loop turn unless a pick
    /// arrives first. A replacing pick clears the selection just before adding to it, so
    /// a lone clear — a click on empty space — is the only one that reaches the deferred
    /// slot and confirms.
    void scheduleConfirmOnClear();
    bool _confirmOnClearPending = false;

    App::Property* _boundProperty = nullptr;
    /// The bound property's owning object; watched so a deletion drops the binding
    /// before any stale property access can happen.
    App::DocumentObject* _boundContainer = nullptr;
    bool _autoApply = true;
    bool _writingBack = false;
    fastsignals::scoped_connection _propertyChangedConnection;
    fastsignals::scoped_connection _objectDeletedConnection;

    void reloadFromProperty();
    bool writeToProperty();

    std::unique_ptr<GeometryHighlighter> _highlighter;

    /// Republishes the current references to the highlighter, re-resolving the
    /// hover against them. Called on every change to the reference model.
    void refreshHighlight();
    /// Sends @p references to the Hovered role and records them as what is published.
    void publishHover(std::vector<GeometryReference> references);
    /// The hovered position, or -1 when the hover is free-standing or absent. Held so a
    /// reference change can re-resolve an index-bound hover.
    int _hoveredIndex = -1;
    /// What the Hovered role currently publishes, whichever entry point set it.
    std::vector<GeometryReference> _hoveredReferences;
};

}  // namespace Gui

// Enables use as a Q_PROPERTY type (QVariant / setProperty on the selector widget).
Q_DECLARE_METATYPE(Gui::GeometryQuantity)
