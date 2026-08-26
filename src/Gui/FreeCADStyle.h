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

#include <QEvent>
#include <QProxyStyle>
#include <QPushButton>
#include <QStyleOption>

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

private:
    /// Attaches @p widget to @p context, so resolution can consult what the widget declares.
    static void bindWidget(StyleParameters::StyleContext& context, const QWidget* widget);

    mutable std::unordered_map<uint64_t, std::optional<StyleParameters::Value>> tokenCache;
};

}  // namespace Gui
