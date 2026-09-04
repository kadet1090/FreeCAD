// SPDX-License-Identifier: LGPL-2.1-or-later
#include "FCMenuBar.h"

#include <QActionEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QStyleOption>
#include <QStyleOptionMenuItem>

namespace Gui
{

FCMenuBar::FCMenuBar(QWidget* parent)
    : QMenuBar(parent)
{
    setContentsMargins({});
}

void FCMenuBar::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);

    // 1. Draw the bar background and bottom border for the full bar rect.
    //    CE_MenuBarEmptyArea normally only covers the area not occupied by items;
    //    here we use the full rect and let the style draw the complete background.
    {
        QStyleOption barOption;
        barOption.initFrom(this);
        barOption.rect = rect();
        style()->drawControl(QStyle::CE_MenuBarEmptyArea, &barOption, &painter, this);
    }

    // 2. Draw each action.  Unlike Qt's default paintEvent we do NOT call
    //    painter.setClipRect(actionRect) before each item, so CE_MenuBarItem is
    //    free to paint a centred highlight that is taller than the raw text area.
    //    The device clip (from the paint event region) is already the full bar
    //    height because CT_MenuBarItem expands item rects to widget->height().
    for (QAction* action : actions()) {
        const QRect actionRect = actionGeometry(action);
        if (actionRect.isEmpty() || !event->rect().intersects(actionRect)) {
            continue;
        }

        QStyleOptionMenuItem option;
        initStyleOption(&option, action);
        option.rect = actionRect;

        style()->drawControl(QStyle::CE_MenuBarItem, &option, &painter, this);
    }
}

}  // namespace Gui
