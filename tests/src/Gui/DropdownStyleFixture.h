// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <string>

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QRect>
#include <QScopeGuard>
#include <QWidget>
#include <QtGlobal>

#include "src/App/InitApplication.h"

#include <Gui/Application.h>
#include <Gui/FreeCADStyle.h>
#include <Gui/StyleParameters/ParameterManager.h>

// The style handles a theme reload in its event filter, which is protected. Delivering the event
// the way Gui::Application does — sending it to qApp, where the style is installed as a filter —
// is not open to a test either: every filter on qApp sees it, including Application's own, which
// reapplies the stylesheet through a main window a headless test does not have. Widening the
// access reaches the style's handler, which is the wiring under test, and nothing else.
class ReloadableStyle: public Gui::FreeCADStyle
{
public:
    using Gui::FreeCADStyle::eventFilter;
};

/// The dropdown theme every dropdown test resolves against, plus the render helpers that read
/// pixels back off a widget. Inherit privately from a QObject test class.
struct DropdownStyleFixture
{
    DropdownStyleFixture()
    {
        tests::initApplication();

        if (Gui::Application::Instance == nullptr) {
            new Gui::Application(true);
        }

        Gui::Application::Instance->styleParameterManager()->addSource(
            new Gui::StyleParameters::InMemoryParameterSource(
                {
                    {.name = "DropdownListMaxHeight", .value = "250px"},
                    // Mirrors the production token: a dropdown that must show every row clears
                    // the shared cap for itself alone.
                    {.name = "UncappedDropdownMaxHeight", .value = "reset()"},
                    {.name = "ShortDropdownMaxHeight", .value = "80px"},

                    // The combo box itself, which production also gives a padding. Stated here
                    // because it is what the popup's frame width must NOT be resolved from:
                    // without it nothing would inflate for a Select and a metric that leaked
                    // from the view onto the combo box would go unnoticed.
                    {.name = "SelectPadding", .value = "padding(horizontal: 8px, vertical: 4px)"},

                    // The popup surface. The container around the list paints it, so these are
                    // the tokens the popup's fill and edge come from.
                    {.name = "DropdownListBackground", .value = "#101010"},
                    {.name = "DropdownListBorderColor", .value = "#00ff00"},
                    {.name = "DropdownListBorderThickness", .value = "1px"},
                    {.name = "DropdownListBorderRadius", .value = "0px"},
                    // Deliberately larger than DropdownListItemSpacing below. Every row carries
                    // a leading gap, and the container gives the first one back by shrinking
                    // this padding at the top; a padding smaller than the gap would clamp at
                    // zero and hide whether the deduction happens at all.
                    {.name = "DropdownListPadding", .value = "padding(4px)"},

                    // The rows. The item-view path splits a row between two elements: padding
                    // and label colour on Item, the interaction fill on Row. Values are picked
                    // to be unmistakable, so a token resolved against the wrong element shows up
                    // as a wrong measurement rather than an accidental match.
                    {.name = "DropdownListItemPadding",
                     .value = "padding(horizontal: 7px, vertical: 5px)"},
                    {.name = "DropdownListItemSpacing", .value = "3px"},
                    {.name = "DropdownListRowSelectedBackground", .value = "#ff0000"},
                    {.name = "DropdownListRowHoveredBackground", .value = "#0000ff"},

                    // The same pair on the plain List component. The priority test below uses an
                    // untagged view on purpose: Task 3 gives DropdownList a selection of its own,
                    // derived from the combo box rather than from State_Selected, so a test of
                    // the generic item-view mapping must not be written against that component.
                    {.name = "ListRowSelectedBackground", .value = "#ff0000"},
                    {.name = "ListRowHoveredBackground", .value = "#0000ff"},
                },
                {.name = "Dropdown Fixture"}
            )
        );

        // Registered last so it outranks the fixture above, and left empty so it costs
        // nothing until a test asks for a different value.
        overrides = new Gui::StyleParameters::InMemoryParameterSource(
            {},
            {.name = "Dropdown Fixture Overrides"}
        );
        Gui::Application::Instance->styleParameterManager()->addSource(overrides);
    }

    // Horizontal and vertical halves of DropdownListItemPadding, and the inter-row gap, as the
    // fixture states them. Named so an assertion reads as the token it is pinning.
    static constexpr int itemPaddingHorizontal = 7;
    static constexpr int itemPaddingVertical = 5;
    static constexpr int itemSpacing = 3;

    // DropdownListBorderThickness and DropdownListPadding, as the fixture states them. Together
    // they are the whole inset between the popup's edge and the first row.
    static constexpr int containerBorder = 1;
    static constexpr int containerPadding = 4;

    // DropdownListBackground and DropdownListBorderColor, as the fixture states them. Anything
    // else down the popup's centre belongs to a row.
    static const inline QColor surfaceColor {0x10, 0x10, 0x10};
    static const inline QColor surfaceBorderColor {0x00, 0xff, 0x00};

    // Swaps one token in for the body of a test and puts the fixture's value back on the way
    // out, so an assertion that returns early cannot leak it into the next test.
    [[nodiscard]] auto overrideToken(const std::string& name, const std::string& value) const
    {
        auto* manager = Gui::Application::Instance->styleParameterManager();

        overrides->define({.name = name, .value = value});
        manager->reload();

        return qScopeGuard([this, manager, name] {
            overrides->remove(name);
            manager->reload();
        });
    }

    // The widget as it paints itself, over a ground no token in the fixture can produce — so a
    // pixel nothing painted is impossible to mistake for one something did.
    static QImage renderOf(QWidget& widget)
    {
        QImage canvas(widget.size(), QImage::Format_ARGB32);
        canvas.fill(Qt::magenta);
        widget.render(&canvas);
        return canvas;
    }

    // A FreeCADStyle installed the way Gui::Application installs it: as the application's.
    //
    // A combo box builds its popup as a window of its own, and a window inherits nothing from a
    // style set on one widget — set on the combo box alone, the popup would keep whatever the
    // platform theme provides and every measurement of it would be of the wrong style. It is
    // also always a fresh instance, because box geometry is cached for a style's lifetime and
    // only clearTokenCache() drops it, which an installed style is told to do on a theme reload
    // but not on the token overrides a test makes. QApplication takes ownership of the style it
    // is given and deletes the one it replaces, so successive calls clean up after each other.
    static ReloadableStyle& installFreshApplicationStyle()
    {
        auto* style = new ReloadableStyle();
        QApplication::setStyle(style);
        return *style;
    }

    // How much of @p rect @p canvas paints in @p colour. A label is a handful of glyph pixels
    // over a much larger fill, so a text colour can only be asserted as a presence or an
    // absence — never as the colour of any one pixel picked in advance.
    static int pixelsOfColour(const QImage& canvas, const QRect& rect, const QColor& colour)
    {
        int found = 0;
        for (int y = rect.top(); y <= rect.bottom(); ++y) {
            for (int x = rect.left(); x <= rect.right(); ++x) {
                if (canvas.rect().contains(x, y) && canvas.pixelColor(x, y) == colour) {
                    ++found;
                }
            }
        }
        return found;
    }

protected:
    Gui::StyleParameters::InMemoryParameterSource* overrides = nullptr;
};
