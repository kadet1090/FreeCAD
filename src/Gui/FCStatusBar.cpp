// SPDX-License-Identifier: LGPL-2.1-or-later
#include "FCStatusBar.h"

#include <algorithm>

#include <QPainter>
#include <QPaintEvent>
#include <QStyleOption>

#include "FreeCADStyle.h"

namespace Gui
{

namespace
{
// Qt lays the message out between these bounds, and keeps two pixels clear of the first item
// beside it. Mirrored here because QStatusBarPrivate::messageRect() is private.
constexpr int messageLeadingMargin = 6;
constexpr int messageTrailingMargin = 12;
constexpr int messageItemGap = 2;
}  // namespace

FCStatusBar::FCStatusBar(QWidget* parent)
    : QStatusBar(parent)
{}

void FCStatusBar::setMessageLevel(StyleParameters::MessageLevel level)
{
    if (_messageLevel == level) {
        return;
    }

    _messageLevel = level;
    update();
}

QRect FCStatusBar::messageRect() const
{
    const bool rightToLeft = layoutDirection() == Qt::RightToLeft;

    int left = messageLeadingMargin;
    int right = width() - messageTrailingMargin;

    // While a message shows, Qt hides every item that is not permanent, so what is still visible
    // is what the message has to stay clear of.
    for (const QObject* child : children()) {
        const auto* item = qobject_cast<const QWidget*>(child);

        if (item == nullptr || !item->isVisible()) {
            continue;
        }

        if (rightToLeft) {
            left = std::max(left, item->x() + item->width() + messageItemGap);
        }
        else {
            right = std::min(right, item->x() - messageItemGap);
        }
    }

    return QRect(left, 0, right - left, height());
}

void FCStatusBar::paintEvent(QPaintEvent*)
{
    QPainter painter(this);

    QStyleOption option;
    option.initFrom(this);
    style()->drawPrimitive(QStyle::PE_PanelStatusBar, &option, &painter, this);

    const QString message = currentMessage();

    if (message.isEmpty()) {
        return;
    }

    painter.setPen(
        FreeCADStyle::statusMessageColor(this, _messageLevel, palette().windowText().color())
    );
    painter.drawText(messageRect(), Qt::AlignLeading | Qt::AlignVCenter | Qt::TextSingleLine, message);
}

}  // namespace Gui
