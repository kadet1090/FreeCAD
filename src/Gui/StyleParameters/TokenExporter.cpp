// SPDX-License-Identifier: LGPL-2.1-or-later
/****************************************************************************
 *                                                                          *
 *   Copyright (c) 2025 Kacper Donat <kacper@kadet.net>                     *
 *                                                                          *
 *   This file is part of FreeCAD.                                          *
 *                                                                          *
 *   FreeCAD is free software: you can redistribute it and/or modify it     *
 *   under the terms of the GNU Lesser General Public License as            *
 *   published by the Free Software Foundation, either version 2.1 of the   *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   FreeCAD is distributed in the hope that it will be useful, but         *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with FreeCAD. If not, see                                *
 *   <https://www.gnu.org/licenses/>.                                       *
 *                                                                          *
 ***************************************************************************/

#include "TokenExporter.h"
#include "ColorEffect.h"
#include "ParameterManager.h"
#include "ParameterDescriptorRegistry.h"
#include "Value.h"
#include "Corners.h"
#include "Edges.h"

#include <Base/Color.h>

#include <QColor>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

// QJsonObject::operator[] is safe — it only inserts/overwrites named keys
// in an owned heap object, with no pointer arithmetic involved.
// NOLINTBEGIN(*-pro-bounds-avoid-unchecked-container-access)
namespace Gui::StyleParameters
{

namespace
{

QJsonObject valueToJson(const Value& value);

QJsonObject colorToLeaf(const Base::Color& color)
{
    const QColor qcolor = color.asValue<QColor>();

    // Opaque colors: compact "#rrggbb" hex. Transparent: "rgba(r, g, b, a)" CSS notation.
    const QString value = (qcolor.alpha() == 255)
        ? qcolor.name()
        : QString("rgba(%1, %2, %3, %4)")
              .arg(qcolor.red())
              .arg(qcolor.green())
              .arg(qcolor.blue())
              .arg(static_cast<double>(color.a), 0, 'g', 3);

    return {{"$type", "color"}, {"$value", value}};
}

QJsonObject numericToLeaf(const Numeric& numeric)
{
    // Use 'g' format: drops trailing zeros (28.0 → "28", 1.5 → "1.5").
    const QString numStr = QString::number(numeric.value, 'g', 10);

    if (numeric.unit == "px" || numeric.unit == "%") {
        return {{"$type", "dimension"}, {"$value", numStr + QString::fromStdString(numeric.unit)}};
    }

    if (numeric.unit.empty()) {
        return {{"$type", "number"}, {"$value", numeric.value}};
    }

    // Unknown unit — export as string so no information is lost.
    return {{"$type", "string"}, {"$value", numStr + QString::fromStdString(numeric.unit)}};
}

void addEdgeConvenience(QJsonObject& group, const Tuple& tuple)
{
    const Edges<Numeric> edges {Value {tuple}};
    const Numeric& top = edges.top();
    const Numeric& right = edges.right();
    const Numeric& bottom = edges.bottom();
    const Numeric& left = edges.left();

    if (top == right && right == bottom && bottom == left) {
        group["all"] = numericToLeaf(top);
    }
    if (left == right) {
        group["horizontal"] = numericToLeaf(left);
    }
    if (top == bottom) {
        group["vertical"] = numericToLeaf(top);
    }
}

void addCornerConvenience(QJsonObject& group, const Tuple& tuple)
{
    const Corners corners {tuple};
    const Numeric& topLeft = corners.topLeft();
    const Numeric& topRight = corners.topRight();
    const Numeric& bottomRight = corners.bottomRight();
    const Numeric& bottomLeft = corners.bottomLeft();

    if (topLeft == topRight && topRight == bottomRight && bottomRight == bottomLeft) {
        group["all"] = numericToLeaf(topLeft);
    }
}

QJsonObject tupleToGroup(const Tuple& tuple)
{
    QJsonObject group;
    for (size_t index = 0; index < tuple.elements.size(); ++index) {
        const auto& element = tuple.elements[index];
        const QString key = element.name ? QString::fromStdString(*element.name)
                                         : QString::number(static_cast<int>(index));
        const QJsonObject child = valueToJson(*element.value);
        if (!child.isEmpty()) {
            group[key] = child;
        }
    }

    switch (tuple.kind) {
        case TupleKind::Padding:
        case TupleKind::Margins:
        case TupleKind::BorderThickness:
            addEdgeConvenience(group, tuple);
            break;
        case TupleKind::Corners:
            addCornerConvenience(group, tuple);
            break;
        default:
            break;
    }

    return group;
}

QJsonObject valueToJson(const Value& value)
{
    if (value.holds<Base::Color>()) {
        return colorToLeaf(value.get<Base::Color>());
    }
    if (value.holds<Numeric>()) {
        return numericToLeaf(value.get<Numeric>());
    }
    if (value.holds<Tuple>()) {
        return tupleToGroup(value.get<Tuple>());
    }
    if (value.holds<std::string>()) {
        const auto& str = value.get<std::string>();
        if (!str.empty()) {
            return {{"$type", "string"}, {"$value", QString::fromStdString(str)}};
        }
    }
    return {};
}

// Determine the Figma collection a root-level token belongs to based on its value type.
// Routing: colors and color-dominant composites → "Theme"; sizes and layout composites → "Density".
// Sub-tokens of a composite inherit their root's collection via the Python script.
QString collectionFor(const Value& value)
{
    if (value.holds<Base::Color>() || value.holds<std::string>()) {
        return "Theme";
    }
    if (value.holds<Numeric>()) {
        return "Density";
    }
    if (value.holds<Tuple>()) {
        switch (value.get<Tuple>().kind) {
            case TupleKind::LinearGradient:
            case TupleKind::RadialGradient:
            case TupleKind::InnerShadow:
            case TupleKind::ColorEffect:
            case TupleKind::BorderColors:
                return "Theme";
            case TupleKind::Padding:
            case TupleKind::Margins:
            case TupleKind::BorderThickness:
            case TupleKind::Corners:
                return "Density";
            case TupleKind::Generic: {
                const auto& elements = value.get<Tuple>().elements;
                if (!elements.empty() && elements[0].value) {
                    return collectionFor(*elements[0].value);
                }
                return "Density";
            }
        }
    }
    return "Density";
}

// If expression is a plain parameter reference, return the W3C path for the target.
// Handles three forms:
//   @Name          → "Name"
//   @Name.SubKey   → "Name/SubKey"  (dot notation for tuple sub-tokens, converted to W3C slashes)
//   @{Name/Path}   → "Name/Path"    (already slash-separated)
std::optional<std::string> simpleAliasTarget(const std::string& expression)
{
    static const QRegularExpression regex(R"(^\s*@(?:(\w+(?:\.\w+)*)|\{([\w/]+)\})\s*$)");
    const QRegularExpressionMatch match = regex.match(QString::fromStdString(expression));
    if (!match.hasMatch()) {
        return std::nullopt;
    }
    QString target = match.captured(1).isEmpty() ? match.captured(2) : match.captured(1);
    target.replace('.', '/');
    return target.toStdString();
}

// Recursively applies a ColorEffect to every Base::Color leaf inside a Value.
// Numeric, string, and None leaves are returned unchanged; Tuples are deep-copied
// with each color stop modified — mirroring applyEffectToBrush() in FreeCADStyle.cpp
// but operating in the Value domain without any Qt types.
Value applyEffect(const Value& base, const ColorEffect& effect)
{
    if (base.holds<Base::Color>()) {
        return {effect.apply(base.get<Base::Color>())};
    }
    if (base.holds<Tuple>()) {
        const auto& tuple = base.get<Tuple>();
        Tuple modified;
        modified.kind = tuple.kind;
        for (const auto& element : tuple.elements) {
            if (!element.value) {
                continue;
            }
            Value modifiedValue = applyEffect(*element.value, effect);
            if (element.name) {
                modified.elements.push_back(
                    Tuple::Element::named(*element.name, std::move(modifiedValue))
                );
            }
            else {
                modified.elements.push_back(Tuple::Element::unnamed(std::move(modifiedValue)));
            }
        }
        return {std::move(modified)};
    }
    return base;
}

// clang-format off
constexpr std::array<std::string_view, 2> effectProperties = {"BackgroundEffect", "BorderColorEffect"};
constexpr std::array<std::string_view, 5> stateNames       = {"Hovered", "Pressed", "Checked", "Focused", "Disabled"};
// clang-format on

struct EffectMapping
{
    std::string computedName;  // token that should exist, e.g. "ButtonPrimaryHoveredBackground"
    std::string baseName;  // base token the effect is applied to, e.g. "ButtonPrimaryBackground"
};

// Parsed representation of an effect token name.
struct EffectSpec
{
    std::string componentPrefix;  // e.g. "Button" from "ButtonHoveredBackgroundEffect"
    std::string_view state;       // e.g. "Hovered"
    std::string_view baseProp;    // e.g. "Background"
};

// Parses "ButtonHoveredBackgroundEffect" → {componentPrefix="Button", state="Hovered",
// baseProp="Background"}. Returns nullopt for names that don't match a recognised effect property +
// state.
std::optional<EffectSpec> parseEffectName(const std::string& effectName)
{
    for (const auto effectProp : effectProperties) {
        if (!std::string_view(effectName).ends_with(effectProp)) {
            continue;
        }
        const std::string_view baseProp
            = effectProp.substr(0, effectProp.size() - std::string_view("Effect").size());
        const std::string_view withoutProp
            = std::string_view(effectName).substr(0, effectName.size() - effectProp.size());

        for (const auto state : stateNames) {
            if (!withoutProp.ends_with(state)) {
                continue;
            }
            return EffectSpec {
                .componentPrefix
                = std::string(withoutProp.substr(0, withoutProp.size() - state.size())),
                .state = state,
                .baseProp = baseProp,
            };
        }
    }
    return std::nullopt;
}

// Returns true if `middle` contains no state name — used to exclude already-qualified names
// like "ButtonDisabledBackground" (middle = "Disabled") when scanning for base tokens.
bool containsStateName(std::string_view middle)
{
    return std::ranges::any_of(stateNames, [&](std::string_view state) {
        return middle.find(state) != std::string_view::npos;
    });
}

// Finds all base tokens matching the effect's prefix+property, and for each produces an
// EffectMapping: the synthesised computed name and the existing base token name.
//
// Example: EffectSpec{prefix="Button", state="Hovered", baseProp="Background"} scans all
// parameters to find "ButtonBackground", "ButtonPrimaryBackground", "ButtonLinkBackground",
// etc. and returns mappings to "ButtonHoveredBackground", "ButtonPrimaryHoveredBackground",
// "ButtonLinkHoveredBackground", etc. Explicit YAML definitions take priority at call site.
std::vector<EffectMapping> findEffectMappings(const EffectSpec& spec, const std::list<Parameter>& params)
{
    std::vector<EffectMapping> mappings;
    for (const auto& param : params) {
        const std::string_view name = param.name;
        if (!name.starts_with(spec.componentPrefix) || !name.ends_with(spec.baseProp)) {
            continue;
        }
        const std::string_view middle = name.substr(
            spec.componentPrefix.size(),
            name.size() - spec.componentPrefix.size() - spec.baseProp.size()
        );
        if (containsStateName(middle)) {
            continue;
        }
        const std::string basePrefix(name.substr(0, name.size() - spec.baseProp.size()));
        mappings.push_back({
            .computedName = basePrefix + std::string(spec.state) + std::string(spec.baseProp),
            .baseName = std::string(name),
        });
    }
    return mappings;
}

// Recursively merge src into dst: sub-objects are merged, other values are overwritten.
void deepMerge(QJsonObject& dst, const QJsonObject& src)
{
    for (auto it = src.constBegin(); it != src.constEnd(); ++it) {
        if (it.value().isObject() && dst.contains(it.key()) && dst[it.key()].isObject()) {
            auto child = dst[it.key()].toObject();
            deepMerge(child, it.value().toObject());
            dst[it.key()] = child;
        }
        else {
            dst[it.key()] = it.value();
        }
    }
}

// Builds a W3C token entry for the given parameter, preserving aliases when possible.
// Returns nullopt if the resolved value produces no JSON representation.
std::optional<QJsonObject> buildTokenEntry(
    const ParameterManager& manager,
    const std::string& name,
    const Value& resolved
)
{
    QJsonObject entry;

    if (!resolved.holds<Tuple>()) {
        if (const auto expr = manager.expression(name)) {
            if (const auto target = simpleAliasTarget(*expr)) {
                entry = valueToJson(resolved);
                entry["$value"] = QString("{%1}").arg(QString::fromStdString(*target));
            }
        }
    }

    if (entry.isEmpty()) {
        entry = valueToJson(resolved);
    }

    if (entry.isEmpty()) {
        return std::nullopt;
    }

    return entry;
}

QJsonObject annotatedEntry(
    QJsonObject entry,
    const Value& value,
    const std::string& name,
    const ParameterDescriptorRegistry& registry
)
{
    QJsonObject freecadExt {{"collection", collectionFor(value)}};
    if (const auto parsed = registry.parse(name)) {
        freecadExt["component"] = QString::fromStdString(parsed->component);
    }
    entry["$extensions"] = QJsonObject {{"freecad", freecadExt}};
    return entry;
}

// Emits a synthesised computed token into root unless the name is already occupied by
// an explicit YAML definition (which takes priority).
void emitComputedEffectToken(
    QJsonObject& root,
    const std::string& computedName,
    const Value& computed,
    const ParameterDescriptorRegistry& registry
)
{
    const QString qname = QString::fromStdString(computedName);
    if (root.contains(qname)) {
        return;
    }
    QJsonObject entry = valueToJson(computed);
    if (entry.isEmpty()) {
        return;
    }
    root[qname] = annotatedEntry(std::move(entry), computed, computedName, registry);
}

}  // namespace

TokenExporter::TokenExporter(const ParameterManager& manager)
    : manager(&manager)
{}

QString TokenExporter::exportW3CTokens() const
{
    QJsonObject root;
    root["$schema"] = "https://design-tokens.github.io/community-group/format/";

    const auto& registry = manager->descriptorRegistry();
    const auto& params = manager->parameters();

    for (const auto& param : params) {
        const auto resolved = manager->resolve(param.name);
        if (!resolved) {
            continue;
        }

        // buildTokenEntry handles alias preservation for non-tuple tokens.
        auto entry = buildTokenEntry(*manager, param.name, *resolved);
        if (!entry) {
            continue;
        }

        *entry = annotatedEntry(std::move(*entry), *resolved, param.name, registry);

        const QString name = QString::fromStdString(param.name);

        if (root.contains(name) && root[name].isObject()) {
            auto existing = root[name].toObject();
            deepMerge(existing, *entry);
            root[name] = existing;
        }
        else {
            root[name] = *entry;
        }
    }

    // Synthesize computed tokens for each *Effect parameter. For each effect, find ALL
    // base tokens with the same component prefix and property — including variant overrides
    // like ButtonPrimaryBackground — and emit a state-qualified token for each one.
    // Explicit YAML definitions take priority; emitComputedEffectToken skips occupied names.
    for (const auto& param : params) {
        const auto spec = parseEffectName(param.name);
        if (!spec) {
            continue;
        }
        const auto effectValue = manager->resolve(param.name);
        if (!effectValue) {
            continue;
        }
        const ColorEffect effect(*effectValue);

        for (const auto& mapping : findEffectMappings(*spec, params)) {
            const auto baseValue = manager->resolve(mapping.baseName);
            if (!baseValue) {
                continue;
            }
            emitComputedEffectToken(root, mapping.computedName, applyEffect(*baseValue, effect), registry);
        }
    }

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

}  // namespace Gui::StyleParameters
// NOLINTEND(*-pro-bounds-avoid-unchecked-container-access)
