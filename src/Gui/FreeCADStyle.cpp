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
#include <QPalette>
#include <QStyleOption>
#include <QWidget>
#endif

#include "Application.h"
#include "FreeCADStyle.h"
#include "StyleParameters/ParameterDescriptorRegistry.h"
#include "StyleParameters/ParameterManager.h"

using namespace Gui;
using namespace Gui::StyleParameters;

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
