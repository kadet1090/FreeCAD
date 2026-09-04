// SPDX-License-Identifier: LGPL-2.1-or-later

#include <string_view>

#include <QtCore/QtCore>

#include "GeometrySelectionGate.h"

using namespace Gui;

namespace
{

struct KindEntry
{
    GeometryKind kind;
    const char* prefix;
};

// clang-format off
static const KindEntry kindTable[] = {
    {.kind = GeometryKind::Vertex,      .prefix = "Vertex"},
    {.kind = GeometryKind::Edge,        .prefix = "Edge"  },
    {.kind = GeometryKind::Wire,        .prefix = "Wire"  },
    {.kind = GeometryKind::Face,        .prefix = "Face"  },
    {.kind = GeometryKind::Solid,       .prefix = "Solid" },
};
// clang-format on

}  // namespace

GeometryKindGate::GeometryKindGate(GeometryKinds kinds, App::DocumentObject* support)
    : _kinds(kinds)
    , _support(support)
{}

bool GeometryKindGate::allow(App::Document* /*doc*/, App::DocumentObject* obj, const char* subName)
{
    if (_support != nullptr && obj != _support) {
        notAllowedReason = QT_TR_NOOP("Selection must be on the support object.");
        return false;
    }

    const std::string_view sub = subName != nullptr ? std::string_view(subName) : std::string_view();

    if (sub.empty()) {
        if (_kinds.testFlag(GeometryKind::WholeObject)) {
            return true;
        }
        notAllowedReason = QT_TR_NOOP("Whole-object selection is not allowed here.");
        return false;
    }

    for (const KindEntry& entry : kindTable) {
        if (_kinds.testFlag(entry.kind) && sub.starts_with(entry.prefix)) {
            return true;
        }
    }

    notAllowedReason = QT_TR_NOOP("This type of geometry is not allowed here.");
    return false;
}
