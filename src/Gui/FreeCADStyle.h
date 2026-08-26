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

#pragma once

#pragma once

#include <initializer_list>
#include <optional>
#include <string_view>
#include <unordered_map>

#include <FCGlobal.h>

#include <QBrush>
#include <QEvent>
#include <QMarginsF>
#include <QPainter>
#include <QPainterPath>
#include <QProxyStyle>
#include <QPushButton>
#include <QStyleOption>

#include "StyleParameters/Insets.h"
#include "StyleParameters/StyleContext.h"
#include "StyleParameters/Value.h"

namespace Gui
{

/**
 * @brief A QStyle whose every visual decision comes from design tokens.
 *
 * A widget is described by a StyleParameters::StyleContext: the component it is, the element
 * of that component being drawn, the variants that apply and the states it is in. contextOf()
 * derives that description from the widget and its style option, and resolve() turns it into
 * a value by assembling token names from it and walking the component's inheritance chain.
 */
class GuiExport FreeCADStyle: public QProxyStyle
{
    Q_OBJECT

public:
    FreeCADStyle()
        : QProxyStyle(QStringLiteral("Fusion"))
    {}

    /**
     * @brief Per-corner border radii, each stored as a Numeric (possibly with "%" unit).
     *
     * Percent values are resolved to absolute pixels at paint time via resolve().
     * Use setLeft/setRight/setTop/setBottom to zero out corners (e.g. for tab shapes).
     */
    struct CornerRadii
    {
        StyleParameters::Numeric topLeft = {.value = 0, .unit = "px"};
        StyleParameters::Numeric topRight = {.value = 0, .unit = "px"};
        StyleParameters::Numeric bottomRight = {.value = 0, .unit = "px"};
        StyleParameters::Numeric bottomLeft = {.value = 0, .unit = "px"};

        void setLeft(qreal left)
        {
            topLeft = bottomLeft = {.value = left, .unit = "px"};
        }

        void setRight(qreal right)
        {
            topRight = bottomRight = {.value = right, .unit = "px"};
        }

        void setTop(qreal top)
        {
            topLeft = topRight = {.value = top, .unit = "px"};
        }

        void setBottom(qreal bottom)
        {
            bottomLeft = bottomRight = {.value = bottom, .unit = "px"};
        }

        CornerRadii enlarged(qreal amount) const
        {
            CornerRadii result = *this;

            result.topLeft.value += amount;
            result.topRight.value += amount;
            result.bottomRight.value += amount;
            result.bottomLeft.value += amount;

            return result;
        }

        bool isRounded() const
        {
            return topLeft.value > 0 || topRight.value > 0 || bottomRight.value > 0
                || bottomLeft.value > 0;
        }

        /**
         * @brief Resolves any percent-unit radii to absolute pixel values.
         *
         * A "%" radius is resolved as value/100 * min(width, height), matching
         * the CSS border-radius convention for uniform corner shapes. Absolute
         * radii ("px" or dimensionless) are passed through unchanged.
         */
        CornerRadii resolve(QSizeF size) const;
    };

    /**
     * @brief Per-side border colors in CSS TRBL order.
     *
     * When all four sides are equal, isUniform() returns true and uniform()
     * gives the shared color, enabling a single-fill fast path in drawBoxBackground().
     */
    struct BorderColorsPerSide
    {
        QColor top;
        QColor right;
        QColor bottom;
        QColor left;

        bool isUniform() const
        {
            return top == right && right == bottom && bottom == left;
        }

        QColor uniform() const
        {
            return top;
        }
    };

    /**
     * @brief Describes the visual appearance of a painted background box.
     *
     * All border fields must be set together (borderColor + borderThickness)
     * for a border to be drawn; partial specification is silently ignored.
     */
    struct BoxStyleDefinition
    {
        QBrush background;
        std::optional<BorderColorsPerSide> borderColor;
        std::optional<QMarginsF> borderThickness;
        CornerRadii borderRadius;  // default: all zero (sharp corners)
    };

    /**
     * @brief Describes the spatial layout properties of a box-shaped widget.
     *
     * Resolved from Design System tokens:
     *   Padding, Height, MinWidth, MaxWidth, Width, MinHeight, MaxHeight, IconSpacing.
     *
     * Constraint semantics (applied by constrain() and sizeFromContents()):
     *   1. Fixed overrides (width, height) are applied first — pin the dimension absolutely.
     *   2. min* clamps raise the result.
     *   3. max* clamps lower the result.
     *
     * Usage guidance:
     *   - Use sizeFromContents(contentSize) for a size hint, whether the content size was
     *     computed here (PushButton, ToolButton, ItemViewItem) or delegated to the parent style
     *     first (ComboBox, LineEdit, SpinBox). The parent reserves only its own frame, so a hint
     *     that skips the padding promises room that contentRect() then takes away.
     *   - Use constrain(rect) to fit an existing rect to the same limits without adding padding.
     */
    struct BoxGeometryDefinition
    {
        QMarginsF padding;
        QMarginsF margin;

        std::optional<int> height;
        std::optional<int> minWidth;
        std::optional<int> width;      // fixed width override
        std::optional<int> maxWidth;   // maximum width clamp
        std::optional<int> minHeight;  // minimum height clamp
        std::optional<int> maxHeight;  // maximum height clamp
        /** Qt hardcodes this many pixels between an icon and its label text. */
        static constexpr int qtBuiltInIconGap = 4;

        int iconSpacing = qtBuiltInIconGap;  // fallback matches Qt's built-in

        /**
         * @brief Width delta to replace Qt's hardcoded icon–text gap with the token spacing.
         *
         * Add this to a width computed by Qt (sizeHint, sizeFromContents) when Qt has already
         * baked in qtBuiltInIconGap pixels for the icon–text gap and you want the token value.
         * Returns 0 when the token matches Qt's default, so it is always safe to apply.
         */
        [[nodiscard]] int iconGapDelta() const
        {
            return iconSpacing - qtBuiltInIconGap;
        }

        /** @brief Total horizontal padding (left + right), in pixels. */
        [[nodiscard]] int paddingH() const
        {
            return static_cast<int>(padding.left() + padding.right());
        }

        /** @brief Total vertical padding (top + bottom), in pixels. */
        [[nodiscard]] int paddingV() const
        {
            return static_cast<int>(padding.top() + padding.bottom());
        }

        /** @brief Applies all dimension constraints to a size. Fixed overrides first, then min
         * clamps up, max clamps down. */
        [[nodiscard]] QSize constrain(QSize size) const
        {
            if (width) {
                size.setWidth(*width);
            }
            if (height) {
                size.setHeight(*height);
            }
            if (minWidth) {
                size.setWidth(std::max(size.width(), *minWidth));
            }
            if (minHeight) {
                size.setHeight(std::max(size.height(), *minHeight));
            }
            if (maxWidth) {
                size.setWidth(std::min(size.width(), *maxWidth));
            }
            if (maxHeight) {
                size.setHeight(std::min(size.height(), *maxHeight));
            }
            return size;
        }

        /** @brief Applies all dimension constraints to a rect, preserving top-left position. */
        [[nodiscard]] QRect constrain(const QRect& rect) const
        {
            return {rect.topLeft(), constrain(rect.size())};
        }

        /** @brief Computes outer widget size: adds padding to content size, then constrains.
         *  Every component's size hint goes through here, including the ones that ask the parent
         *  style first: the parent reserves only its own frame, while contentRect() takes the
         *  full padding back out, so a hint that skips this step promises room it will not have.
         */
        [[nodiscard]] QSize sizeFromContents(QSize contentSize) const
        {
            return constrain(contentSize.grownBy(padding.toMargins()));
        }

        [[nodiscard]] QSize marginBox(QSize contentSize) const
        {
            return sizeFromContents(contentSize).grownBy(margin.toMargins());
        }

        /** @brief Returns @p rect inset by this geometry's padding. */
        [[nodiscard]] QRect contentRect(const QRect& rect) const
        {
            return borderRect(rect).marginsRemoved(padding.toMargins());
        }

        /** @brief Returns @p rect inset by this geometry's padding. */
        [[nodiscard]] QRect contentRect(const QRect& rect, const QSize& size) const
        {
            if (rect.width() <= size.width() && rect.height() <= size.height()) {
                return rect;
            }

            int availableWidth = rect.width() - size.width();
            int availableHeight = rect.height() - size.height();

            if (availableWidth > paddingH() && availableHeight > paddingV()) {
                return contentRect(rect);
            }

            double scaleHorizontal = qMin(static_cast<double>(availableWidth) / paddingH(), 1.0);
            double scaleVertical = qMin(static_cast<double>(availableHeight) / paddingV(), 1.0);

            return rect.adjusted(
                static_cast<int>(padding.left() * scaleHorizontal),
                static_cast<int>(padding.top() * scaleVertical),
                -static_cast<int>(padding.right() * scaleHorizontal),
                -static_cast<int>(padding.bottom() * scaleVertical)
            );
        }

        [[nodiscard]] QRect borderRect(QRect rect) const
        {
            return rect.marginsRemoved(margin.toMargins());
        }
    };

    void polish(QPalette& palette) override;

    int styleHint(
        StyleHint hint,
        const QStyleOption* option,
        const QWidget* widget,
        QStyleHintReturn* returnData
    ) const override;

    /**
     * @brief Builds a StyleContext from a widget and its current style option.
     *
     * Static so callers outside a paint path can describe a widget the same way the style
     * does. @p option may be null, in which case no state is derived.
     */
    static StyleParameters::StyleContext contextOf(
        const QWidget* widget,
        const QStyleOption* option = nullptr,
        const StyleParameters::StyleComponentElement& element =
            StyleParameters::StyleComponentElement::Root
    );

    /**
     * @brief Discards every cached token value.
     *
     * Call whenever the resolved value of a token may have changed — a theme reload, an
     * edit in the theme editor — since nothing else invalidates the cache.
     */
    void clearTokenCache();

    /// Resolves the geometry of the box @p context describes: padding, margin and the
    /// dimension constraints that decide how big it may be.
    BoxGeometryDefinition resolveBoxGeometry(const StyleParameters::StyleContext& context) const;

    /// Resolves the appearance of the box @p context describes: background, border and radii.
    BoxStyleDefinition resolveBoxStyle(const StyleParameters::StyleContext& context) const;

    /**
     * @brief Paints the box @p context describes into @p rect.
     *
     * @p rect is the outer rect, so the resolved margin is taken off before painting.
     */
    void paintBox(
        QPainter* painter,
        const QRect& rect,
        const StyleParameters::StyleContext& context
    ) const;

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

    /// Resolves a token by its full name.
    std::optional<StyleParameters::Value> resolve(std::string_view name) const;

    /**
     * @brief Resolves the first of @p names that has a value.
     *
     * Useful for resolved-with-fallback patterns, e.g.:
     * resolve({"ToolButtonSmallPadding", "ToolButtonPadding"})
     */
    std::optional<StyleParameters::Value> resolve(
        std::initializer_list<std::string_view> names
    ) const;

    /**
     * @brief Resolves @p property for @p context, caching the result.
     *
     * The context's component chain, variants and states decide which token names are tried
     * and in what order. The cache is invalidated by clearTokenCache().
     */
    std::optional<StyleParameters::Value> resolve(
        const StyleParameters::StyleContext& context,
        StyleParameters::StyleProperty property
    ) const;

    /// Typed variants of each resolve() overload, wrapping it with StyleParameters::valueAs.
    template<typename T>
    std::optional<T> resolve(std::string_view name) const
    {
        return StyleParameters::valueAs<T>(resolve(name));
    }

    template<typename T>
    std::optional<T> resolve(std::initializer_list<std::string_view> names) const
    {
        return StyleParameters::valueAs<T>(resolve(names));
    }

    template<typename T>
    std::optional<T> resolve(
        const StyleParameters::StyleContext& context,
        StyleParameters::StyleProperty property
    ) const
    {
        return StyleParameters::valueAs<T>(resolve(context, property));
    }

    /**
     * @brief Paints a background, its border ring and its rounded corners into @p rect.
     *
     * @p borderMask, when non-empty, clips the border ring, for a container whose border is
     * interrupted by something drawn on top of it.
     */
    static void drawBoxBackground(
        QPainter* painter,
        const QRect& rect,
        const BoxStyleDefinition& rule,
        const QPainterPath& borderMask = {}
    );

    /// Paints the box @p context resolves to, without taking its margin off @p rect.
    void drawComponent(
        QPainter* painter,
        const QRect& rect,
        const StyleParameters::StyleContext& context
    ) const;

    void drawComponent(
        QPainter* painter,
        const QRect& rect,
        const QWidget* widget,
        const QStyleOption* option = nullptr
    ) const;

private:
    /// Attaches @p widget to @p context, so resolution can consult what the widget declares.
    static void bindWidget(StyleParameters::StyleContext& context, const QWidget* widget);

    mutable std::unordered_map<uint64_t, std::optional<StyleParameters::Value>> tokenCache;

    // Aggregate caches for resolveBoxStyle() and resolveBoxGeometry(), keyed by the
    // context-only key (property bits left zero).
    mutable std::unordered_map<uint64_t, BoxStyleDefinition> boxStyleCache;
    mutable std::unordered_map<uint64_t, BoxGeometryDefinition> boxGeometryCache;
};

}  // namespace Gui
