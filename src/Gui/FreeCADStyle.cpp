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
 *   FreeCAD is distributed in the hope that it will be us
 *   eful, but         *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with FreeCAD. If not, see                                *
 *   <https://www.gnu.org/licenses/>.                                       *
 *                                                                          *
 ***************************************************************************/


#include "PreCompiled.h"

#ifndef _PreComp_
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QStyleOption>
#include <QWidget>
#endif

#include "Application.h"
#include "Utilities.h"
#include "FreeCADStyle.h"
#include "StyleParameters/ColorEffect.h"
#include "StyleParameters/Corners.h"
#include "StyleParameters/Edges.h"
#include "StyleParameters/InnerShadow.h"
#include "StyleParameters/Insets.h"
#include "StyleParameters/ParameterDescriptorRegistry.h"
#include "StyleParameters/ParameterManager.h"

// Qt exports its blur but does not declare it in any public header, and there is no public
// equivalent. The declaration has to match QtWidgets' own, namespace included.
QT_BEGIN_NAMESPACE
extern Q_WIDGETS_EXPORT void qt_blurImage(
    QPainter* painter,
    QImage& blurImage,
    qreal radius,
    bool quality,
    bool alphaOnly,
    int transposed = 0
);
QT_END_NAMESPACE

using namespace Gui;
using namespace Gui::StyleParameters;

namespace Base
{

template<>
FreeCADStyle::CornerRadii convertTo<FreeCADStyle::CornerRadii, Corners>(const Corners& corners)
{
    return {
        .topLeft = corners.topLeft(),
        .topRight = corners.topRight(),
        .bottomRight = corners.bottomRight(),
        .bottomLeft = corners.bottomLeft(),
    };
}

template<>
FreeCADStyle::InnerShadow convertTo<FreeCADStyle::InnerShadow, InnerShadow>(const InnerShadow& shadow)
{
    return {
        .color = shadow.color().asValue<QColor>(),
        .x = shadow.x(),
        .y = shadow.y(),
        .blur = shadow.blur(),
    };
}

template<>
FreeCADStyle::BorderColorsPerSide convertTo<FreeCADStyle::BorderColorsPerSide, BorderColors>(
    const BorderColors& borderColors
)
{
    return {
        .top = borderColors.top().asValue<QColor>(),
        .right = borderColors.right().asValue<QColor>(),
        .bottom = borderColors.bottom().asValue<QColor>(),
        .left = borderColors.left().asValue<QColor>(),
    };
}

}  // namespace Base

FreeCADStyle::CornerRadii FreeCADStyle::CornerRadii::resolve(QSizeF size) const
{
    const qreal minDimension = std::min(size.width(), size.height());
    auto resolveOne =
        [minDimension](const StyleParameters::Numeric& radius) -> StyleParameters::Numeric {
        if (radius.unit == "%") {
            return {.value = radius.value / 100.0 * minDimension, .unit = "px"};
        }
        return radius;
    };
    return {
        .topLeft = resolveOne(topLeft),
        .topRight = resolveOne(topRight),
        .bottomRight = resolveOne(bottomRight),
        .bottomLeft = resolveOne(bottomLeft),
    };
}


// Arc start angles (in degrees) for each corner of a clockwise rounded rectangle.
constexpr qreal arcStartTopRight = 90;
constexpr qreal arcStartBottomRight = 0;
constexpr qreal arcStartBottomLeft = 270;
constexpr qreal arcStartTopLeft = 180;
constexpr qreal arcSweepClockwise = -90;

QPainterPath roundedRectPath(const QRectF& rect, const FreeCADStyle::CornerRadii& radii)
{
    // Resolve percent radii then clamp to at most half the shorter side.
    const FreeCADStyle::CornerRadii resolved = radii.resolve(rect.size());
    const qreal maxRadius = std::min(rect.width(), rect.height()) / 2.0;
    const qreal topLeft = std::min(resolved.topLeft.value, maxRadius);
    const qreal topRight = std::min(resolved.topRight.value, maxRadius);
    const qreal bottomRight = std::min(resolved.bottomRight.value, maxRadius);
    const qreal bottomLeft = std::min(resolved.bottomLeft.value, maxRadius);

    QPainterPath path;
    path.moveTo(rect.left() + topLeft, rect.top());
    path.lineTo(rect.right() - topRight, rect.top());
    path.arcTo(
        rect.right() - (2 * topRight),
        rect.top(),
        2 * topRight,
        2 * topRight,
        arcStartTopRight,
        arcSweepClockwise
    );
    path.lineTo(rect.right(), rect.bottom() - bottomRight);
    path.arcTo(
        rect.right() - (2 * bottomRight),
        rect.bottom() - (2 * bottomRight),
        2 * bottomRight,
        2 * bottomRight,
        arcStartBottomRight,
        arcSweepClockwise
    );
    path.lineTo(rect.left() + bottomLeft, rect.bottom());
    path.arcTo(
        rect.left(),
        rect.bottom() - (2 * bottomLeft),
        2 * bottomLeft,
        2 * bottomLeft,
        arcStartBottomLeft,
        arcSweepClockwise
    );
    path.lineTo(rect.left(), rect.top() + topLeft);
    path.arcTo(rect.left(), rect.top(), 2 * topLeft, 2 * topLeft, arcStartTopLeft, arcSweepClockwise);
    path.closeSubpath();
    return path;
}

// Computes inner corner radii after subtracting border thickness.
// Expects outer to already be resolved to absolute pixels (no "%" unit).
FreeCADStyle::CornerRadii innerRadii(const FreeCADStyle::CornerRadii& outer, const QMarginsF& thickness)
{
    auto shrink = [](qreal radius, qreal a, qreal b) -> qreal {
        return std::max(0.0, radius - std::max(a, b));
    };
    return {
        .topLeft
        = {.value = shrink(outer.topLeft.value, thickness.top(), thickness.left()), .unit = "px"},
        .topRight
        = {.value = shrink(outer.topRight.value, thickness.top(), thickness.right()), .unit = "px"},
        .bottomRight
        = {.value = shrink(outer.bottomRight.value, thickness.bottom(), thickness.right()),
           .unit = "px"},
        .bottomLeft
        = {.value = shrink(outer.bottomLeft.value, thickness.bottom(), thickness.left()),
           .unit = "px"},
    };
}



namespace
{

struct ShadowCacheKey
{
    int width, height;
    qreal x, y, blur;
    QRgb color;
    qreal radiusTopLeft, radiusTopRight, radiusBottomRight, radiusBottomLeft;

    auto operator<=>(const ShadowCacheKey&) const = default;
};

QImage buildShadowImage(
    const QRect& rect,
    const FreeCADStyle::CornerRadii& radii,
    const FreeCADStyle::InnerShadow& shadow
)
{
    const int padding = static_cast<int>(std::ceil(shadow.blur)) + 1;
    const QSize imageSize = rect.size() + QSize(2 * padding, 2 * padding);

    // Create a fully opaque black image and punch a transparent hole in the shape.
    // The opaque ring that remains around the hole produces the shadow after blurring.
    QImage mask(imageSize, QImage::Format_ARGB32_Premultiplied);
    mask.fill(Qt::black);

    {
        QPainter maskPainter(&mask);
        maskPainter.setRenderHint(QPainter::Antialiasing);
        maskPainter.setCompositionMode(QPainter::CompositionMode_Clear);
        maskPainter.fillPath(
            roundedRectPath(QRectF(padding, padding, rect.width(), rect.height()), radii),
            Qt::transparent
        );
    }

    QImage blurred(imageSize, QImage::Format_ARGB32_Premultiplied);
    blurred.fill(Qt::transparent);
    {
        QPainter blurPainter(&blurred);
        qt_blurImage(&blurPainter, mask, shadow.blur, false, false);
    }

    // Tint the blurred image with the shadow color.
    {
        QPainter tintPainter(&blurred);
        tintPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        tintPainter.fillRect(blurred.rect(), shadow.color);
    }

    return blurred;
}

const QImage& getCachedShadowImage(
    const QRect& rect,
    const FreeCADStyle::CornerRadii& radii,
    const FreeCADStyle::InnerShadow& shadow
)
{
    constexpr int maxCacheEntries = 32;
    static std::map<ShadowCacheKey, QImage> cache;

    const ShadowCacheKey key {
        .width = rect.width(),
        .height = rect.height(),
        .x = shadow.x,
        .y = shadow.y,
        .blur = shadow.blur,
        .color = shadow.color.rgba(),
        .radiusTopLeft = radii.topLeft.value,
        .radiusTopRight = radii.topRight.value,
        .radiusBottomRight = radii.bottomRight.value,
        .radiusBottomLeft = radii.bottomLeft.value,
    };

    if (auto it = cache.find(key); it != cache.end()) {
        return it->second;
    }
    if (static_cast<int>(cache.size()) >= maxCacheEntries) {
        cache.erase(cache.begin());
    }
    return cache.emplace(key, buildShadowImage(rect, radii, shadow)).first->second;
}

}  // namespace

// Fills each side of a border ring with its own color using CSS-style diagonal corner splits.
// Four non-overlapping trapezoids partition the border ring; corners are split diagonally,
// matching CSS border-color behaviour.
static void drawBorderRingSided(
    QPainter* painter,
    const QRect& rect,
    const QPainterPath& borderRingPath,
    const QMarginsF& thickness,
    const FreeCADStyle::BorderColorsPerSide& colors
)
{
    const auto fillSide = [&](const QPolygonF& trapezoid, QColor color) {
        QPainterPath clip;
        clip.addPolygon(trapezoid);
        clip.closeSubpath();
        painter->fillPath(borderRingPath.intersected(clip), QBrush(color));
    };

    // Use QRectF to match the exclusive right/bottom used in borderRingPath (built from
    // QRectF(rect)). QRect::right() = left() + width() - 1 (inclusive), QRectF::right() = left() +
    // width() (exclusive).
    const QRectF rectF(rect);
    const qreal rectLeft = rectF.left();
    const qreal rectRight = rectF.right();
    const qreal rectTop = rectF.top();
    const qreal rectBottom = rectF.bottom();

    const qreal innerLeft = rectLeft + thickness.left();
    const qreal innerRight = rectRight - thickness.right();
    const qreal innerTop = rectTop + thickness.top();
    const qreal innerBottom = rectBottom - thickness.bottom();

    // Fill the entire ring with the top color first.
    // This acts as a base so that anti-aliased pixels at the diagonal corner boundaries blend
    // between the two adjacent side colors rather than exposing the painter background.
    // Without this, intersected() produces semi-transparent edges on both trapezoids at the
    // shared diagonal, leaving a gap where the background shows through.
    painter->fillPath(borderRingPath, QBrush(colors.top));

    // Overwrite right, bottom, left with their own colors.
    // Anti-aliasing at each diagonal blends the overdraw color into the top base — no gap.
    // clang-format off
    fillSide({{rectRight, rectTop},    {rectRight,  rectBottom}, {innerRight, innerBottom}, {innerRight, innerTop}},    colors.right);
    fillSide({{rectRight, rectBottom}, {rectLeft,   rectBottom}, {innerLeft,  innerBottom}, {innerRight, innerBottom}}, colors.bottom);
    fillSide({{rectLeft,  rectBottom}, {rectLeft,   rectTop},   {innerLeft,  innerTop},    {innerLeft,  innerBottom}},  colors.left);
    // clang-format on
}


namespace
{

/**
 * @brief Applies a ColorEffect to each color in a QBrush.
 *
 * Solid brushes are shifted directly. Gradient brushes have each stop color
 * shifted individually so the gradient shape is preserved.
 */
QBrush applyEffectToBrush(const QBrush& brush, const ColorEffect& effect)
{
    const auto applyToColor = [&](const QColor& color) -> QColor {
        return effect.apply(Base::Color::fromValue(color)).asValue<QColor>();
    };

    if (brush.style() == Qt::SolidPattern) {
        return QBrush(applyToColor(brush.color()));
    }

    if (const QGradient* gradient = brush.gradient()) {
        QGradientStops stops = gradient->stops();
        for (auto& [position, color] : stops) {
            color = applyToColor(color);
        }

        // clang-format off
        switch (gradient->type()) {
            case QGradient::LinearGradient: {
                const auto* linear = static_cast<const QLinearGradient*>(gradient);
                QLinearGradient result(linear->start(), linear->finalStop());
                result.setStops(stops);
                result.setSpread(gradient->spread());
                result.setCoordinateMode(gradient->coordinateMode());
                return result;
            }
            case QGradient::RadialGradient: {
                const auto* radial = static_cast<const QRadialGradient*>(gradient);
                QRadialGradient result(radial->center(), radial->radius(), radial->focalPoint());
                result.setStops(stops);
                result.setSpread(gradient->spread());
                result.setCoordinateMode(gradient->coordinateMode());
                return result;
            }
            case QGradient::ConicalGradient: {
                const auto* conical = static_cast<const QConicalGradient*>(gradient);
                QConicalGradient result(conical->center(), conical->angle());
                result.setStops(stops);
                result.setCoordinateMode(gradient->coordinateMode());
                return result;
            }
            default:
                break;
        }
        // clang-format on
    }

    return brush;  // unsupported brush type — return unchanged
}

}  // namespace

void FreeCADStyle::drawBoxBackground(
    QPainter* painter,
    const QRect& rect,
    const BoxStyleDefinition& rule,
    const QPainterPath& borderMask
)
{
    const bool hasBorder = rule.borderColor.has_value() && rule.borderThickness.has_value();
    const bool hasBackground = rule.background.style() != Qt::NoBrush;
    const bool hasInnerShadow = rule.innerShadow.has_value();

    if (!hasBackground && !hasBorder && !hasInnerShadow) {
        return;
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    painter->setClipRect(rect, Qt::IntersectClip);

    // Resolve percent radii once against the rect size; all downstream helpers expect absolute px.
    const CornerRadii resolvedBorderRadius = rule.borderRadius.resolve(rect.size());
    const QRect backgroundRect = rect;

    if (hasBackground) {
        QRectF backgroundInnerRect = backgroundRect;

        // In case of border being applied, shrink the background to half of the border thickness
        // so it does not bleed out to outer background.
        if (rule.borderThickness) {
            backgroundInnerRect = backgroundInnerRect.marginsRemoved(*rule.borderThickness / 2);
        }

        painter->fillPath(
            roundedRectPath(QRectF(backgroundInnerRect), resolvedBorderRadius),
            rule.background
        );
    }

    if (hasInnerShadow) {
        const int padding = static_cast<int>(std::ceil(rule.innerShadow->blur)) + 1;
        const QImage& shadowImage = getCachedShadowImage(rect, resolvedBorderRadius, *rule.innerShadow);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setClipPath(roundedRectPath(QRectF(rect), resolvedBorderRadius), Qt::IntersectClip);
        painter->drawImage(
            QPointF(
                rect.left() - padding + rule.innerShadow->x,
                rect.top() - padding + rule.innerShadow->y
            ),
            shadowImage
        );
        painter->restore();
    }

    if (hasBorder) {
        const QMarginsF& thickness = *rule.borderThickness;

        // Snap each border side to the nearest integer pixel.
        const QMarginsF snappedThickness(
            qRound(thickness.left()),
            qRound(thickness.top()),
            qRound(thickness.right()),
            qRound(thickness.bottom())
        );

        const QRect innerRect = backgroundRect.marginsRemoved(thickness.toMargins());

        // Subtract inner from outer path to fill only the border ring, preserving transparency.
        const QPainterPath outerPath = roundedRectPath(QRectF(rect), resolvedBorderRadius);
        const QPainterPath innerPath
            = roundedRectPath(QRectF(innerRect), innerRadii(resolvedBorderRadius, snappedThickness));
        const QPainterPath fullRing = outerPath.subtracted(innerPath);
        const QPainterPath borderRingPath = borderMask.isEmpty() ? fullRing
                                                                 : fullRing.intersected(borderMask);

        const BorderColorsPerSide& colors = *rule.borderColor;
        if (colors.isUniform()) {
            painter->fillPath(borderRingPath, QBrush(colors.uniform()));
        }
        else {
            drawBorderRingSided(painter, rect, borderRingPath, snappedThickness, colors);
        }
    }

    painter->restore();
}

void FreeCADStyle::drawComponent(QPainter* painter, const QRect& rect, const StyleContext& context) const
{
    drawBoxBackground(painter, rect, resolveBoxStyle(context));
}

void FreeCADStyle::drawComponent(
    QPainter* painter,
    const QRect& rect,
    const QWidget* widget,
    const QStyleOption* option
) const
{
    drawComponent(painter, rect, contextOf(widget, option));
}

void FreeCADStyle::paintBox(
    QPainter* painter,
    const QRect& rect,
    const StyleParameters::StyleContext& context
) const
{
    // Inset by the resolved Margin before painting, matching drawComponent: the margin is a
    // BoxGeometry property, so a box painted from a bare rect would otherwise ignore it.
    const QRect borderRect = resolveBoxGeometry(context).borderRect(rect);
    drawBoxBackground(painter, borderRect, resolveBoxStyle(context));
}

FreeCADStyle::BoxGeometryDefinition FreeCADStyle::resolveBoxGeometry(const StyleContext& context) const
{
    const uint64_t key = context.cacheKey();

    if (const auto cached = boxGeometryCache.find(key); cached != boxGeometryCache.end()) {
        return cached->second;
    }

    BoxGeometryDefinition result;

    if (const auto padding = resolve<Insets>(context, StyleProperty::Padding)) {
        result.padding = Base::convertTo<QMarginsF>(*padding);
    }

    if (const auto margin = resolve<Insets>(context, StyleProperty::Margin)) {
        result.margin = Base::convertTo<QMarginsF>(*margin);
    }

    if (const auto height = resolve<Numeric>(context, StyleProperty::Height)) {
        result.height = static_cast<int>(*height);
    }

    if (const auto minWidth = resolve<Numeric>(context, StyleProperty::MinWidth)) {
        result.minWidth = static_cast<int>(*minWidth);
    }

    if (const auto resolvedWidth = resolve<Numeric>(context, StyleProperty::Width)) {
        result.width = static_cast<int>(*resolvedWidth);
    }

    if (const auto resolvedMaxWidth = resolve<Numeric>(context, StyleProperty::MaxWidth)) {
        result.maxWidth = static_cast<int>(*resolvedMaxWidth);
    }

    if (const auto resolvedMinHeight = resolve<Numeric>(context, StyleProperty::MinHeight)) {
        result.minHeight = static_cast<int>(*resolvedMinHeight);
    }

    if (const auto resolvedMaxHeight = resolve<Numeric>(context, StyleProperty::MaxHeight)) {
        result.maxHeight = static_cast<int>(*resolvedMaxHeight);
    }

    if (const auto spacing = resolve<Numeric>(context, StyleProperty::IconSpacing)) {
        result.iconSpacing = static_cast<int>(*spacing);
    }

    boxGeometryCache[key] = result;
    return result;
}

FreeCADStyle::BoxStyleDefinition FreeCADStyle::resolveBoxStyle(const StyleContext& context) const
{
    const uint64_t key = context.cacheKey();

    if (const auto cached = boxStyleCache.find(key); cached != boxStyleCache.end()) {
        return cached->second;
    }

    BoxStyleDefinition result;

    if (const auto background = resolve(context, StyleProperty::Background)) {
        result.background = Base::convertTo<QBrush>(*background);
    }
    if (const auto borderRadius = resolve<Corners>(context, StyleProperty::BorderRadius)) {
        result.borderRadius = Base::convertTo<CornerRadii>(*borderRadius);
    }
    if (const auto borderThickness = resolve<Insets>(context, StyleProperty::BorderThickness)) {
        result.borderThickness = Base::convertTo<QMarginsF>(*borderThickness);
    }
    if (const auto borderColors = resolve<BorderColors>(context, StyleProperty::BorderColor)) {
        result.borderColor = Base::convertTo<BorderColorsPerSide>(*borderColors);
    }

    if (const auto innerShadow
        = resolve<StyleParameters::InnerShadow>(context, StyleProperty::InnerShadow)) {
        result.innerShadow = Base::convertTo<InnerShadow>(*innerShadow);
    }

    if (const auto effect = resolve<ColorEffect>(context, StyleProperty::BackgroundEffect)) {
        result.background = applyEffectToBrush(result.background, *effect);
    }

    if (const auto effect = resolve<ColorEffect>(context, StyleProperty::BorderColorEffect)) {
        if (result.borderColor) {
            auto& colors = *result.borderColor;
            for (QColor* side : {&colors.top, &colors.right, &colors.bottom, &colors.left}) {
                *side = effect->apply(Base::Color::fromValue(*side)).asValue<QColor>();
            }
        }
    }

    boxStyleCache[key] = result;
    return result;
}

void FreeCADStyle::polish(QPalette& palette)
{
    QProxyStyle::polish(palette);

    // Sets role in both Active and Inactive groups; leaves Disabled unchanged.
    const auto set = [&](QPalette::ColorRole role, std::string_view token) {
        if (const auto color = resolve<Base::Color>(token)) {
            palette.setColor(QPalette::Active, role, color->asValue<QColor>());
            palette.setColor(QPalette::Inactive, role, color->asValue<QColor>());
        }
    };

    // Sets role only in the Disabled group.
    const auto setDisabled = [&](QPalette::ColorRole role, std::string_view token) {
        if (const auto color = resolve<Base::Color>(token)) {
            palette.setColor(QPalette::Disabled, role, color->asValue<QColor>());
        }
    };

    // Window surfaces
    set(QPalette::Window, "BaseWindowBackground");
    set(QPalette::WindowText, "BaseTextColor");

    // Input / item-view surfaces
    set(QPalette::Base, "BaseInputBackground");
    set(QPalette::AlternateBase, "BaseAlternateBackground");
    set(QPalette::Text, "BaseTextColor");
    set(QPalette::PlaceholderText, "BasePlaceholderTextColor");

    // Buttons
    set(QPalette::Button, "BaseButtonBackground");
    set(QPalette::ButtonText, "ButtonTextColor");

    // Selection
    set(QPalette::Highlight, "BaseHighlightBackground");
    set(QPalette::HighlightedText, "BaseHighlightTextColor");
    set(QPalette::BrightText, "BaseHighlightTextColor");

    // Links
    set(QPalette::Link, "BaseLinkColor");
    set(QPalette::LinkVisited, "BaseLinkColor");  // themes can override if needed

    // Tooltips
    set(QPalette::ToolTipBase, "BaseTooltipBackground");
    set(QPalette::ToolTipText, "BaseTooltipTextColor");

    // 3D shading (used by non-custom widgets for borders and shadows)
    set(QPalette::Light, "BaseShadingLight");
    set(QPalette::Midlight, "BaseShadingMidlight");
    set(QPalette::Mid, "BaseShadingMid");
    set(QPalette::Dark, "BaseShadingDark");
    set(QPalette::Shadow, "BaseShadingShadow");

    setDisabled(QPalette::WindowText, "BaseDisabledTextColor");
    setDisabled(QPalette::Text, "BaseDisabledTextColor");
    setDisabled(QPalette::ButtonText, "BaseDisabledTextColor");
    setDisabled(QPalette::PlaceholderText, "BaseDisabledTextColor");
    setDisabled(QPalette::Base, "BaseWindowBackground");
    setDisabled(QPalette::Button, "BaseDisabledBackground");
    setDisabled(QPalette::Highlight, "BaseShadingMid");
    setDisabled(QPalette::HighlightedText, "BaseDisabledTextColor");
}

int FreeCADStyle::styleHint(
    StyleHint hint,
    const QStyleOption* option,
    const QWidget* widget,
    QStyleHintReturn* returnData
) const
{
    // A dialog's buttons are labelled, and the platform's stock icons for them are drawn from
    // a different icon set than everything else in the window. Declining the hint keeps a
    // button box reading as a row of words.
    if (hint == SH_DialogButtonBox_ButtonsHaveIcons) {
        return 0;
    }

    return QProxyStyle::styleHint(hint, option, widget, returnData);
}


StyleContext FreeCADStyle::contextOf(
    const QWidget* widget,
    const QStyleOption* option,
    const StyleComponentElement& element
)
{
    StyleContext context;
    context.element = element;

    // Component override — derived from the "component" widget property.
    // For unrecognised widget types the name is resolved to a StyleComponent enum value
    // when possible (e.g. QFrame[component="List"] → StyleComponent::List). For recognised
    // widget types the name becomes a prefix override (e.g. "DocumentTree" on a QTreeView).
    if (widget) {
        const std::string overrideName = widget->property("component").toString().toStdString();
        if (!overrideName.empty()) {
            auto* manager = Application::Instance->styleParameterManager();

            if (const auto namedComponent = manager->descriptorRegistry().findComponent(overrideName)) {
                context.component = *namedComponent;
            }
            else {
                context.componentOverride = overrideName;
            }
        }
    }

    // State — all active flags captured as a bitmask.
    if (option) {
        if (!(option->state & QStyle::State_Enabled)) {
            context.state |= StyleState::Disabled;
        }
        if (option->state & QStyle::State_MouseOver) {
            context.state |= StyleState::Hovered;
        }
        if (option->state & QStyle::State_On) {
            context.state |= StyleState::Checked;
        }
        if (option->state & QStyle::State_HasFocus) {
            context.state |= StyleState::Focused;
        }
    }

    bindWidget(context, widget);

    return context;
}

void FreeCADStyle::bindWidget(StyleContext& context, const QWidget* widget)
{
    context.widget = widget;
}

std::optional<Value> FreeCADStyle::resolve(std::string_view name) const
{
    return Application::Instance->styleParameterManager()->resolve(std::string(name));
}

std::optional<Value> FreeCADStyle::resolve(std::initializer_list<std::string_view> names) const
{
    for (const std::string_view name : names) {
        if (auto value = resolve(name)) {
            return value;
        }
    }
    return std::nullopt;
}

std::optional<Value> FreeCADStyle::resolve(const StyleContext& context, StyleProperty property) const
{
    const uint64_t key = context.cacheKey(property);

    if (const auto cached = tokenCache.find(key); cached != tokenCache.end()) {
        return cached->second;
    }

    auto* manager = Application::Instance->styleParameterManager();

    const std::string propertySuffix(propertyString(property));
    std::optional<Value> result;

    for (const std::string& prefix : manager->descriptorRegistry().buildPrefixes(context)) {
        // Use the flat resolver per prefix: the prefix list IS the inheritance
        // walk, so name-based chain synthesis must not run here.
        result = manager->resolve(prefix + propertySuffix, StyleParameters::ParameterManager::ResolveContext {});
        if (result) {
            break;
        }
    }

    tokenCache[key] = result;
    return result;
}

void FreeCADStyle::clearTokenCache()
{
    tokenCache.clear();
    boxStyleCache.clear();
    boxGeometryCache.clear();
    StyleContext::Intern::global().clear();
}

bool FreeCADStyle::eventFilter(QObject* obj, QEvent* event)
{
    // This is a hacky fix for https://github.com/FreeCAD/FreeCAD/issues/23607
    // Basically after widget is shown or polished we enforce it's minimum size to at least cover
    // the minimum size hint - something that QSS ignores if min-width is specified
    if (event->type() == QEvent::Polish || event->type() == QEvent::Show) {
        if (auto* btn = qobject_cast<QPushButton*>(obj)) {
            btn->setMinimumWidth(std::max(btn->minimumSizeHint().width(), btn->minimumWidth()));
        }
    }

    return QObject::eventFilter(obj, event);
}
