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
#include <array>
#include <map>
#include <QPainter>
#include <QPainterPath>
#include <QApplication>
#include <QCoreApplication>
#include <QPalette>
#include <QPushButton>
#include <QMenuBar>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QStyleOptionButton>
#include <QStyleOptionToolButton>
#include <QStyleOption>
#include <QWidget>
#endif

#include "Application.h"
#include "IconManager.h"
#include "ThemeReloadEvent.h"
#include "Utilities.h"
#include "FreeCADStyle.h"
#include "StyleParameters/ColorEffect.h"
#include "StyleParameters/Corners.h"
#include "StyleParameters/Edges.h"
#include "StyleParameters/InnerShadow.h"
#include "StyleParameters/Insets.h"
#include "StyleParameters/ParameterDescriptorRegistry.h"
#include "StyleParameters/ParameterManager.h"
#include "StyleParameters/StyleOverrides.h"

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

namespace
{

/**
 * @brief Applies the standard 4-way edge rotation to an array of values.
 *
 * Canonical (North) order: (left/topLeft, top/topRight, right/bottomRight, bottom/bottomLeft).
 * South swaps opposite pairs; East/West rotate by one step in either direction.
 */
template<typename T>
std::array<T, 4> rotate4(std::array<T, 4> values, Position position)
{
    // The array has known size - bounds are guaranteed
    // NOLINTBEGIN(*-pro-bounds-avoid-unchecked-container-access)
    // clang-format off
    switch (position) {
        case Position::South: return {values[2], values[3], values[0], values[1]};
        case Position::East:  return {values[3], values[0], values[1], values[2]};
        case Position::West:  return {values[1], values[2], values[3], values[0]};
        default:              return values;
    }
    // clang-format on
    // NOLINTEND(*-pro-bounds-avoid-unchecked-container-access)
}

/** @brief Rotates canonical (North) margins to the given position. */
QMarginsF rotated(const QMarginsF& margins, Position position)
{
    const auto [left, top, right, bottom] = rotate4(
        std::to_array({margins.left(), margins.top(), margins.right(), margins.bottom()}),
        position
    );
    return {left, top, right, bottom};
}
// clang-format on

// ─── Color effect helpers ─────────────────────────────────────────────────────
/** @brief Rotates canonical (North) corner radii to the given position. */
FreeCADStyle::CornerRadii rotated(const FreeCADStyle::CornerRadii& corners, Position position)
{
    const auto [topLeft, topRight, bottomRight, bottomLeft] = rotate4(
        std::to_array({corners.topLeft, corners.topRight, corners.bottomRight, corners.bottomLeft}),
        position
    );
    return {.topLeft = topLeft, .topRight = topRight, .bottomRight = bottomRight, .bottomLeft = bottomLeft};
}

/**
 * @brief Rotates a canonical (North, top→bottom) linear gradient brush to the given position.
 *
 * Point transform: North=(px,py), South=(px,1-py), East=(1-py,px), West=(py,1-px).
 * Non-linear-gradient brushes are returned unchanged.
 */
// clang-format off
QBrush rotated(const QBrush& brush, Position position)
{
    if (position == Position::North) {
        return brush;
    }
    const QGradient* gradient = brush.gradient();
    if (!gradient || gradient->type() != QGradient::LinearGradient) {
        return brush;
    }
    const auto* linear = static_cast<const QLinearGradient*>(gradient);
    const auto rotatePoint = [position](const QPointF& pointF) -> QPointF {
        switch (position) {
            case Position::South: return {pointF.x(),       1.0 - pointF.y()};
            case Position::East:  return {1.0 - pointF.y(), pointF.x()      };
            case Position::West:  return {pointF.y(),       1.0 - pointF.x()};
            default:              return pointF;
        }
    };

    QLinearGradient result(rotatePoint(linear->start()), rotatePoint(linear->finalStop()));
    result.setStops(linear->stops());
    result.setCoordinateMode(linear->coordinateMode());
    result.setSpread(linear->spread());

    return result;
}


}  // namespace

FreeCADStyle::BoxStyleDefinition FreeCADStyle::seamedBoxStyle(
    const StyleContext& context,
    SeamEdge seam,
    SeamBorder border
) const
{
    BoxStyleDefinition style = resolveBoxStyle(context);

    if (seam == SeamEdge::None) {
        return style;
    }

    if (border == SeamBorder::Drop && style.borderThickness.has_value()) {
        switch (seam) {
            case SeamEdge::Left:
                style.borderThickness->setLeft(0);
                break;
            case SeamEdge::Right:
                style.borderThickness->setRight(0);
                break;
            case SeamEdge::Top:
                style.borderThickness->setTop(0);
                break;
            case SeamEdge::Bottom:
                style.borderThickness->setBottom(0);
                break;
            case SeamEdge::None:
                break;
        }
    }

    switch (seam) {
        case SeamEdge::Left:
            style.borderRadius.setLeft(0);
            break;
        case SeamEdge::Right:
            style.borderRadius.setRight(0);
            break;
        case SeamEdge::Top:
            style.borderRadius.setTop(0);
            break;
        case SeamEdge::Bottom:
            style.borderRadius.setBottom(0);
            break;
        case SeamEdge::None:
            break;
    }

    return style;
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

bool FreeCADStyle::transparencyBelow(const QWidget* widget) const
{
    if (widget == nullptr) {
        return false;
    }

    // Only the propagated tag describes the surface: it says the widget is painted over
    // something see-through. The Transparent variant contextOf() derives for a toolbar hosted
    // in a status bar means the opposite direction — suppress my own chrome, I blend into an
    // opaque host — and must not be mistaken for a see-through surface for the children.
    // A theme that does want such a toolbar's children in the Transparent chain says so with
    // ToolBarTransparentIsTransparent, which resolves here for exactly that widget.
    return resolve<bool>(contextOf(widget), StyleProperty::IsTransparent).value_or(isTransparent(widget));
}

void FreeCADStyle::tagWidgetTransparency(QWidget* widget, bool surface) const
{
    if (isTransparent(widget) == surface) {
        return;
    }

    widget->setProperty(transparencyProperty, surface);

    // The tag changes padding, spacing and height tokens as well as colours, so a repaint
    // is not enough — QTabBar caches its tab sizes until the style changes.
    notifyStyleChange(widget);
}

bool FreeCADStyle::ownSurface(const QWidget* widget, bool inherited)
{
    // An explicit property opens a root; otherwise inherit.
    const QVariant seed = widget->property(transparencyOverrideProperty);
    return seed.isValid() ? seed.toBool() : inherited;
}

bool FreeCADStyle::canInheritTransparency(const QWidget* widget)
{
    // Popups, menus, tooltips and dialogs are separate top-level surfaces over the desktop,
    // not surfaces over the 3D view — they do not inherit through the QObject parent/child
    // link used purely for lifetime management. An explicit override still applies regardless,
    // via ownSurface().
    return widget != nullptr && !widget->isWindow();
}

void FreeCADStyle::updateTransparency(QWidget* widget, bool inherited)
{
    if (widget == nullptr) {
        return;
    }

    tagWidgetTransparency(widget, ownSurface(widget, inherited));

    const bool below = transparencyBelow(widget);

    forEachChildWidget(widget, [this, below](QWidget* childWidget) {
        updateTransparency(childWidget, canInheritTransparency(childWidget) && below);
    });
}









bool FreeCADStyle::isTransparent(const QWidget* widget)
{
    return widget != nullptr && widget->property(transparencyProperty).toBool();
}

void FreeCADStyle::notifyStyleChange(QWidget* widget)
{
    QEvent styleChange(QEvent::StyleChange);
    QCoreApplication::sendEvent(widget, &styleChange);
}

FreeCADStyle::BoxGeometryDefinition FreeCADStyle::resolveBoxGeometry(const StyleContext& context) const
{
    const uint32_t bin = overrideSetOf(context.widget);
    const uint64_t key = context.cacheKey();

    if (const auto* cached = boxGeometryCache.find(bin, key)) {
        return *cached;
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

    boxGeometryCache.store(bin, key, result);
    return result;
}

FreeCADStyle::BoxStyleDefinition FreeCADStyle::resolveBoxStyle(const StyleContext& context) const
{
    const uint32_t bin = overrideSetOf(context.widget);
    const uint64_t key = context.cacheKey();

    if (const auto* cached = boxStyleCache.find(bin, key)) {
        return *cached;
    }

    const auto position = static_cast<Position>(context.variant.get(VariantSlot::Position));
    const StyleContext northContext = withNorthPosition(context);

    BoxStyleDefinition result;

    // Geometric tokens are stated once at the canonical North and rotated to where the
    // component actually attaches. rotated(x, North) is the identity, so this is safe to run
    // for every component, not only the ones that move.
    if (const auto background = resolve(northContext, StyleProperty::Background)) {
        result.background = rotated(Base::convertTo<QBrush>(*background), position);
    }
    if (const auto borderRadius = resolve<Corners>(northContext, StyleProperty::BorderRadius)) {
        result.borderRadius = rotated(Base::convertTo<CornerRadii>(*borderRadius), position);
    }
    if (const auto borderThickness = resolve<Insets>(northContext, StyleProperty::BorderThickness)) {
        result.borderThickness = rotated(Base::convertTo<QMarginsF>(*borderThickness), position);
    }
    if (const auto borderColors = resolve<BorderColors>(context, StyleProperty::BorderColor)) {
        result.borderColor = Base::convertTo<BorderColorsPerSide>(*borderColors);
    }

    if (const auto innerShadow
        = resolve<StyleParameters::InnerShadow>(context, StyleProperty::InnerShadow)) {
        result.innerShadow = Base::convertTo<InnerShadow>(*innerShadow);
    }

    // BackgroundEffect follows Background, so it resolves from North too.
    if (const auto effect = resolve<ColorEffect>(northContext, StyleProperty::BackgroundEffect)) {
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

    boxStyleCache.store(bin, key, result);
    return result;
}

namespace
{

// QIcon::Mode from option state; AutoRaise plus hover is what a flat button reports.
bool isFlat(const QWidget* widget, const QStyleOption* option)
{
    if (const auto* buttonOption = qstyleoption_cast<const QStyleOptionButton*>(option)) {
        if (buttonOption->features & QStyleOptionButton::Flat) {
            return true;
        }
    }
    if (const auto* button = qobject_cast<const QPushButton*>(widget)) {
        return button->isFlat();
    }
    if (const auto* toolButton = qobject_cast<const QToolButton*>(widget)) {
        return toolButton->autoRaise();
    }
    return widget && widget->property("flat").toBool();
}

/**
 * @brief Returns the orientation of the nearest ancestor QToolBar, or nullopt if the widget is
 *        not inside a toolbar.
 */
std::optional<Qt::Orientation> toolbarOrientationOf(const QWidget* widget)
{
    const QWidget* ancestor = widget ? widget->parentWidget() : nullptr;

    if (const auto* toolbar = qobject_cast<const QToolBar*>(ancestor)) {
        return toolbar->orientation();
    }

    return std::nullopt;
}

// The chevron is drawn slightly softer than body text.
constexpr int arrowAlpha = 160;

QIcon::Mode iconModeOf(const QStyleOption* option)
{
    if (!(option->state & QStyle::State_Enabled)) {
        return QIcon::Disabled;
    }
    if ((option->state & QStyle::State_MouseOver) && (option->state & QStyle::State_AutoRaise)) {
        return QIcon::Active;
    }
    return QIcon::Normal;
}

QIcon::State iconStateOf(const QStyleOption* option)
{
    return (option->state & QStyle::State_On) ? QIcon::On : QIcon::Off;
}

}  // namespace

QColor FreeCADStyle::resolveIconColor(const StyleContext& context, const QPalette& palette) const
{
    if (const auto color = resolve<Base::Color>(context, StyleProperty::IconColor)) {
        return color->asValue<QColor>();
    }
    if (const auto color = resolve<Base::Color>(context, StyleProperty::TextColor)) {
        return color->asValue<QColor>();
    }
    return palette.buttonText().color();
}

QPixmap FreeCADStyle::renderStyledIcon(
    QPainter* painter,
    const QIcon& icon,
    const QSize& maxSize,
    QIcon::Mode mode,
    QIcon::State state,
    const StyleContext& context,
    const QPalette& palette
) const
{
    return IconManager::instance().render(
        icon,
        {
            .size = maxSize,
            .dpr = painter->device()->devicePixelRatio(),
            .color = resolveIconColor(context, palette),
            .mode = mode,
            .state = state,
        }
    );
}

QPixmap FreeCADStyle::renderStyledIcon(
    QPainter* painter,
    const QIcon& icon,
    const QSize& maxSize,
    const QStyleOption* option,
    const StyleContext& context
) const
{
    return renderStyledIcon(
        painter,
        icon,
        maxSize,
        iconModeOf(option),
        iconStateOf(option),
        context,
        option->palette
    );
}

void FreeCADStyle::drawChevronArrow(
    QPainter* painter,
    const QRect& rect,
    Qt::ArrowType direction,
    const QColor& color
) const
{
    constexpr qreal arrowMaxSize = 6.0;
    constexpr qreal arrowPenWidthRatio = 0.18;
    constexpr qreal arrowMinPenWidth = 1.0;
    // For down/up: width=side, height=side*ratio. For left/right: proportions swap.
    constexpr qreal arrowHeightRatio = 0.5;

    const qreal side = qMin(static_cast<qreal>(qMin(rect.width(), rect.height())), arrowMaxSize);
    const qreal penWidth = qMax(arrowMinPenWidth, side * arrowPenWidthRatio);
    const qreal halfW = side / 2.0;
    const qreal halfH = side * arrowHeightRatio / 2.0;

    // Use QRectF centre to avoid the 0.5 px truncation of QRect::center().
    const QPointF center = QRectF(rect).center();

    // Each direction is computed directly — no painter rotation, no rounding accumulation.
    QPainterPath chevronPath;
    // clang-format off
    switch (direction) {
        case Qt::UpArrow:
            chevronPath.moveTo(center.x() - halfW, center.y() + halfH);
            chevronPath.lineTo(center.x(),         center.y() - halfH);
            chevronPath.lineTo(center.x() + halfW, center.y() + halfH);
            break;
        case Qt::LeftArrow:
            chevronPath.moveTo(center.x() + halfH, center.y() - halfW);
            chevronPath.lineTo(center.x() - halfH, center.y());
            chevronPath.lineTo(center.x() + halfH, center.y() + halfW);
            break;
        case Qt::RightArrow:
            chevronPath.moveTo(center.x() - halfH, center.y() - halfW);
            chevronPath.lineTo(center.x() + halfH, center.y());
            chevronPath.lineTo(center.x() - halfH, center.y() + halfW);
            break;
        default:  // Qt::DownArrow
            chevronPath.moveTo(center.x() - halfW, center.y() - halfH);
            chevronPath.lineTo(center.x(),         center.y() + halfH);
            chevronPath.lineTo(center.x() + halfW, center.y() - halfH);
            break;
    }
    // clang-format on

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    painter->strokePath(chevronPath, QPen(color, penWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->restore();
}

QSize FreeCADStyle::toolButtonSizeFromContents(
    const QStyleOptionToolButton* option,
    const QSize& size,
    const QWidget* widget
) const
{
    BoxGeometryDefinition geometry = resolveBoxGeometry(contextOf(widget, option));

    const int menuWidth = proxy()->pixelMetric(PM_MenuButtonIndicator, option, widget);
    const bool hasMenu = option->features & QStyleOptionToolButton::MenuButtonPopup;

    const std::optional<Qt::Orientation> toolbarOrientation = toolbarOrientationOf(widget);

    // QToolButton::sizeHint() adds PM_MenuButtonIndicator to the width for
    // MenuButtonPopup buttons before calling sizeFromContents, regardless of toolbar
    // orientation. For vertical toolbars the strip goes below the icon, so we move
    // the indicator contribution from width to height.
    QSize contentSize = size;
    if (hasMenu && toolbarOrientation == Qt::Vertical) {
        contentSize.rwidth() -= menuWidth;
        contentSize.rheight() += menuWidth;
    }

    // For horizontal menu-strip buttons, minWidth expresses the button body minimum;
    // add the strip width so constrain sees the correct total minimum.
    const bool hasHorizontalMenu = hasMenu && toolbarOrientation != Qt::Vertical;
    if (hasHorizontalMenu && geometry.minWidth) {
        geometry.minWidth = *geometry.minWidth + menuWidth;
    }

    // For instant/delayed popup buttons the arrow indicator is drawn inline within the
    // button body (no separate strip). Reserve width for it so the icon does not overlap.
    const bool hasInlineIndicator = (option->features & QStyleOptionToolButton::HasMenu) && !hasMenu;
    if (hasInlineIndicator) {
        contentSize.rwidth() += menuWidth;
        if (geometry.minWidth) {
            geometry.minWidth = *geometry.minWidth + menuWidth;
        }
    }

    return geometry.sizeFromContents(contentSize);
}

QRect FreeCADStyle::toolButtonSubControlRect(
    const QStyleOptionToolButton* option,
    SubControl subControl,
    const QWidget* widget
) const
{
    const QRect rect = option->rect;

    if (option->features & QStyleOptionToolButton::MenuButtonPopup) {
        const int menuWidth = proxy()->pixelMetric(PM_MenuButtonIndicator, option, widget);
        const bool isVertical = toolbarOrientationOf(widget) == Qt::Vertical;

        switch (subControl) {
            case SC_ToolButton:
                if (isVertical) {
                    return {rect.left(), rect.top(), rect.width(), rect.height() - menuWidth};
                }
                return {rect.left(), rect.top(), rect.width() - menuWidth, rect.height()};
            case SC_ToolButtonMenu:
                if (isVertical) {
                    return {rect.left(), rect.bottom() - menuWidth + 1, rect.width(), menuWidth};
                }
                return {rect.right() - menuWidth + 1, rect.top(), menuWidth, rect.height()};
            default:
                return QProxyStyle::subControlRect(CC_ToolButton, option, subControl, widget);
        }
    }

    return QProxyStyle::subControlRect(CC_ToolButton, option, subControl, widget);
}

void FreeCADStyle::drawToolButton(
    const QStyleOptionToolButton* option,
    QPainter* painter,
    const QWidget* widget
) const
{
    const bool hasMenuButton = option->features & QStyleOptionToolButton::MenuButtonPopup;
    const bool isVertical = toolbarOrientationOf(widget) == Qt::Vertical;

    // The main half keeps the border on the join; the strip drops it, so one rule separates
    // the two. In a vertical toolbar the strip sits below the body rather than beside it.
    const SeamEdge mainSeam = !hasMenuButton ? SeamEdge::None
        : isVertical                         ? SeamEdge::Bottom
                                             : SeamEdge::Right;
    const SeamEdge menuSeam = isVertical ? SeamEdge::Top : SeamEdge::Left;

    // Draw the main button area. Strip State_Sunken when only the menu strip is the
    // active subcontrol so that clicking the dropdown does not depress the main area.
    // When the menu strip is being pressed Qt may clear State_MouseOver from the overall
    // state. Use activeSubControls instead: it is non-zero whenever the mouse is over
    // any part of the split button, so the main half keeps its hover look.
    const QRect mainRect = proxy()->subControlRect(CC_ToolButton, option, SC_ToolButton, widget);
    QStyleOptionToolButton mainOption = *option;
    if (!(option->activeSubControls & SC_ToolButton)) {
        mainOption.state &= ~State_Sunken;
    }
    if (hasMenuButton && option->activeSubControls) {
        mainOption.state |= State_MouseOver;
    }
    drawBoxBackground(
        painter,
        mainRect,
        seamedBoxStyle(contextOf(widget, &mainOption), mainSeam, SeamBorder::Keep)
    );

    if (hasMenuButton) {
        // Draw the dropdown arrow strip with its own interactive state.
        const QRect menuRect
            = proxy()->subControlRect(CC_ToolButton, option, SC_ToolButtonMenu, widget);
        QStyleOptionToolButton menuOption = *option;
        if (!(option->activeSubControls & SC_ToolButtonMenu)) {
            menuOption.state &= ~State_Sunken;
        }
        drawBoxBackground(
            painter,
            menuRect,
            seamedBoxStyle(contextOf(widget, &menuOption), menuSeam, SeamBorder::Drop)
        );

        QStyleOptionToolButton arrowOption = *option;
        arrowOption.rect = menuRect;
        proxy()->drawPrimitive(PE_IndicatorArrowDown, &arrowOption, painter, widget);
    }
    QRect labelRect = mainRect;

    if (option->features & QStyleOptionToolButton::HasMenu && !hasMenuButton) {
        // Instant/delayed popup: draw a small arrow indicator on the right, vertically
        // centered within the content area (respecting margin and padding).
        const BoxGeometryDefinition geometry = resolveBoxGeometry(contextOf(widget, option));
        const QRect contentArea = geometry.contentRect(option->rect);
        const int arrowSize = proxy()->pixelMetric(PM_MenuButtonIndicator, option, widget);

        QStyleOptionToolButton arrowOption = *option;
        arrowOption.rect = QRect(
            contentArea.right() - arrowSize + 1,
            contentArea.top() + ((contentArea.height() - arrowSize) / 2),
            arrowSize,
            arrowSize
        );
        proxy()->drawPrimitive(PE_IndicatorArrowDown, &arrowOption, painter, widget);

        // Narrow the label rect by arrowSize only. The sizeFromContents adds
        // arrowSize + iconSpacing to the button width, so the content rect inside
        // CE_ToolButtonLabel ends up iconSpacing wider than the icon — the icon
        // centers within it, placing iconSpacing/2 of buffer between the icon and
        // the arrow rect. Do not subtract iconSpacing here as well, which would
        // cancel its effect entirely.
        labelRect.setRight(mainRect.right() - arrowSize);
    }

    // Draw label (icon + text). Restrict to SC_ToolButton so it does not bleed into
    // the menu strip. Also clear SC_ToolButtonMenu so QCommonStyle::CE_ToolButtonLabel
    // does not draw its own menu indicator arrow on top of ours.
    QStyleOptionToolButton labelOption = *option;
    labelOption.rect = labelRect;
    labelOption.subControls &= ~SC_ToolButtonMenu;
    proxy()->drawControl(CE_ToolButtonLabel, &labelOption, painter, widget);
}

void FreeCADStyle::drawToolButtonLabel(
    QPainter* painter,
    const QStyleOptionToolButton* option,
    const QWidget* widget
) const
{
    const StyleContext context = contextOf(widget, option);
    const BoxGeometryDefinition geometry = resolveBoxGeometry(context);
    const QRect contentRect = geometry.contentRect(option->rect);

    const Qt::ToolButtonStyle tbStyle = option->toolButtonStyle;
    const bool hasIconOrArrow = !option->icon.isNull() || option->arrowType != Qt::NoArrow;
    const bool hasText = hasIconOrArrow && !option->text.isEmpty()
        && (tbStyle == Qt::ToolButtonTextBesideIcon || tbStyle == Qt::ToolButtonTextUnderIcon);

    if (!hasText) {
        // Icon-only with a real (non-arrow) icon: draw it ourselves so the
        // token-based icon color is applied. Text-only, arrow-only, and
        // ToolButtonTextOnly always delegate — we have nothing to colour there.
        const bool hasRealIcon = !option->icon.isNull() && option->arrowType == Qt::NoArrow
            && tbStyle != Qt::ToolButtonTextOnly;
        if (!hasRealIcon) {
            QProxyStyle::drawControl(CE_ToolButtonLabel, option, painter, widget);
            return;
        }

        // Prefer the padded content rect, but where the padding leaves too little room for
        // the icon on an axis, ignore the padding there (grow back toward the full button
        // rect) so a short button shows the icon at size instead of squashing it.
        QRect iconRect = contentRect;
        if (iconRect.width() < option->iconSize.width()) {
            iconRect.setLeft(option->rect.left());
            iconRect.setWidth(option->rect.width());
        }
        if (iconRect.height() < option->iconSize.height()) {
            iconRect.setTop(option->rect.top());
            iconRect.setHeight(option->rect.height());
        }
        iconRect = applyButtonShift(iconRect, option, widget);

        // Shrink to the available room preserving aspect ratio; never distort or upscale.
        QSize iconSize = option->iconSize;
        if (iconSize.width() > iconRect.width() || iconSize.height() > iconRect.height()) {
            iconSize.scale(iconRect.size(), Qt::KeepAspectRatio);
        }

        const QPixmap pixmap = renderStyledIcon(painter, option->icon, iconSize, option, context);
        if (!pixmap.isNull()) {
            proxy()->drawItemPixmap(painter, iconRect, Qt::AlignCenter, pixmap);
        }
        return;
    }

    const int iconSpacing = geometry.iconSpacing;

    const QRect shiftedContentRect = applyButtonShift(contentRect, option, widget);

    const bool hasArrow = option->arrowType != Qt::NoArrow;

    QPixmap pixmap;
    QSize pixmapSize = option->iconSize;
    if (!hasArrow && !option->icon.isNull()) {
        pixmap = renderStyledIcon(
            painter,
            option->icon,
            shiftedContentRect.size().boundedTo(option->iconSize),
            option,
            context
        );
        pixmapSize = pixmap.size() / painter->device()->devicePixelRatio();
    }

    const auto drawArrowInRect = [&](const QRect& arrowRect) {
        QStyleOption arrowOpt(*option);
        arrowOpt.rect = arrowRect;
        PrimitiveElement primitive = PE_IndicatorArrowDown;
        switch (option->arrowType) {
            case Qt::LeftArrow:
                primitive = PE_IndicatorArrowLeft;
                break;
            case Qt::RightArrow:
                primitive = PE_IndicatorArrowRight;
                break;
            case Qt::UpArrow:
                primitive = PE_IndicatorArrowUp;
                break;
            default:
                break;
        }
        proxy()->drawPrimitive(primitive, &arrowOpt, painter, widget);
    };

    const int textFlags = mnemonicTextFlags(option, widget);

    painter->save();
    painter->setFont(option->font);

    if (tbStyle == Qt::ToolButtonTextBesideIcon) {
        const QRect iconRect(
            shiftedContentRect.left(),
            shiftedContentRect.top() + (shiftedContentRect.height() - pixmapSize.height()) / 2,
            pixmapSize.width(),
            pixmapSize.height()
        );
        const QRect textRect = shiftedContentRect.adjusted(pixmapSize.width() + iconSpacing, 0, 0, 0);

        if (hasArrow) {
            drawArrowInRect(iconRect);
        }
        else {
            proxy()->drawItemPixmap(painter, iconRect, Qt::AlignCenter, pixmap);
        }
        proxy()->drawItemText(
            painter,
            QStyle::visualRect(option->direction, shiftedContentRect, textRect),
            textFlags | Qt::AlignLeft | Qt::AlignVCenter,
            option->palette,
            option->state & State_Enabled,
            option->text,
            QPalette::ButtonText
        );
    }
    else {
        // Qt::ToolButtonTextUnderIcon
        const int fontHeight = option->fontMetrics.height();
        const QRect iconRect = shiftedContentRect.adjusted(0, 0, 0, -(fontHeight + iconSpacing));
        const QRect textRect(
            shiftedContentRect.left(),
            iconRect.bottom() + 1 + iconSpacing,
            shiftedContentRect.width(),
            fontHeight
        );

        if (hasArrow) {
            drawArrowInRect(iconRect);
        }
        else {
            proxy()->drawItemPixmap(painter, iconRect, Qt::AlignCenter, pixmap);
        }
        proxy()->drawItemText(
            painter,
            QStyle::visualRect(option->direction, shiftedContentRect, textRect),
            textFlags | Qt::AlignHCenter | Qt::AlignTop,
            option->palette,
            option->state & State_Enabled,
            option->text,
            QPalette::ButtonText
        );
    }

    painter->restore();
}

/*static*/ Position FreeCADStyle::toolbarPositionOf(const QToolBar* toolbar)
{
    const QWidget* ancestor = toolbar ? toolbar->parentWidget() : nullptr;
    while (ancestor) {
        if (const auto* mainWindow = qobject_cast<const QMainWindow*>(ancestor)) {
            switch (mainWindow->toolBarArea(const_cast<QToolBar*>(toolbar))) {
                case Qt::TopToolBarArea:
                    return Position::North;
                case Qt::BottomToolBarArea:
                    return Position::South;
                case Qt::RightToolBarArea:
                    return Position::East;
                case Qt::LeftToolBarArea:
                    return Position::West;
                default:
                    return Position::North;
            }
        }
        ancestor = ancestor->parentWidget();
    }
    return Position::North;
}

// ─── Context building ────────────────────────────────────────────────────────

// The entry @p widget's dropdown currently holds, or nothing when nothing drives its selection.
//
// A combo box answers for its own popup and answers live, so its current index is read through
// the tag rather than copied. Any other dropdown states the row once, when it is adopted.
void FreeCADStyle::drawSeparatorLine(
    QPainter* painter,
    const QRect& rect,
    Qt::Orientation orientation
) const
{
    int thickness = 1;
    if (const auto numeric = resolve<Numeric>("SeparatorThickness")) {
        thickness = static_cast<int>(*numeric);
    }
    if (const auto color = resolve<Base::Color>("SeparatorColor")) {
        const QRect lineRect = orientation == Qt::Horizontal
            ? QRect(rect.left(), rect.center().y() - (thickness / 2), rect.width(), thickness)
            : QRect(rect.center().x() - (thickness / 2), rect.top(), thickness, rect.height());
        painter->fillRect(lineRect, color->asValue<QColor>());
    }
}

std::optional<int> FreeCADStyle::resolvePixelMetric(
    PixelMetric metric,
    const QStyleOption* option,
    const QWidget* widget
) const
{
    using enum StyleProperty;

    const StyleContext context = contextOf(widget, option);

    // Qt asks for whichever of these suits the widget it is sizing an icon for; both answer
    // from the same token, so a component states its icon size once.
    static const std::map<PixelMetric, StyleProperty> metrics = {
        {PM_SmallIconSize, IconSize},
        {PM_ButtonIconSize, IconSize},
    };

    if (const auto found = metrics.find(metric); found != metrics.end()) {
        return resolve<int>(context, found->second);
    }

    static const std::map<PixelMetric, std::pair<StyleComponentElement, StyleProperty>> elementMetrics = {
        {PM_MenuButtonIndicator, {StyleComponentElement::Menu, Width}},
        {PM_ToolBarItemMargin, {StyleComponentElement::Item, Margin}},
        {PM_ToolBarItemSpacing, {StyleComponentElement::Item, Spacing}},
        {PM_ToolBarFrameWidth, {StyleComponentElement::Root, FrameWidth}},
        {PM_ToolBarIconSize, {StyleComponentElement::Root, IconSize}},
    };

    if (const auto found = elementMetrics.find(metric); found != elementMetrics.end()) {
        const auto& [element, property] = found->second;
        return resolve<int>(contextOf(widget, option, element), property);
    }

    return {};
}

int FreeCADStyle::pixelMetric(PixelMetric metric, const QStyleOption* option, const QWidget* widget) const
{
    if (const auto value = resolvePixelMetric(metric, option, widget)) {
        return *value;
    }
    return QProxyStyle::pixelMetric(metric, option, widget);
}

QSize FreeCADStyle::sizeFromContents(
    ContentsType type,
    const QStyleOption* option,
    const QSize& size,
    const QWidget* widget
) const
{
    if (type == CT_PushButton) {
        const auto* btnOption = qstyleoption_cast<const QStyleOptionButton*>(option);
        const BoxGeometryDefinition geometry = resolveBoxGeometry(contextOf(widget, option));
        QSize contentSize = size;
        if (btnOption && !btnOption->icon.isNull() && !btnOption->text.isEmpty()) {
            contentSize.rwidth() += geometry.iconGapDelta();
        }
        return geometry.sizeFromContents(contentSize);
    }

    if (type == CT_ToolButton) {
        if (const auto* tbOption = qstyleoption_cast<const QStyleOptionToolButton*>(option)) {
            return toolButtonSizeFromContents(tbOption, size, widget);
        }
    }

    return QProxyStyle::sizeFromContents(type, option, size, widget);
}

QRect FreeCADStyle::subControlRect(
    ComplexControl complexControl,
    const QStyleOptionComplex* option,
    SubControl subControl,
    const QWidget* widget
) const
{
    if (complexControl == CC_ToolButton) {
        if (const auto* opt = qstyleoption_cast<const QStyleOptionToolButton*>(option)) {
            return toolButtonSubControlRect(opt, subControl, widget);
        }
    }

    return QProxyStyle::subControlRect(complexControl, option, subControl, widget);
}

void FreeCADStyle::drawComplexControl(
    ComplexControl control,
    const QStyleOptionComplex* option,
    QPainter* painter,
    const QWidget* widget
) const
{
    if (control == CC_ToolButton) {
        if (const auto* opt = qstyleoption_cast<const QStyleOptionToolButton*>(option)) {
            drawToolButton(opt, painter, widget);
            return;
        }
    }

    QProxyStyle::drawComplexControl(control, option, painter, widget);
}

void FreeCADStyle::drawPrimitive(
    PrimitiveElement element,
    const QStyleOption* option,
    QPainter* painter,
    const QWidget* widget
) const
{
    if (element == PE_PanelButtonCommand) {
        drawComponent(painter, option->rect, widget, option);
        return;
    }

    if (element == PE_IndicatorToolBarSeparator) {
        // In a horizontal toolbar the buttons are side by side, so the separator is a vertical
        // line; in a vertical toolbar it is a horizontal one.
        const bool toolbarIsHorizontal = option->state & QStyle::State_Horizontal;
        drawSeparatorLine(
            painter,
            toolbarIsHorizontal ? option->rect.adjusted(0, 4, 0, -4)
                                : option->rect.adjusted(4, 0, -4, 0),
            toolbarIsHorizontal ? Qt::Vertical : Qt::Horizontal
        );
        return;
    }

    if (element == PE_IndicatorArrowDown || element == PE_IndicatorArrowUp
        || element == PE_IndicatorArrowLeft || element == PE_IndicatorArrowRight) {
        // clang-format off
        const Qt::ArrowType direction = [&] {
            switch (element) {
                case PE_IndicatorArrowUp:    return Qt::UpArrow;
                case PE_IndicatorArrowLeft:  return Qt::LeftArrow;
                case PE_IndicatorArrowRight: return Qt::RightArrow;
                default:                     return Qt::DownArrow;
            }
        }();
        // clang-format on
        const StyleContext context = contextOf(widget, option);
        QColor arrowColor = resolveIconColor(context, option->palette);
        arrowColor.setAlpha(arrowAlpha);
        drawChevronArrow(painter, option->rect, direction, arrowColor);
        return;
    }

    QProxyStyle::drawPrimitive(element, option, painter, widget);
}

void FreeCADStyle::drawControl(
    ControlElement element,
    const QStyleOption* option,
    QPainter* painter,
    const QWidget* widget
) const
{
    if (element == CE_ShapedFrame) {
        if (const auto* frameOption = qstyleoption_cast<const QStyleOptionFrame*>(option)) {
            const QFrame::Shape shape = frameOption->frameShape;

            // A rule drawn as a frame is the same rule the style draws anywhere else, and it
            // is stated once, in the separator tokens.
            if (shape == QFrame::HLine || shape == QFrame::VLine) {
                drawSeparatorLine(
                    painter,
                    option->rect,
                    shape == QFrame::HLine ? Qt::Horizontal : Qt::Vertical
                );
                return;
            }
        }
    }

    if (element == CE_ToolBar) {
        const StyleContext context = contextOf(widget, option);
        drawBoxBackground(painter, option->rect, resolveBoxStyle(context));
        return;
    }

    if (element == CE_PushButton || element == CE_PushButtonBevel) {
        if (auto btnOpt = qstyleoption_cast<const QStyleOptionButton*>(option)) {
            // Flatness is a token variant here, so Qt must not also act on the feature flag
            // and skip the panel this style is about to paint.
            QStyleOptionButton modified = *btnOpt;
            modified.features &= ~QStyleOptionButton::Flat;
            QProxyStyle::drawControl(element, &modified, painter, widget);
            return;
        }
    }

    if (element == CE_PushButtonLabel) {
        if (const auto* btnOption = qstyleoption_cast<const QStyleOptionButton*>(option)) {
            drawPushButtonLabel(painter, btnOption, widget);
            return;
        }
    }

    if (element == CE_ToolButtonLabel) {
        if (const auto* tbOption = qstyleoption_cast<const QStyleOptionToolButton*>(option)) {
            drawToolButtonLabel(painter, tbOption, widget);
            return;
        }
    }

    QProxyStyle::drawControl(element, option, painter, widget);
}

void FreeCADStyle::drawPushButtonLabel(
    QPainter* painter,
    const QStyleOptionButton* option,
    const QWidget* widget
) const
{
    const StyleContext context = contextOf(widget, option);
    const BoxGeometryDefinition geometry = resolveBoxGeometry(context);

    // option->rect at this point is SE_PushButtonContents from Fusion (inset by its own frame
    // width), which doesn't reflect our token-based padding. Use widget->rect() — the true
    // button rect — as the base, then apply token padding to derive the content area.
    // This is consistent with CT_PushButton in sizeFromContents, which also computes the total
    // size as content + token padding (not Fusion's frame).
    const QRect buttonRect = widget ? widget->rect() : option->rect;
    const QRect contentRect = geometry.contentRect(buttonRect);

    // For icon-only or text-only, delegate to parent with the token-padded content rect.
    // The parent centers the content within this rect; press-state shift is left to the parent.
    if (option->icon.isNull() || option->text.isEmpty()) {
        QStyleOptionButton adjustedOption = *option;
        adjustedOption.rect = contentRect;
        QProxyStyle::drawControl(CE_PushButtonLabel, &adjustedOption, painter, widget);
        return;
    }

    // Icon + text: custom layout with token icon spacing.
    const QRect shiftedContentRect = applyButtonShift(contentRect, option, widget);
    const int iconSpacing = geometry.iconSpacing;

    const QPixmap pixmap = renderStyledIcon(
        painter,
        option->icon,
        shiftedContentRect.size().boundedTo(option->iconSize),
        option,
        context
    );
    const QSize pixmapSize = pixmap.size() / painter->device()->devicePixelRatio();

    // Center the icon+text group horizontally in the content rect.
    const int textWidth = option->fontMetrics.horizontalAdvance(option->text);
    const int groupWidth = pixmapSize.width() + iconSpacing + textWidth;
    const int groupLeft = shiftedContentRect.left() + (shiftedContentRect.width() - groupWidth) / 2;

    const int textLeft = groupLeft + pixmapSize.width() + iconSpacing;
    const QRect iconRect(
        groupLeft,
        shiftedContentRect.top() + (shiftedContentRect.height() - pixmapSize.height()) / 2,
        pixmapSize.width(),
        pixmapSize.height()
    );
    const QRect textRect(
        textLeft,
        shiftedContentRect.top(),
        shiftedContentRect.right() - textLeft,
        shiftedContentRect.height()
    );

    const int textFlags = mnemonicTextFlags(option, widget) | Qt::AlignVCenter | Qt::AlignLeft;

    painter->save();
    proxy()->drawItemPixmap(painter, iconRect, Qt::AlignCenter, pixmap);
    proxy()->drawItemText(
        painter,
        QStyle::visualRect(option->direction, shiftedContentRect, textRect),
        textFlags,
        option->palette,
        option->state & State_Enabled,
        option->text,
        QPalette::ButtonText
    );
    painter->restore();
}

int FreeCADStyle::mnemonicTextFlags(const QStyleOption* option, const QWidget* widget) const
{
    int flags = Qt::TextShowMnemonic;
    if (!proxy()->styleHint(SH_UnderlineShortcut, option, widget)) {
        flags |= Qt::TextHideMnemonic;
    }
    return flags;
}

QRect FreeCADStyle::applyButtonShift(
    const QRect& rect,
    const QStyleOption* option,
    const QWidget* widget
) const
{
    if (!(option->state & (State_Sunken | State_On))) {
        return rect;
    }
    QRect shifted = rect;
    shifted.translate(
        proxy()->pixelMetric(PM_ButtonShiftHorizontal, option, widget),
        proxy()->pixelMetric(PM_ButtonShiftVertical, option, widget)
    );
    return shifted;
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

    if (qobject_cast<const QToolButton*>(widget)) {
        const bool isInToolBar = qobject_cast<const QToolBar*>(widget->parent());
        context.component = isInToolBar ? StyleComponent::ToolBarButton : StyleComponent::ToolButton;
    }
    else if (qobject_cast<const QPushButton*>(widget)) {
        context.component = StyleComponent::PushButton;
    }
    else if (const auto* toolbar = qobject_cast<const QToolBar*>(widget)) {
        context.component = StyleComponent::ToolBar;
        context.element = element;
        context.variant.set(VariantSlot::Position, toolbarPositionOf(toolbar));

        // A toolbar hosted in the status bar or as a menu bar corner widget blends into its
        // host surface, so the Transparent variant suppresses its background and border.
        for (const QObject* ancestor = widget->parent(); ancestor; ancestor = ancestor->parent()) {
            if (qobject_cast<const QStatusBar*>(ancestor) || qobject_cast<const QMenuBar*>(ancestor)) {
                context.variant.set(VariantSlot::TransparencyMode, TransparencyMode::Transparent);
                break;
            }
        }
    }

    // ButtonType, derived from the style option's features first, then from widget properties.
    const auto* buttonOption = qstyleoption_cast<const QStyleOptionButton*>(option);
    if (buttonOption && (buttonOption->features & QStyleOptionButton::DefaultButton)) {
        context.variant.set(VariantSlot::ButtonType, ButtonType::Primary);
    }
    else if (isFlat(widget, option)) {
        context.variant.set(VariantSlot::ButtonType, ButtonType::Link);
    }

    // An explicit "buttonType" property overrides the above.
    if (widget) {
        const QString buttonType = widget->property("buttonType").toString();
        if (buttonType == u"primary") {
            context.variant.set(VariantSlot::ButtonType, ButtonType::Primary);
        }
        else if (buttonType == u"link") {
            context.variant.set(VariantSlot::ButtonType, ButtonType::Link);
        }
    }

    // ControlSize, derived from the "controlSize" widget property.
    if (widget) {
        const QString sizeName = widget->property("controlSize").toString();
        if (sizeName == u"internal") {
            context.variant.set(VariantSlot::ControlSize, ControlSize::Internal);
        }
        else if (sizeName == u"small") {
            context.variant.set(VariantSlot::ControlSize, ControlSize::Small);
        }
        else if (sizeName == u"big") {
            context.variant.set(VariantSlot::ControlSize, ControlSize::Big);
        }
    }

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

        // State_Sunken means "is being pressed" for a button, but "has a sunken frame" for an
        // input widget, which sets it permanently. Only button-like components map it to
        // Pressed, so an input's Focused state is not masked by it.
        const bool isButton = context.component == StyleComponent::PushButton
            || context.component == StyleComponent::ToolButton
            || context.component == StyleComponent::ToolBarButton;
        if (isButton && (option->state & QStyle::State_Sunken)) {
            context.state |= StyleState::Pressed;
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

    if (isTransparent(widget)) {
        context.variant.set(VariantSlot::TransparencyMode, TransparencyMode::Transparent);
    }
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
    const uint32_t bin = overrideSetOf(context.widget);
    const uint64_t key = context.cacheKey(property);

    if (const auto* cached = tokenCache.find(bin, key)) {
        return *cached;
    }

    auto* manager = Application::Instance->styleParameterManager();

    const std::string propertySuffix(propertyString(property));
    std::optional<Value> result;

    for (const std::string& prefix : manager->descriptorRegistry().buildPrefixes(context)) {
        // Use the flat resolver per prefix: the prefix list IS the inheritance
        // walk, so name-based chain synthesis must not run here.
        result = manager->resolve(
            prefix + propertySuffix,
            StyleParameters::ResolveContext {.visited = {}, .overrides = bin}
        );
        if (!result) {
            continue;
        }
        if (result->holds<None>()) {
            // reset() stops the chain here, so the property resolves to nothing at all.
            result = std::nullopt;
        }
        break;
    }

    tokenCache.store(bin, key, result);
    return result;
}

StyleParameters::OverrideSet FreeCADStyle::declaredOverrides(const QWidget* widget)
{
    StyleParameters::OverrideSet declared;

    const std::string_view prefix {overridePropertyPrefix};

    for (const QByteArray& propertyName : widget->dynamicPropertyNames()) {
        const std::string_view name {
            propertyName.constData(),
            static_cast<size_t>(propertyName.size()),
        };

        if (!name.starts_with(prefix) || name.size() == prefix.size()) {
            continue;
        }

        const QVariant value = widget->property(propertyName.constData());
        if (value.typeId() != QMetaType::QString) {
            continue;
        }

        declared.emplace(std::string(name.substr(prefix.size())), value.toString().toStdString());
    }

    return declared;
}

uint32_t FreeCADStyle::computeOverrideSet(const QWidget* widget)
{
    // A polish can happen without a Gui::Application behind it: a bare QApplication::setStyle()
    // in a test, or an embedding. Fail soft rather than dereferencing a null instance.
    if (Application::Instance == nullptr) {
        return StyleParameters::OverrideRegistry::emptyId;
    }

    StyleParameters::OverrideSet merged;

    for (const QWidget* ancestor = widget; ancestor != nullptr; ancestor = ancestor->parentWidget()) {
        // try_emplace keeps what is already there, so the nearest declaration of a name wins.
        for (auto&& [name, expression] : declaredOverrides(ancestor)) {
            merged.try_emplace(name, std::move(expression));
        }

        // A window is its own surface: its own declarations count, but it does not inherit
        // from whatever it happens to be parented to for lifetime management.
        if (ancestor->isWindow()) {
            break;
        }
    }

    return Application::Instance->styleParameterManager()->overrideRegistry().intern(merged);
}

void FreeCADStyle::storeOverrideSet(QWidget* widget, uint32_t set) const
{
    // QObject::setProperty() allocates the object's dynamic-property storage on first use, even
    // when it is only clearing a property that was never set — so only write when there is
    // something to record or an existing property to clear, or every widget with no overrides
    // would pay that allocation anyway. An invalid QVariant removes the property.
    const bool hasStoredSet = widget->dynamicPropertyNames().contains(overrideSetProperty);
    if (set != StyleParameters::OverrideRegistry::emptyId || hasStoredSet) {
        widget->setProperty(
            overrideSetProperty,
            set == StyleParameters::OverrideRegistry::emptyId ? QVariant {} : QVariant {set}
        );
    }

    // Seeded rather than cleared: this function already knows the correct (widget, set) pair,
    // so priming the memo with it directly saves the very next overrideSetOf() call the
    // QObject::property() lookup the memo exists to avoid.
    overrideMemoWidget = widget;
    overrideMemoSet = set;
}

void FreeCADStyle::setStyleOverride(QWidget* widget, const QString& name, const QString& expression)
{
    if (widget == nullptr) {
        return;
    }

    const QString propertyName = QString::fromLatin1(overridePropertyPrefix) + name;
    widget->setProperty(propertyName.toLatin1().constData(), expression);

    refreshStyleOverrides(widget);
}

void FreeCADStyle::refreshStyleOverrides(QWidget* widget)
{
    if (widget == nullptr) {
        return;
    }

    // Nothing to recompute when this widget is not ours to style; the declaration stays on the
    // widget and is picked up if a FreeCADStyle polishes it later.
    if (const auto* style = qobject_cast<const FreeCADStyle*>(widget->style())) {
        style->recomputeOverrideSets(widget);
    }
}

void FreeCADStyle::recomputeOverrideSets(QWidget* widget) const
{
    storeOverrideSet(widget, computeOverrideSet(widget));

    forEachChildWidget(widget, [this](QWidget* childWidget) { recomputeOverrideSets(childWidget); });
}

void FreeCADStyle::forEachChildWidget(QWidget* widget, const std::function<void(QWidget*)>& visit)
{
    // Copy first: callers invoke handlers (StyleChange, DynamicPropertyChange, ...) synchronously
    // while recursing, and in-tree handlers may add or remove siblings — e.g. DlgToolbarsImp
    // repopulates a tree on StyleChange — which would invalidate a live QObjectList mid-iteration.
    const QObjectList children = widget->children();
    for (QObject* child : children) {
        if (auto* childWidget = qobject_cast<QWidget*>(child)) {
            visit(childWidget);
        }
    }
}

uint32_t FreeCADStyle::overrideSetOf(const QWidget* widget) const
{
    if (widget == nullptr) {
        return StyleParameters::OverrideRegistry::emptyId;
    }

    if (overrideMemoWidget.data() == widget) {
        return overrideMemoSet;
    }

    overrideMemoWidget = widget;
    overrideMemoSet = widget->property(overrideSetProperty).toUInt();

    return overrideMemoSet;
}


void FreeCADStyle::polish(QWidget* widget)
{
    QProxyStyle::polish(widget);

    if (widget == nullptr) {
        return;
    }

    // Overrides are inherited down the widget tree. computeOverrideSet() walks up from this
    // widget rather than seeding from the parent's stored id, so a widget polished before its
    // parent still lands on the right set. Only this widget is tagged here: QWidget::
    // ensurePolished() already walks descendants and polishes each in turn.
    //
    // This must run before tagWidgetTransparency() below: that call can synchronously dispatch
    // QEvent::StyleChange to this widget, and a handler reacting to it resolves tokens for this
    // same widget before polish() returns. The override id has to already be in place when that
    // happens, or the handler runs under the id this widget had before this polish.
    storeOverrideSet(widget, computeOverrideSet(widget));

    // Transparency is inherited down the widget tree. Seeding from the parent here covers
    // widgets built after their parent's subtree was propagated - lazily created editors,
    // popups and the like - without an extra event filter. Nothing here depends on this
    // widget's own override id: it resolves only against widget->parentWidget().
    const bool inherited = canInheritTransparency(widget)
        && transparencyBelow(widget->parentWidget());
    tagWidgetTransparency(widget, ownSurface(widget, inherited));
}

void FreeCADStyle::unpolish(QWidget* widget)
{
    if (widget == nullptr) {
        QProxyStyle::unpolish(widget);
        return;
    }

    // The id means nothing under another style, and leaving it behind would have that style's
    // widget resolve against a set it never asked for.
    storeOverrideSet(widget, StyleParameters::OverrideRegistry::emptyId);

    QProxyStyle::unpolish(widget);
}

StyleContext FreeCADStyle::withNorthPosition(const StyleContext& context)
{
    StyleContext north = context;
    north.variant.set(VariantSlot::Position, Position::North);
    return north;
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
    if (event->type() == ThemeReloadEvent::registeredType()) {
        clearTokenCache();

        // IsTransparent may resolve differently after a reload, so the tags the propagator
        // produced from the previous theme have to be recomputed before anything repaints.
        // Guard the list: updateTransparency() sends StyleChange synchronously and in-tree
        // handlers may destroy other top-level widgets while we are still walking them.
        QList<QPointer<QWidget>> topLevels;
        for (QWidget* topLevel : QApplication::topLevelWidgets()) {
            topLevels.append(topLevel);
        }
        for (const QPointer<QWidget>& topLevel : topLevels) {
            if (!topLevel.isNull()) {
                updateTransparency(topLevel, false);
            }
        }

        for (QWidget* widget : QApplication::allWidgets()) {
            widget->update();
        }

        // Let the handler in Application process the event as well.
        return false;
    }

    return QObject::eventFilter(obj, event);
}
