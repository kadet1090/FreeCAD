// SPDX-License-Identifier: LGPL-2.1-or-later

#include <algorithm>
#include <memory>
#include <string>

#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QImage>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QScopeGuard>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleOptionMenuItem>
#include <QTest>

#include "src/App/InitApplication.h"

#include <Gui/Application.h>
#include <Gui/FreeCADStyle.h>
#include <Gui/StyleParameters/ParameterManager.h>

// The fixture's own numbers, chosen so every column width is distinct and no two sums
// collide — a wrong column therefore fails loudly instead of coincidentally matching.
constexpr int menuBorder = 1;
constexpr int menuPadding = 4;
constexpr int iconSpacing = 8;
constexpr int iconSize = 16;
constexpr int indicatorSize = 14;
constexpr int arrowWidth = 10;
constexpr int shortcutSpacing = 20;
constexpr int separatorHeight = 9;
// 13px a side (26px total): distinct from every other constant above and from the fixture's own
// MenuItemPadding (6px horizontal, 3px vertical) — that collision had teeth, since the box's
// geometry sits inside the item's, so a bug that resolved the item's box instead of the
// indicator's would still have passed the horizontal assertions below at the old value.
constexpr int iconIndicatorPadding = 13;
constexpr int iconIndicatorPaddingTotal = iconIndicatorPadding * 2;

// menuItemLayout, menuItemDrawnLabel and menuArrowColor are protected on FreeCADStyle;
// using-declarations republish them so the column walk, the label-eliding decision and the
// arrow colour can be exercised without going through a live menu or painting into an image.
class ProbeStyle: public Gui::FreeCADStyle
{
public:
    using Gui::FreeCADStyle::menuArrowColor;
    using Gui::FreeCADStyle::menuItemDrawnLabel;
    using Gui::FreeCADStyle::menuItemLayout;
};

class TestMenuGeometry: public QObject
{
    Q_OBJECT

public:
    TestMenuGeometry()
    {
        tests::initApplication();

        if (Gui::Application::Instance == nullptr) {
            new Gui::Application(true);
        }

        Gui::Application::Instance->styleParameterManager()->addSource(
            new Gui::StyleParameters::InMemoryParameterSource(
                {
                    {.name = "MenuBackground", .value = "#202020"},
                    {.name = "MenuBorderColor", .value = "#000000"},
                    {.name = "MenuBorderThickness", .value = "1px"},
                    {.name = "MenuBorderRadius", .value = "0px"},
                    {.name = "MenuPadding", .value = "padding(4px)"},
                    {.name = "MenuIconSize", .value = "16px"},
                    {.name = "MenuOverlap", .value = "0px"},

                    {.name = "MenuItemPadding", .value = "padding(horizontal: 6px, vertical: 3px)"},
                    {.name = "MenuItemIconSpacing", .value = "8px"},
                    {.name = "MenuItemSpacing", .value = "0px"},
                    {.name = "MenuItemMargin", .value = "padding(0px)"},
                    {.name = "MenuItemTextColor", .value = "#ffffff"},
                    {.name = "MenuItemHoveredBackground", .value = "#ff0000"},
                    {.name = "MenuItemHoveredTextColor", .value = "#ffff00"},
                    {.name = "MenuItemCheckedBackground", .value = "#0000ff"},

                    {.name = "MenuIconIndicatorPadding", .value = "padding(13px)"},
                    {.name = "MenuIconIndicatorBorderRadius", .value = "2px"},
                    {.name = "MenuIconIndicatorBorderThickness", .value = "1px"},
                    {.name = "MenuIconIndicatorCheckedBackground", .value = "#00ff7f"},

                    {.name = "MenuShortcutSpacing", .value = "20px"},
                    {.name = "MenuShortcutTextColor", .value = "#808080"},

                    {.name = "MenuSeparatorHeight", .value = "9px"},
                    {.name = "MenuSeparatorMargin", .value = "padding(horizontal: 4px)"},
                    {.name = "MenuSeparatorBorderColor", .value = "#00ff00"},
                    {.name = "MenuSeparatorBorderThickness", .value = "1px"},
                    {.name = "MenuSeparatorPadding",
                     .value = "padding(horizontal: 4px, vertical: 2px)"},
                    {.name = "MenuSeparatorTextColor", .value = "#c0c0c0"},
                    // Deliberately not 4px, the hardcoded fallback BoxGeometryDefinition uses
                    // when this token fails to resolve — so a regression back to the silent
                    // fallback would show up as a wrong gap rather than an accidental match.
                    {.name = "MenuSeparatorIconSpacing", .value = "6px"},

                    // MenuArrowIconColor is deliberately left undefined: the shipped theme does
                    // not state it either, so the fixture exercises the same fall-through to
                    // the item label colour that the arrow relies on to track item state.
                    {.name = "MenuArrowWidth", .value = "10px"},

                    // PM_IndicatorWidth/Height resolve through the CheckBox component at the
                    // Root element — contextOf() routes the Indicator element there and does
                    // not carry the element across, so the tokens are CheckBoxWidth/Height.
                    {.name = "CheckBoxWidth", .value = "14px"},
                    {.name = "CheckBoxHeight", .value = "14px"},

                    // Two distinct fills so the check column says which component it
                    // resolved against, rather than only which glyph it drew.
                    {.name = "CheckBoxBackground", .value = "#00ffff"},
                    {.name = "RadioButtonBackground", .value = "#ff00ff"},
                },
                {.name = "Menu Geometry"}
            )
        );

        // Registered last so it outranks the fixture above, and left empty so it costs
        // nothing until a test asks for a different value.
        overrides = new Gui::StyleParameters::InMemoryParameterSource(
            {},
            {.name = "Menu Geometry Overrides"}
        );
        Gui::Application::Instance->styleParameterManager()->addSource(overrides);
    }

private:
    Gui::StyleParameters::InMemoryParameterSource* overrides = nullptr;

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

    // A menu option seeded the way QMenu::initStyleOption does for a plain text action.
    static QStyleOptionMenuItem plainItem(const QMenu& menu)
    {
        QStyleOptionMenuItem option;
        option.initFrom(&menu);
        option.menuItemType = QStyleOptionMenuItem::Normal;
        option.checkType = QStyleOptionMenuItem::NotCheckable;
        option.checked = false;
        option.menuHasCheckableItems = false;
        option.maxIconWidth = 0;
        option.reservedShortcutWidth = 0;
        option.text = QStringLiteral("Open");
        option.rect = QRect();
        return option;
    }

    // A non-null QIcon that needs no file on disk, so the layout's "does this row carry an
    // icon" decision can be exercised without a resource dependency.
    static QIcon solidIcon()
    {
        QPixmap pixmap(iconSize, iconSize);
        pixmap.fill(Qt::blue);
        return QIcon(pixmap);
    }


    // Paints one checkable row of the given check type over magenta and reports the colour at
    // the centre of its indicator, which is the fill the indicator's component resolved.
    // Returns an invalid colour when the row produced no indicator at all.
    static QColor indicatorFillOf(
        ProbeStyle& probeStyle,
        QMenu& menu,
        QStyleOptionMenuItem::CheckType checkType
    )
    {
        QStyle& style = probeStyle;

        QStyleOptionMenuItem option = plainItem(menu);
        option.menuHasCheckableItems = true;
        option.checkType = checkType;
        option.rect
            = QRect(QPoint(), style.sizeFromContents(QStyle::CT_MenuItem, &option, QSize(), &menu));

        const auto layout = probeStyle.menuItemLayout(&option, &menu);
        if (!layout.has_value() || layout->indicator.isNull()) {
            return {};
        }

        QImage canvas(option.rect.size(), QImage::Format_ARGB32);
        canvas.fill(Qt::magenta);

        QPainter painter(&canvas);
        style.drawControl(QStyle::CE_MenuItem, &option, &painter, &menu);
        painter.end();

        return canvas.pixelColor(layout->indicator.center());
    }

    // Paints one checkable row that carries an icon over magenta and reports the colour just
    // inside the state box behind that icon — clear of the icon itself and of the box's rounded
    // corners, so it is the box's own fill and not something drawn on top of it.
    // Returns an invalid colour when the row produced no box at all.
    static QColor iconIndicatorFillOf(
        ProbeStyle& probeStyle,
        QMenu& menu,
        QStyleOptionMenuItem::CheckType checkType,
        bool checked
    )
    {
        QStyle& style = probeStyle;

        QStyleOptionMenuItem option = plainItem(menu);
        option.menuHasCheckableItems = true;
        option.checkType = checkType;
        option.checked = checked;
        option.maxIconWidth = 20;
        option.icon = solidIcon();
        option.rect
            = QRect(QPoint(), style.sizeFromContents(QStyle::CT_MenuItem, &option, QSize(), &menu));

        const auto layout = probeStyle.menuItemLayout(&option, &menu);
        if (!layout.has_value() || layout->iconIndicator.isNull()) {
            return {};
        }

        QImage canvas(option.rect.size(), QImage::Format_ARGB32);
        canvas.fill(Qt::magenta);

        QPainter painter(&canvas);
        style.drawControl(QStyle::CE_MenuItem, &option, &painter, &menu);
        painter.end();

        return canvas.pixelColor(layout->iconIndicator.left() + 1, layout->iconIndicator.center().y());
    }

private Q_SLOTS:

    // QMenu asks the style for these before it lays anything out, and QMenu::paintEvent then
    // clips each CE_MenuItem call to its own action rect — so this is the only channel a
    // style has for insetting menu items from the popup edge. Panel width stays 0 so QMenu
    // skips its PE_FrameMenu pass and the border PE_PanelMenu paints is the only one.
    void test_popupMarginsComeFromTheMenuTokens()  // NOLINT
    {
        Gui::FreeCADStyle freecadStyle;
        QStyle& style = freecadStyle;
        QMenu menu;

        QCOMPARE(style.pixelMetric(QStyle::PM_MenuPanelWidth, nullptr, &menu), 0);
        QCOMPARE(style.pixelMetric(QStyle::PM_MenuHMargin, nullptr, &menu), menuBorder + menuPadding);
        QCOMPARE(style.pixelMetric(QStyle::PM_MenuVMargin, nullptr, &menu), menuBorder + menuPadding);
        QCOMPARE(style.pixelMetric(QStyle::PM_SubMenuOverlap, nullptr, &menu), 0);
    }

    // Every item reserves half of MenuItemSpacing above it and half below, and between two
    // items those halves merge into the intended gap. The first item's top half and the last
    // item's bottom half have no neighbour, so without compensation they pile onto the popup's
    // vertical inset and the popup reads taller than it is wide. Only the vertical metric is
    // adjusted, so the two axes agree again.
    void test_itemSpacingDoesNotWidenTheVerticalPopupInset()  // NOLINT
    {
        const auto guard = overrideToken("MenuItemSpacing", "6px");

        // A fresh style: box geometry is cached per instance and only clearTokenCache() drops
        // it, which a bare instance never gets told to do.
        Gui::FreeCADStyle freecadStyle;
        QStyle& style = freecadStyle;
        QMenu menu;

        QCOMPARE(style.pixelMetric(QStyle::PM_MenuHMargin, nullptr, &menu), menuBorder + menuPadding);
        QCOMPARE(
            style.pixelMetric(QStyle::PM_MenuVMargin, nullptr, &menu),
            menuBorder + menuPadding - (6 / 2)
        );
    }

    // The deduction is clamped: a theme whose MenuPadding is smaller than half its
    // MenuItemSpacing must not end up with a margin inside the border, or the items paint
    // over the frame.
    void test_verticalPopupInsetNeverFallsBelowTheBorder()  // NOLINT
    {
        // Far more than 2 * (menuBorder + menuPadding), so an unclamped deduction would go
        // negative.
        const auto guard = overrideToken("MenuItemSpacing", "40px");

        Gui::FreeCADStyle freecadStyle;
        QStyle& style = freecadStyle;
        QMenu menu;

        QCOMPARE(style.pixelMetric(QStyle::PM_MenuVMargin, nullptr, &menu), menuBorder);
    }

    // The metrics are menu-scoped: a widget that is not a QMenu must keep whatever the base
    // style says, or every other popup in the application shifts.
    void test_nonMenuWidgetsKeepTheBaseStyleMargins()  // NOLINT
    {
        Gui::FreeCADStyle freecadStyle;
        QStyle& style = freecadStyle;
        QWidget plain;

        // FreeCADStyle proxies Fusion, so Fusion is the baseline a non-menu widget must
        // still see — not QApplication::style(), which a platform theme plugin (qt5ct,
        // qt6ct) replaces, making the comparison depend on the developer's environment.
        const std::unique_ptr<QStyle> fusion(QStyleFactory::create(QStringLiteral("Fusion")));
        QVERIFY(fusion != nullptr);

        QCOMPARE(
            style.pixelMetric(QStyle::PM_MenuHMargin, nullptr, &plain),
            fusion->pixelMetric(QStyle::PM_MenuHMargin, nullptr, &plain)
        );
        QCOMPARE(
            style.pixelMetric(QStyle::PM_MenuVMargin, nullptr, &plain),
            fusion->pixelMetric(QStyle::PM_MenuVMargin, nullptr, &plain)
        );
    }

    // PE_PanelMenu is drawn first, over the whole widget rect and unclipped, so it is where
    // the surface belongs. CE_MenuEmptyArea then runs last over whatever region the items
    // left and must not repaint anything.
    void test_panelMenuPaintsTheWholePopupSurface()  // NOLINT
    {
        Gui::FreeCADStyle freecadStyle;
        QStyle& style = freecadStyle;
        QMenu menu;
        menu.resize(120, 60);

        QImage canvas(menu.size(), QImage::Format_ARGB32);
        canvas.fill(Qt::magenta);

        QStyleOption option;
        option.initFrom(&menu);
        option.rect = QRect(QPoint(), menu.size());

        QPainter painter(&canvas);
        style.drawPrimitive(QStyle::PE_PanelMenu, &option, &painter, &menu);
        painter.end();

        // Centre is the background token; the outermost pixel is the border token.
        QCOMPARE(canvas.pixelColor(60, 30), QColor(QStringLiteral("#202020")));
        QCOMPARE(canvas.pixelColor(0, 0), QColor(QStringLiteral("#000000")));
    }

    // REGRESSION GUARD, not a red-first test: Fusion's CE_MenuEmptyArea is already a no-op,
    // so this passes before the handler exists and cannot be made to fail first. It is here
    // to lock in that the empty area never paints over the surface PE_PanelMenu now owns.
    void test_emptyAreaLeavesThePanelUntouched()  // NOLINT
    {
        Gui::FreeCADStyle freecadStyle;
        QStyle& style = freecadStyle;
        QMenu menu;
        menu.resize(120, 60);

        QImage canvas(menu.size(), QImage::Format_ARGB32);
        canvas.fill(Qt::magenta);

        QStyleOptionMenuItem option = plainItem(menu);
        option.menuItemType = QStyleOptionMenuItem::EmptyArea;
        option.rect = QRect(QPoint(), menu.size());

        QPainter painter(&canvas);
        style.drawControl(QStyle::CE_MenuEmptyArea, &option, &painter, &menu);
        painter.end();

        QCOMPARE(canvas.pixelColor(60, 30), QColor(Qt::magenta));
    }

    // Each column is reserved only under its own condition, and costs exactly its width plus
    // one MenuItemIconSpacing gap. Deltas keep this independent of the test font.
    void test_eachColumnCostsItsWidthPlusOneGap()  // NOLINT
    {
        Gui::FreeCADStyle freecadStyle;
        QStyle& style = freecadStyle;
        QMenu menu;

        const QStyleOptionMenuItem plain = plainItem(menu);
        const auto widthOf = [&style, &menu](const QStyleOptionMenuItem& option) {
            return style.sizeFromContents(QStyle::CT_MenuItem, &option, QSize(), &menu).width();
        };
        const int base = widthOf(plain);

        // maxIconWidth is Qt's hardcoded PM_SmallIconSize + 4; only its non-zero answer is
        // used, and the column itself comes from MenuIconSize.
        QStyleOptionMenuItem withIcon = plain;
        withIcon.maxIconWidth = 20;
        QCOMPARE(widthOf(withIcon) - base, iconSize + iconSpacing);

        QStyleOptionMenuItem submenu = plain;
        submenu.menuItemType = QStyleOptionMenuItem::SubMenu;
        QCOMPARE(widthOf(submenu) - base, arrowWidth + iconSpacing);
    }

    // Elements do not chain, so an Arrow-element token never sees the item's state. With no
    // arrow colour stated the arrow follows the label instead of freezing at the resting colour
    // — the defect that made hovered submenu rows keep a dark chevron on a white label. The
    // resolved colour is asserted directly; sampling an antialiased chevron is not deterministic.
    void test_arrowFollowsTheItemTextColourThroughItsStates()  // NOLINT
    {
        ProbeStyle freecadStyle;
        QMenu menu;

        QStyleOptionMenuItem option = plainItem(menu);
        option.menuItemType = QStyleOptionMenuItem::SubMenu;

        QCOMPARE(freecadStyle.menuArrowColor(&option, &menu), QColor(QStringLiteral("#ffffff")));

        // QMenu marks the row under the cursor with State_Selected, not State_MouseOver.
        option.state |= QStyle::State_Selected;
        QCOMPARE(freecadStyle.menuArrowColor(&option, &menu), QColor(QStringLiteral("#ffff00")));
    }

    // Stating MenuArrowIconColor is how a theme asks for an arrow colour distinct from the
    // label; it then wins in every state, which is the documented cost of opting out.
    void test_statedArrowColourWinsOverTheItemTextColour()  // NOLINT
    {
        const auto guard = overrideToken("MenuArrowIconColor", "#00ffff");

        ProbeStyle freecadStyle;
        QMenu menu;

        QStyleOptionMenuItem option = plainItem(menu);
        option.menuItemType = QStyleOptionMenuItem::SubMenu;

        QCOMPARE(freecadStyle.menuArrowColor(&option, &menu), QColor(QStringLiteral("#00ffff")));

        option.state |= QStyle::State_Selected;
        QCOMPARE(freecadStyle.menuArrowColor(&option, &menu), QColor(QStringLiteral("#00ffff")));
    }

    // Qt stacks action rects with a bare y += height() and has no spacing metric, so the gap
    // has to be built into the row height. It is split half above and half below because
    // CT_MenuItem runs before Qt positions anything and cannot tell a first row from any other.
    void test_itemSpacingAddsToTheRowHeight()  // NOLINT
    {
        QMenu menu;
        QStyleOptionMenuItem option = plainItem(menu);

        const int tight = [&] {
            Gui::FreeCADStyle freecadStyle;
            QStyle& style = freecadStyle;
            return style.sizeFromContents(QStyle::CT_MenuItem, &option, QSize(), &menu).height();
        }();

        const auto guard = overrideToken("MenuItemSpacing", "6px");

        // A fresh style: FreeCADStyle caches resolved box geometry per instance, and only
        // clearTokenCache() drops it — which fires from eventFilter() on ThemeReloadEvent,
        // an event a bare style instance like this one never receives. Reusing the instance
        // above would measure the pre-override geometry straight out of its cache.
        Gui::FreeCADStyle spacedFreecadStyle;
        QStyle& spacedStyle = spacedFreecadStyle;
        const int spaced
            = spacedStyle.sizeFromContents(QStyle::CT_MenuItem, &option, QSize(), &menu).height();

        QCOMPARE(spaced - tight, 6);
    }

    // Every other test here drives the style directly with a synthesised option, so
    // QMenuPrivate::updateActionRects — the code that consumes the four pixel metrics and
    // stacks the rows — never runs. This is the end-to-end check: a real popup, real actions,
    // laid out by Qt against these metrics. Each row carries half of MenuItemSpacing at each
    // of its own ends, so the popup's two axes only agree once the vertical metric has given
    // that orphaned half back — which is what this measures where it is actually visible.
    void test_aRealMenuStacksItsActionsAgainstTheMetrics()  // NOLINT
    {
        const auto guard = overrideToken("MenuItemSpacing", "6px");

        // Declared before the menu so it outlives it: QWidget::setStyle does not take
        // ownership, and a fresh instance is needed for the override to be visible at all.
        Gui::FreeCADStyle freecadStyle;

        QMenu menu;
        menu.setStyle(&freecadStyle);

        const QList<QAction*> actions {
            menu.addAction(QStringLiteral("Open")),
            menu.addAction(QStringLiteral("Save")),
            menu.addAction(QStringLiteral("Save As...")),
        };
        menu.ensurePolished();

        const QSize hint = menu.sizeHint();
        const int verticalMargin = freecadStyle.pixelMetric(QStyle::PM_MenuVMargin, nullptr, &menu);

        int stackedHeight = 0;
        QRect previous;

        for (QAction* action : actions) {
            const QRect geometry = menu.actionGeometry(action);
            QVERIFY(!geometry.isEmpty());

            if (!previous.isNull()) {
                // Abutting, not overlapping and not gapped: the gap lives inside the rows.
                QCOMPARE(geometry.top(), previous.bottom() + 1);
            }

            stackedHeight += geometry.height();
            previous = geometry;
        }

        const QRect first = menu.actionGeometry(actions.constFirst());
        const QRect last = menu.actionGeometry(actions.constLast());

        // The list sits centred between the popup's edges. A genuine invariant, but note that
        // it cannot see the defect below: that one was symmetric top-to-bottom.
        QCOMPARE(first.top(), hint.height() - 1 - last.bottom());

        // Nothing but the two margins is added around the stack.
        QCOMPARE(verticalMargin + stackedHeight + verticalMargin, hint.height());

        // The half gap a row carries at its top sits inside the action rect, so the row's
        // painted edge is half a gap below the rect. Nothing equivalent happens horizontally.
        // The inset the eye sees is therefore PM_MenuVMargin plus half a gap vertically
        // against PM_MenuHMargin horizontally, and the two have to come out equal — the
        // measured defect was rows sitting 4px from the top and bottom edges but 3px from the
        // left and right, vertically symmetric and still wrong.
        constexpr int halfItemSpacing = 6 / 2;
        const int horizontalMargin = freecadStyle.pixelMetric(QStyle::PM_MenuHMargin, nullptr, &menu);

        QCOMPARE(first.left(), horizontalMargin);
        QCOMPARE(first.top() + halfItemSpacing, horizontalMargin);
    }

    // The guard against the whole class of bug where the size hint and the paint code drift
    // apart: give the item exactly the width the hint asked for, then check every column
    // still fits, in order, without overlapping its neighbour.
    void test_layoutFitsInsideTheWidthTheSizeHintAsked()  // NOLINT
    {
        ProbeStyle freecadStyle;
        QStyle& style = freecadStyle;
        QMenu menu;

        QStyleOptionMenuItem option = plainItem(menu);
        option.menuItemType = QStyleOptionMenuItem::SubMenu;
        // A non-checkable row in a menu that has checkable items — the "Customize" row of the
        // toolbar-visibility menu. It carries an icon, the wider of the two things that can land
        // in the leading column, so it is the tightest row the reservation has to accommodate.
        option.menuHasCheckableItems = true;
        option.maxIconWidth = 20;
        option.icon = solidIcon();
        option.text = QStringLiteral("Export as\tCtrl+Shift+E");
        option.reservedShortcutWidth = 77;

        const QSize hint = style.sizeFromContents(QStyle::CT_MenuItem, &option, QSize(), &menu);

        // QMenuPrivate::updateActionRects() widens every row to max_column_width and then
        // adds tabWidth once, globally. Reproduce that here or the shortcut has no room.
        option.rect = QRect(0, 0, hint.width() + option.reservedShortcutWidth, hint.height());

        const auto layout = freecadStyle.menuItemLayout(&option, &menu);
        QVERIFY(layout.has_value());

        // The icon and the indicator share the leading column, and this row has an icon.
        QVERIFY(!layout->icon.isNull());
        QVERIFY(!layout->arrow.isNull());

        QVERIFY(layout->icon.right() < layout->text.left());
        QVERIFY(layout->text.right() < layout->arrow.left());

        QVERIFY(option.rect.contains(layout->icon));
        QVERIFY(option.rect.contains(layout->arrow));

        // The label keeps at least the width it was measured at, so nothing elides that the
        // size hint claimed would fit.
        const QFontMetrics metrics(option.font);
        const int measured
            = metrics.boundingRect(QRect(), Qt::TextShowMnemonic, QStringLiteral("Export as")).width();
        QVERIFY2(
            layout->text.width() >= measured,
            qPrintable(
                QStringLiteral("text rect %1px, label needs %2px").arg(layout->text.width()).arg(measured)
            )
        );
    }

    // Two-measurement trap regression guard: menuItemSizeFromContents() measures a label with
    // mnemonicTextFlags(), which does not count '&' as a glyph. If drawMenuItemText() measured
    // eliding differently, the widest item in a mnemonic-heavy menu — exactly the one
    // CT_MenuItem sized to fit the label with no slack to spare — would get a spurious
    // ellipsis. Reproduces the width CT_MenuItem actually granted (via menuItemLayout(), not a
    // hand-picked rect) and asserts the label survives untouched.
    void test_mnemonicLabelIsNotElidedWhenItFitsTheSizeHint()  // NOLINT
    {
        ProbeStyle freecadStyle;
        QStyle& style = freecadStyle;
        QMenu menu;

        const QString text = QStringLiteral("&Open Recent");
        QStyleOptionMenuItem option = plainItem(menu);
        option.text = text;
        option.rect
            = QRect(QPoint(), style.sizeFromContents(QStyle::CT_MenuItem, &option, QSize(), &menu));

        const auto layout = freecadStyle.menuItemLayout(&option, &menu);
        QVERIFY(layout.has_value());

        // mnemonicTextFlags() is private; reproduced here from the public styleHint() API so
        // the test measures with the same flags the real draw call uses, without needing to
        // expose it.
        int textFlags = Qt::TextShowMnemonic;
        if (!style.styleHint(QStyle::SH_UnderlineShortcut, &option, &menu)) {
            textFlags |= Qt::TextHideMnemonic;
        }

        const QFontMetrics metrics(option.font);
        const QString drawnLabel
            = ProbeStyle::menuItemDrawnLabel(metrics, textFlags, text, layout->text.width());

        QCOMPARE(drawnLabel, text);
    }

    // Columns a plain item does not have leave null rects, so the caller can test for them
    // rather than reasoning about zero-width geometry.
    void test_plainItemReservesNothingButItsLabel()  // NOLINT
    {
        ProbeStyle freecadStyle;
        QStyle& style = freecadStyle;
        QMenu menu;

        QStyleOptionMenuItem option = plainItem(menu);
        option.rect
            = QRect(QPoint(), style.sizeFromContents(QStyle::CT_MenuItem, &option, QSize(), &menu));

        const auto layout = freecadStyle.menuItemLayout(&option, &menu);
        QVERIFY(layout.has_value());
        QVERIFY(layout->icon.isNull());
        QVERIFY(layout->arrow.isNull());
        QVERIFY(!layout->text.isNull());
    }

    // The walk is written left-to-right and mirrored as a block, so right-to-left has to put
    // the leading column on the right without any per-part special casing.
    void test_rightToLeftMirrorsTheWholeWalk()  // NOLINT
    {
        ProbeStyle freecadStyle;
        QStyle& style = freecadStyle;
        QMenu menu;

        QStyleOptionMenuItem option = plainItem(menu);
        option.menuItemType = QStyleOptionMenuItem::SubMenu;
        option.menuHasCheckableItems = true;
        option.checkType = QStyleOptionMenuItem::NonExclusive;
        option.maxIconWidth = 20;
        option.direction = Qt::RightToLeft;
        option.rect
            = QRect(QPoint(), style.sizeFromContents(QStyle::CT_MenuItem, &option, QSize(), &menu));

        const auto layout = freecadStyle.menuItemLayout(&option, &menu);
        QVERIFY(layout.has_value());

        // Leading column is now on the right and the trailing arrow on the left: the whole
        // order reverses, without any per-part special casing.
        QVERIFY(layout->icon.left() > layout->text.left());
        QVERIFY(layout->text.left() > layout->arrow.left());
    }

    // The hovered background is the one state the fixture can see without a font: paint a real
    // "Open" item into an image and sample the far left of the box, inside the padding, before
    // the label starts — the same pixel and rationale as test_restingItemPaintsNoBackground, so
    // the two differ only in State_Selected and isolate that one axis. Sampling behind a real
    // label (rather than blanking the text) also exercises the stronger claim that the
    // highlight spans the whole row, including the padding, not just wherever layout->text
    // happens to land.
    void test_hoveredItemPaintsItsStateBackground()  // NOLINT
    {
        Gui::FreeCADStyle freecadStyle;
        QStyle& style = freecadStyle;
        QMenu menu;

        QStyleOptionMenuItem option = plainItem(menu);
        option.rect
            = QRect(QPoint(), style.sizeFromContents(QStyle::CT_MenuItem, &option, QSize(), &menu));
        // QMenu marks the row under the cursor with State_Selected, not State_MouseOver.
        option.state |= QStyle::State_Selected;

        QImage canvas(option.rect.size(), QImage::Format_ARGB32);
        canvas.fill(Qt::magenta);

        QPainter painter(&canvas);
        style.drawControl(QStyle::CE_MenuItem, &option, &painter, &menu);
        painter.end();

        QCOMPARE(canvas.pixelColor(1, option.rect.center().y()), QColor(QStringLiteral("#ff0000")));
    }

    // At rest no background token resolves, so the row must stay transparent and let the
    // popup surface through.
    void test_restingItemPaintsNoBackground()  // NOLINT
    {
        Gui::FreeCADStyle freecadStyle;
        QStyle& style = freecadStyle;
        QMenu menu;

        QStyleOptionMenuItem option = plainItem(menu);
        option.rect
            = QRect(QPoint(), style.sizeFromContents(QStyle::CT_MenuItem, &option, QSize(), &menu));

        QImage canvas(option.rect.size(), QImage::Format_ARGB32);
        canvas.fill(Qt::magenta);

        QPainter painter(&canvas);
        style.drawControl(QStyle::CE_MenuItem, &option, &painter, &menu);
        painter.end();

        // The far left of the box is inside the padding, before the label starts.
        QCOMPARE(canvas.pixelColor(1, option.rect.center().y()), QColor(Qt::magenta));
    }

    // The assertion that pins the shared leading column. A menu with both icons and checkable
    // items — the toolbar-visibility menu is the real one — must reserve ONE slot, as wide as
    // the widest thing that can land in it, not one slot each. Two slots would indent every
    // label past a column that is empty on all but a handful of rows.
    void test_iconAndIndicatorShareOneLeadingColumn()  // NOLINT
    {
        Gui::FreeCADStyle freecadStyle;
        QStyle& style = freecadStyle;
        QMenu menu;

        const QStyleOptionMenuItem plain = plainItem(menu);
        const auto widthOf = [&style, &menu](const QStyleOptionMenuItem& option) {
            return style.sizeFromContents(QStyle::CT_MenuItem, &option, QSize(), &menu).width();
        };
        const int base = widthOf(plain);

        QStyleOptionMenuItem both = plain;
        both.menuHasCheckableItems = true;
        both.maxIconWidth = 20;

        // Both flags are menu-wide, so this holds for every row of such a menu regardless of
        // what the row itself carries. The icon's contribution now includes the state box's
        // padding, since on a checkable row the icon wears that box.
        QCOMPARE(
            widthOf(both) - base,
            std::max(iconSize + iconIndicatorPaddingTotal, indicatorSize) + iconSpacing
        );
    }

    // One slot, one occupant, decided in the layout so the painter cannot diverge from it. The
    // icon takes the slot whenever the row has one — it is how the command is found — and a
    // checkable row wears its state as a box behind that icon rather than trading the icon away.
    // Only a checkable row with no icon falls back to the glyph.
    void test_aCheckableRowWithAnIconKeepsBoth()  // NOLINT
    {
        ProbeStyle freecadStyle;
        QStyle& style = freecadStyle;
        QMenu menu;

        const auto layoutOf = [&freecadStyle, &style, &menu](QStyleOptionMenuItem option) {
            option.rect = QRect(
                QPoint(),
                style.sizeFromContents(QStyle::CT_MenuItem, &option, QSize(), &menu)
            );
            return freecadStyle.menuItemLayout(&option, &menu);
        };

        QStyleOptionMenuItem withIcon = plainItem(menu);
        withIcon.maxIconWidth = 20;
        withIcon.icon = solidIcon();

        const auto iconLayout = layoutOf(withIcon);
        QVERIFY(iconLayout.has_value());
        QVERIFY(!iconLayout->icon.isNull());
        QVERIFY(iconLayout->indicator.isNull());
        // A row that cannot be checked has no state to show, so it gets no box.
        QVERIFY(iconLayout->iconIndicator.isNull());

        QStyleOptionMenuItem checkableWithoutIcon = plainItem(menu);
        checkableWithoutIcon.menuHasCheckableItems = true;
        checkableWithoutIcon.checkType = QStyleOptionMenuItem::NonExclusive;

        const auto indicatorLayout = layoutOf(checkableWithoutIcon);
        QVERIFY(indicatorLayout.has_value());
        QVERIFY(!indicatorLayout->indicator.isNull());
        QVERIFY(indicatorLayout->icon.isNull());
        QVERIFY(indicatorLayout->iconIndicator.isNull());

        // Sketcher's "Toggle grid" is exactly this shape: checkable and carrying an icon. It
        // keeps the icon and gets the box; the glyph stays away.
        QStyleOptionMenuItem checkableWithIcon = checkableWithoutIcon;
        checkableWithIcon.maxIconWidth = 20;
        checkableWithIcon.icon = solidIcon();

        const auto sharedLayout = layoutOf(checkableWithIcon);
        QVERIFY(sharedLayout.has_value());
        QVERIFY(sharedLayout->indicator.isNull());
        QVERIFY(!sharedLayout->icon.isNull());
        QVERIFY(!sharedLayout->iconIndicator.isNull());
    }

    // The box is the icon rect grown by the box's own padding, so it is centred on the icon by
    // construction rather than by a second calculation that could drift from the first. Asserting
    // containment and the per-edge inset keeps this from re-implementing the layout.
    void test_theIconIndicatorIsTheIconGrownByItsPadding()  // NOLINT
    {
        ProbeStyle freecadStyle;
        QStyle& style = freecadStyle;
        QMenu menu;

        QStyleOptionMenuItem option = plainItem(menu);
        option.menuHasCheckableItems = true;
        option.checkType = QStyleOptionMenuItem::NonExclusive;
        option.maxIconWidth = 20;
        option.icon = solidIcon();
        option.rect
            = QRect(QPoint(), style.sizeFromContents(QStyle::CT_MenuItem, &option, QSize(), &menu));

        const auto layout = freecadStyle.menuItemLayout(&option, &menu);
        QVERIFY(layout.has_value());
        QVERIFY(layout->iconIndicator.contains(layout->icon));
        QCOMPARE(layout->icon.left() - layout->iconIndicator.left(), iconIndicatorPadding);
        QCOMPARE(layout->iconIndicator.right() - layout->icon.right(), iconIndicatorPadding);
        QCOMPARE(layout->icon.top() - layout->iconIndicator.top(), iconIndicatorPadding);
        QCOMPARE(layout->iconIndicator.bottom() - layout->icon.bottom(), iconIndicatorPadding);

        // The row was sized for the box, not just the icon, so the box cannot be clipped.
        QVERIFY(option.rect.contains(layout->iconIndicator));
    }

    // The box says one thing and must say it only when it is true: unchecked has to paint
    // nothing at all, not a transparent box, or every checkable row would wear a permanent well.
    void test_theIconIndicatorPaintsOnlyWhenChecked()  // NOLINT
    {
        ProbeStyle freecadStyle;
        QMenu menu;

        QCOMPARE(
            iconIndicatorFillOf(freecadStyle, menu, QStyleOptionMenuItem::NonExclusive, true),
            QColor(QStringLiteral("#00ff7f"))
        );
        QCOMPARE(
            iconIndicatorFillOf(freecadStyle, menu, QStyleOptionMenuItem::NonExclusive, false),
            QColor(Qt::magenta)
        );
    }

    // The two check types ship identical, so nothing else in this file can tell them apart at the
    // box. A theme must still be able to: an exclusive group wants a round well where a checkbox
    // wants a square one. Stating a token for the Exclusive variant alone has to reach exactly
    // the exclusive row and leave the other on the shared value.
    void test_anExclusiveRowResolvesItsOwnIconIndicatorTokens()  // NOLINT
    {
        const auto guard = overrideToken("MenuIconIndicatorExclusiveCheckedBackground", "#ff8000");

        // The style must be built after the override: FreeCADStyle caches resolved box geometry
        // and box styles per instance, and only clearTokenCache() drops them — which fires from
        // eventFilter() on ThemeReloadEvent, an event a bare instance like this never receives.
        ProbeStyle freecadStyle;
        QMenu menu;

        QCOMPARE(
            iconIndicatorFillOf(freecadStyle, menu, QStyleOptionMenuItem::Exclusive, true),
            QColor(QStringLiteral("#ff8000"))
        );
        QCOMPARE(
            iconIndicatorFillOf(freecadStyle, menu, QStyleOptionMenuItem::NonExclusive, true),
            QColor(QStringLiteral("#00ff7f"))
        );
    }

    // The column is as wide as the widest occupant any row can have, so a narrower occupant has
    // to be centred in it rather than pinned to the leading edge — otherwise a 14px indicator in
    // a 16px column sits 1px off the icons above and below it.
    void test_aNarrowOccupantIsCentredInTheSharedColumn()  // NOLINT
    {
        ProbeStyle freecadStyle;
        QStyle& style = freecadStyle;
        QMenu menu;

        QStyleOptionMenuItem option = plainItem(menu);
        option.menuHasCheckableItems = true;
        option.checkType = QStyleOptionMenuItem::NonExclusive;
        // Icons present too, so the column is iconSize wide while the occupant is indicatorSize.
        option.maxIconWidth = 20;
        option.rect
            = QRect(QPoint(), style.sizeFromContents(QStyle::CT_MenuItem, &option, QSize(), &menu));

        const auto layout = freecadStyle.menuItemLayout(&option, &menu);
        QVERIFY(layout.has_value());
        QVERIFY(!layout->indicator.isNull());
        QCOMPARE(layout->indicator.width(), indicatorSize);

        // Column starts at the content edge: border is not part of the item rect, so it is just
        // the item's own left padding.
        constexpr int itemPaddingLeft = 6;
        QCOMPARE(
            layout->indicator.left(),
            itemPaddingLeft + ((iconSize + iconIndicatorPaddingTotal - indicatorSize) / 2)
        );
        QVERIFY(option.rect.contains(layout->indicator));
    }

    // Which glyph a checkable row draws is decided from checkType, but which component it
    // resolves against is not: contextOf() is handed the QMenu and an Indicator element, and a
    // QMenu is no more a QRadioButton than it is a QCheckBox, so both check types would answer
    // CheckBox. The only difference the theme states between the two is the border radius, so
    // an exclusive row — View → Unit system, or any QActionGroup menu — came out as a rounded
    // square instead of a circle. The primitive is the authority on which glyph it is drawing.
    void test_exclusiveRowsResolveTheRadioIndicatorTokens()  // NOLINT
    {
        ProbeStyle freecadStyle;
        QMenu menu;

        QCOMPARE(
            indicatorFillOf(freecadStyle, menu, QStyleOptionMenuItem::NonExclusive),
            QColor(QStringLiteral("#00ffff"))
        );
        QCOMPARE(
            indicatorFillOf(freecadStyle, menu, QStyleOptionMenuItem::Exclusive),
            QColor(QStringLiteral("#ff00ff"))
        );
    }

    // MenuItemCheckedBackground is not stated by the shipped theme — the indicator carries the
    // state — but contextOf() does map QStyleOptionMenuItem::checked to StyleState::Checked, so a
    // theme that states it gets a row tint. This pins that mapping.
    void test_checkedItemResolvesItsRowBackground()  // NOLINT
    {
        Gui::FreeCADStyle freecadStyle;
        QStyle& style = freecadStyle;
        QMenu menu;

        QStyleOptionMenuItem option = plainItem(menu);
        option.menuHasCheckableItems = true;
        option.checkType = QStyleOptionMenuItem::NonExclusive;
        option.checked = true;
        option.rect
            = QRect(QPoint(), style.sizeFromContents(QStyle::CT_MenuItem, &option, QSize(), &menu));

        QImage canvas(option.rect.size(), QImage::Format_ARGB32);
        canvas.fill(Qt::magenta);

        QPainter painter(&canvas);
        style.drawControl(QStyle::CE_MenuItem, &option, &painter, &menu);
        painter.end();

        // Same pixel and rationale as test_hoveredItemPaintsItsStateBackground: the far left of
        // the box, inside the padding, before anything else is drawn.
        QCOMPARE(canvas.pixelColor(1, option.rect.center().y()), QColor(QStringLiteral("#0000ff")));
    }

    // test_rightToLeftMirrorsTheWholeWalk exercises the glyph path only (no icon), so it never
    // touches the layout.iconIndicator mirroring line in menuItemLayout(). A checkable row with
    // an icon puts the state box on the leading (right) side too, and the box has to stay
    // wrapped around the icon after mirroring rather than being left on the wrong side of it.
    void test_rightToLeftMirrorsTheIconIndicator()  // NOLINT
    {
        ProbeStyle freecadStyle;
        QStyle& style = freecadStyle;
        QMenu menu;

        QStyleOptionMenuItem option = plainItem(menu);
        option.menuHasCheckableItems = true;
        option.checkType = QStyleOptionMenuItem::NonExclusive;
        option.maxIconWidth = 20;
        option.icon = solidIcon();
        option.direction = Qt::RightToLeft;
        option.rect
            = QRect(QPoint(), style.sizeFromContents(QStyle::CT_MenuItem, &option, QSize(), &menu));

        const auto layout = freecadStyle.menuItemLayout(&option, &menu);
        QVERIFY(layout.has_value());
        QVERIFY(!layout->iconIndicator.isNull());

        // The box stays wrapped around the icon after mirroring, not left behind on the icon's
        // pre-mirror side.
        QVERIFY(layout->iconIndicator.contains(layout->icon));

        // The leading column — icon and its box — sits on the right, same as the glyph-only
        // case above.
        QVERIFY(layout->iconIndicator.left() > layout->text.left());
    }

    void test_plainSeparatorUsesItsOwnHeight()  // NOLINT
    {
        Gui::FreeCADStyle freecadStyle;
        QStyle& style = freecadStyle;
        QMenu menu;

        QStyleOptionMenuItem option = plainItem(menu);
        option.menuItemType = QStyleOptionMenuItem::Separator;
        option.text = QString();

        // Qt seeds separators at {2,2}; the style replaces that outright.
        QCOMPARE(
            style.sizeFromContents(QStyle::CT_MenuItem, &option, QSize(2, 2), &menu).height(),
            separatorHeight
        );
    }

    void test_plainSeparatorPaintsItsRule()  // NOLINT
    {
        Gui::FreeCADStyle freecadStyle;
        QStyle& style = freecadStyle;
        QMenu menu;

        QStyleOptionMenuItem option = plainItem(menu);
        option.menuItemType = QStyleOptionMenuItem::Separator;
        option.text = QString();
        option.rect = QRect(0, 0, 120, separatorHeight);

        QImage canvas(option.rect.size(), QImage::Format_ARGB32);
        canvas.fill(Qt::magenta);

        QPainter painter(&canvas);
        style.drawControl(QStyle::CE_MenuItem, &option, &painter, &menu);
        painter.end();

        // The rule sits on the vertical centre, inset horizontally by MenuSeparatorMargin.
        QCOMPARE(canvas.pixelColor(60, separatorHeight / 2), QColor(QStringLiteral("#00ff00")));
        // Outside the margin it stays clear.
        QCOMPARE(canvas.pixelColor(0, separatorHeight / 2), QColor(Qt::magenta));
    }

    // A section is a separator carrying text. The rule fills what the label leaves, so the row
    // reads as a titled divider rather than a label floating on its own.
    void test_sectionHeaderKeepsItsLabelClearOfTheRule()  // NOLINT
    {
        Gui::FreeCADStyle freecadStyle;
        QStyle& style = freecadStyle;
        QMenu menu;

        QStyleOptionMenuItem option = plainItem(menu);
        option.menuItemType = QStyleOptionMenuItem::Separator;
        option.text = QStringLiteral("Visibility");

        const QSize hint = style.sizeFromContents(QStyle::CT_MenuItem, &option, QSize(2, 2), &menu);
        // Wider than the hint, so there is room left over for the rule.
        option.rect = QRect(0, 0, hint.width() + 80, hint.height());

        QImage canvas(option.rect.size(), QImage::Format_ARGB32);
        canvas.fill(Qt::magenta);

        QPainter painter(&canvas);
        style.drawControl(QStyle::CE_MenuItem, &option, &painter, &menu);
        painter.end();

        // Far right is past the label, so the rule is there.
        QCOMPARE(
            canvas.pixelColor(option.rect.width() - 6, option.rect.height() / 2),
            QColor(QStringLiteral("#00ff00"))
        );
    }
};

QTEST_MAIN(TestMenuGeometry)
#include "MenuGeometry.moc"
