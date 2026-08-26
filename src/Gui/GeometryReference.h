// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <cstddef>
#include <string>

#include <FCGlobal.h>

namespace App
{
class DocumentObject;
}

namespace Gui
{

/// One picked reference: a whole object (empty subName) or one of its subelements.
struct GuiExport GeometryReference
{
    App::DocumentObject* object = nullptr;
    std::string subName;

    bool operator==(const GeometryReference& other) const
    {
        return object == other.object && subName == other.subName;
    }
};

/// Which visual treatment a highlighted reference is rendered with.
enum class HighlightRole
{
    /// Every committed reference of a selector.
    Reference,
    /// The single reference whose selector row is under the cursor.
    Hovered,
    /// Not a role: how many there are, so per-role storage can be a fixed array
    /// and a new role cannot be forgotten in one.
    COUNT,
};

/// How many entries per-role storage needs.
inline constexpr std::size_t highlightRoleCount = static_cast<std::size_t>(HighlightRole::COUNT);

/// Where @p role lives in per-role storage.
inline constexpr std::size_t highlightRoleIndex(HighlightRole role)
{
    return static_cast<std::size_t>(role);
}

}  // namespace Gui
