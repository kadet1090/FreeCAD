// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QFlags>

#include <FCGlobal.h>
#include <Gui/Selection/Selection.h>

namespace App
{
class Document;
class DocumentObject;
}  // namespace App

namespace Gui
{

/// Geometry element kinds for kind-flags gate filtering.
enum class GeometryKind : unsigned
{
    Vertex = 1 << 0,
    Edge = 1 << 1,
    Wire = 1 << 2,
    Face = 1 << 3,
    Solid = 1 << 4,
    WholeObject = 1 << 5,
};

Q_DECLARE_FLAGS(GeometryKinds, GeometryKind)

/**
 * A selection gate that admits picks whose subelement type-name prefix matches
 * one of the enabled GeometryKind flags.  When WholeObject is set, empty
 * subName (whole-object picks) are also admitted.  An optional support object
 * restricts acceptance to picks on that object only.
 */
class GuiExport GeometryKindGate: public Gui::SelectionGate
{
public:
    explicit GeometryKindGate(GeometryKinds kinds, App::DocumentObject* support = nullptr);

    bool allow(App::Document* doc, App::DocumentObject* obj, const char* subName) override;

private:
    GeometryKinds _kinds;
    App::DocumentObject* _support;
};

}  // namespace Gui

Q_DECLARE_OPERATORS_FOR_FLAGS(Gui::GeometryKinds)
