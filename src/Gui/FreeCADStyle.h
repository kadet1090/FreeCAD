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

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string_view>
#include <unordered_map>

#include <FCGlobal.h>

#include <QBrush>
#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QEvent>
#include <QMarginsF>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QProxyStyle>
#include <QPushButton>
#include <QStyleOption>

#include "StyleParameters/Insets.h"
#include "StyleParameters/StyleContext.h"
#include "StyleParameters/StyleOverrides.h"
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
     * @brief Describes an inward shadow drawn on top of a box background.
     */
    struct InnerShadow
    {
        QColor color;
        qreal x = 0;
        qreal y = 0;
        qreal blur = 0;
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
        std::optional<InnerShadow> innerShadow;
    };

    /// The edge on which a box meets the half of a split control next to it.
    enum class SeamEdge : std::uint8_t
    {
        None,
        Left,
        Right,
        Top,
        Bottom,
    };

    /// Whether a box keeps its border on the seam, or leaves it to the half next to it.
    enum class SeamBorder : std::uint8_t
    {
        Keep,
        Drop,
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

    int pixelMetric(PixelMetric metric, const QStyleOption* option, const QWidget* widget)
        const override;

    QSize sizeFromContents(
        ContentsType type,
        const QStyleOption* option,
        const QSize& size,
        const QWidget* widget
    ) const override;

    void drawPrimitive(
        PrimitiveElement element,
        const QStyleOption* option,
        QPainter* painter,
        const QWidget* widget
    ) const override;

    void drawControl(
        ControlElement element,
        const QStyleOption* option,
        QPainter* painter,
        const QWidget* widget
    ) const override;

    void drawComplexControl(
        ComplexControl control,
        const QStyleOptionComplex* option,
        QPainter* painter,
        const QWidget* widget
    ) const override;

    QRect subControlRect(
        ComplexControl complexControl,
        const QStyleOptionComplex* option,
        SubControl subControl,
        const QWidget* widget
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

    void polish(QWidget* widget) override;
    void unpolish(QWidget* widget) override;

    /**
     * @brief Declares a style override on @p widget and re-derives it and its descendants.
     *
     * @p name is a token name without the property prefix, e.g. "CurrentPaneBackground";
     * @p expression is written in the same language the theme files use. The override applies
     * to @p widget and everything below it, and a nearer declaration of the same name wins.
     *
     * Setting the dynamic property directly works too, but only takes effect the next time the
     * widget is polished; use this, or refreshStyleOverrides(), for a change made afterwards.
     */
    static void setStyleOverride(QWidget* widget, const QString& name, const QString& expression);

    /// Re-derives the override set of @p widget and its descendants after a declaration changed.
    static void refreshStyleOverrides(QWidget* widget);

    // clang-format off
    /// Prefix of a property declaring an override, e.g. "fcStyleCurrentPaneBackground"
    /// overrides the CurrentPaneBackground token.
    static constexpr const char* overridePropertyPrefix = "fcStyle";

    /// Where the resolved override-set id is cached on the widget. Deliberately outside
    /// overridePropertyPrefix: under it, the collection walk would read this back as an
    /// override named "OverrideSet". Style-owned: storeOverrideSet() is its sole writer, and
    /// overrideSetOf()'s memo is only sound because of that, so never set it from outside.
    static constexpr const char* overrideSetProperty    = "fcOverrideSet";
    // clang-format on


    /// Resolves the geometry of the box @p context describes: padding, margin and the
    /// dimension constraints that decide how big it may be.
    BoxGeometryDefinition resolveBoxGeometry(const StyleParameters::StyleContext& context) const;

    /// Resolves the appearance of the box @p context describes: background, border and radii.
    BoxStyleDefinition resolveBoxStyle(const StyleParameters::StyleContext& context) const;

    /**
     * @brief Resolves a box that abuts another one, and squares off the edge they share.
     *
     * The two halves of a split control are painted as separate boxes, so without this each
     * would round its own corners at the join and both would draw a border there. The half
     * that passes SeamBorder::Keep supplies the single rule between them.
     */
    BoxStyleDefinition seamedBoxStyle(
        const StyleParameters::StyleContext& context,
        SeamEdge seam,
        SeamBorder border
    ) const;

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

    /**
     * @brief Answers @p metric from a token, or nothing when no token describes it.
     *
     * Split out from pixelMetric() so a metric this style does not own falls through to the
     * base style rather than being answered with a fabricated value.
     */
    std::optional<int> resolvePixelMetric(
        PixelMetric metric,
        const QStyleOption* option,
        const QWidget* widget
    ) const;

    /// Draws an anti-aliased chevron pointing @p direction, filling @p rect.
    void drawChevronArrow(
        QPainter* painter,
        const QRect& rect,
        Qt::ArrowType direction,
        const QColor& color
    ) const;

    void drawToolButton(
        const QStyleOptionToolButton* option,
        QPainter* painter,
        const QWidget* widget
    ) const;

    void drawToolButtonLabel(
        QPainter* painter,
        const QStyleOptionToolButton* option,
        const QWidget* widget
    ) const;

    QSize toolButtonSizeFromContents(
        const QStyleOptionToolButton* option,
        const QSize& size,
        const QWidget* widget
    ) const;

    QRect toolButtonSubControlRect(
        const QStyleOptionToolButton* option,
        SubControl subControl,
        const QWidget* widget
    ) const;

    void drawPushButtonLabel(
        QPainter* painter,
        const QStyleOptionButton* option,
        const QWidget* widget
    ) const;

    /**
     * @brief Returns Qt::TextShowMnemonic, optionally OR'd with Qt::TextHideMnemonic.
     *
     * Queries SH_UnderlineShortcut so every label painter respects the same style hint.
     */
    int mnemonicTextFlags(const QStyleOption* option, const QWidget* widget) const;

    /**
     * @brief Shifts @p rect by PM_ButtonShift{Horizontal,Vertical} when sunken or checked.
     *
     * Returns @p rect unchanged when the option state has neither State_Sunken nor State_On.
     */
    QRect applyButtonShift(const QRect& rect, const QStyleOption* option, const QWidget* widget) const;

    /**
     * @brief Resolves the icon colour for @p context.
     *
     * Tries the IconColor token, then TextColor, then falls back to palette.buttonText().
     */
    QColor resolveIconColor(const StyleParameters::StyleContext& context, const QPalette& palette) const;

    /**
     * @brief Renders @p icon through IconManager in the colour @p context resolves to.
     */
    QPixmap renderStyledIcon(
        QPainter* painter,
        const QIcon& icon,
        const QSize& maxSize,
        QIcon::Mode mode,
        QIcon::State state,
        const StyleParameters::StyleContext& context,
        const QPalette& palette
    ) const;

    /// Convenience overload: derives mode, state and palette from @p option.
    QPixmap renderStyledIcon(
        QPainter* painter,
        const QIcon& icon,
        const QSize& maxSize,
        const QStyleOption* option,
        const StyleParameters::StyleContext& context
    ) const;

    /// The overrides @p widget declares itself, with the property prefix stripped.
    static StyleParameters::OverrideSet declaredOverrides(const QWidget* widget);

    /// Interns the overrides in effect for @p widget: its own, plus every ancestor's up to
    /// the window it sits in.
    static uint32_t computeOverrideSet(const QWidget* widget);

    /// Records @p set on @p widget, where overrideSetOf() reads it back.
    void storeOverrideSet(QWidget* widget, uint32_t set) const;

    void recomputeOverrideSets(QWidget* widget) const;

    /// The override set @p widget resolves against, or the empty id when it declares none.
    uint32_t overrideSetOf(const QWidget* widget) const;

    static void forEachChildWidget(QWidget* widget, const std::function<void(QWidget*)>& visit);

private:
    /// Attaches @p widget to @p context, so resolution can consult what the widget declares.
    static void bindWidget(StyleParameters::StyleContext& context, const QWidget* widget);

    // StyleContextCache<T> holds one map per override-set id, so two widgets with different
    // overrides never read each other's entries while widgets sharing a set share a bin.
    // Bin 0 is "no overrides". Every operation is const so const draw methods can use them.
    template<typename T>
    class StyleContextCache
    {
        using Bin = std::unordered_map<uint64_t, T>;

        mutable std::unordered_map<uint32_t, Bin> bins;

    public:
        const T* find(uint32_t bin, uint64_t key) const
        {
            const auto foundBin = bins.find(bin);
            if (foundBin == bins.end()) {
                return nullptr;
            }

            const auto found = foundBin->second.find(key);
            return found != foundBin->second.end() ? &found->second : nullptr;
        }

        void store(uint32_t bin, uint64_t key, T value) const
        {
            bins[bin].emplace(key, std::move(value));
        }

        void clear()
        {
            bins.clear();
        }
    };

    mutable StyleContextCache<std::optional<StyleParameters::Value>> tokenCache;
    mutable StyleContextCache<BoxStyleDefinition> boxStyleCache;
    mutable StyleContextCache<BoxGeometryDefinition> boxGeometryCache;

    // Single-entry memo for overrideSetOf(). resolveBoxStyle() and resolveBoxGeometry() each
    // resolve a dozen tokens from one context, and QObject::property() scans the widget class's
    // static property table on every call. QPointer clears itself when the widget dies, so a
    // stale pointer can never be compared against a new widget at the same address.
    mutable QPointer<const QWidget> overrideMemoWidget;
    mutable uint32_t overrideMemoSet = StyleParameters::OverrideRegistry::emptyId;
};

}  // namespace Gui
