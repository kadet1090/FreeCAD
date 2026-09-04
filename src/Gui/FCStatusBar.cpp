// SPDX-License-Identifier: LGPL-2.1-or-later
#include "FCStatusBar.h"

#include <algorithm>
#include <optional>

#include <QBoxLayout>
#include <QEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QSizeGrip>
#include <QStyleOption>

#include "FreeCADStyle.h"

namespace Gui
{

namespace
{
// Qt lays the message out between these bounds, and keeps two pixels clear of the first item
// beside it — of the size grip it keeps none. Mirrored here because QStatusBarPrivate::
// messageRect() is private.
constexpr int messageLeadingMargin = 6;
constexpr int messageTrailingMargin = 12;
constexpr int messageItemGap = 2;

/// The layout the bar's items were put into, or nothing while the bar is holding none.
QLayout* itemLayoutOf(QWidget* bar)
{
    for (QLayout* candidate : bar->findChildren<QLayout*>()) {
        // The outer layout holds the size grip, so it answers this question wrongly.
        if (candidate == bar->layout()) {
            continue;
        }

        for (int index = 0; index < candidate->count(); ++index) {
            if (candidate->itemAt(index)->widget() != nullptr) {
                return candidate;
            }
        }
    }

    return nullptr;
}

/// Whether @p layout holds the bar's size grip, whose own gap is not part of the bar's inset.
bool holdsSizeGrip(const QLayout* layout)
{
    for (int index = 0; index < layout->count(); ++index) {
        if (qobject_cast<QSizeGrip*>(layout->itemAt(index)->widget()) != nullptr) {
            return true;
        }
    }

    return false;
}

/**
 * @brief Drops the spacing reformat() built into @p layout, keeping what carries meaning.
 *
 * The stretch parting the two item groups expands and is left alone. The strut is fixed like
 * the spacing is, and holds the bar to the height of its tallest item, but Qt builds it across
 * the layout's direction rather than along it — which is what tells the two apart.
 */
void removeFixedSpacing(QBoxLayout* layout)
{
    const QBoxLayout::Direction direction = layout->direction();
    const bool horizontal = direction == QBoxLayout::LeftToRight
        || direction == QBoxLayout::RightToLeft;

    for (int index = layout->count() - 1; index >= 0; --index) {
        QLayoutItem* item = layout->itemAt(index);
        QSpacerItem* spacer = item->spacerItem();

        if (spacer == nullptr || item->expandingDirections() != Qt::Orientations {}) {
            continue;
        }

        const QSize extent = spacer->sizeHint();

        if ((horizontal ? extent.width() : extent.height()) > 0) {
            delete layout->takeAt(index);
        }
    }
}

/// Drops it from every layout below @p bar whose spacing a stated inset and gap are replacing.
void removeFixedSpacing(QWidget* bar)
{
    // Every layout in the tree, not just these two: Qt nests a vertical layout between them and
    // spends the bar's vertical inset there. Whichever one holds the size grip is skipped: with
    // the grip enabled that is the outer layout and its gap belongs to the grip, not the bar's
    // inset; with the grip disabled Qt builds no wrapper at all and the outer layout is the one
    // whose spacers the token is meant to replace, so this test has to be made per layout rather
    // than assumed to be the outer one.
    for (QBoxLayout* box : bar->findChildren<QBoxLayout*>()) {
        if (holdsSizeGrip(box)) {
            continue;
        }

        removeFixedSpacing(box);
    }
}
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
    // is what the message has to stay clear of. This is the same bound Qt's own first-permanent-
    // item check would find, since status-bar items are inserted in geometric order.
    for (const QObject* child : children()) {
        const auto* item = qobject_cast<const QWidget*>(child);

        if (item == nullptr || !item->isVisible()) {
            continue;
        }

        const int gap = qobject_cast<const QSizeGrip*>(item) == nullptr ? messageItemGap : 0;

        if (rightToLeft) {
            left = std::max(left, item->x() + item->width() + gap);
        }
        else {
            right = std::min(right, item->x() - gap);
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

void FCStatusBar::applyLayoutTokens()
{
    QLayout* outer = layout();

    // reformat() builds a new layout every time, so the one already adjusted is the one to leave
    // alone: writing its margins again would invalidate it and post the request that called this.
    if (outer == nullptr || _adjustedLayout == outer) {
        return;
    }

    const std::optional<QMargins> padding
        = FreeCADStyle::stylePadding(this, StyleParameters::StyleComponentElement::Root);
    const std::optional<int> spacing
        = FreeCADStyle::styleSpacing(this, StyleParameters::StyleComponentElement::Item);
    const std::optional<int> height
        = FreeCADStyle::styleHeight(this, StyleParameters::StyleComponentElement::Root);

    // A theme that speaks for none of the three gets Qt's layout and Qt's own bounds exactly as
    // Qt left them: a bar no theme describes has to look the way it looked before this component
    // existed. What an earlier theme imposed is handed back rather than left standing, so that
    // changing theme is not a way of keeping the height of the theme before it.
    if (!padding && !spacing && !height) {
        releaseHeight();
        return;
    }

    _adjustedLayout = outer;

    if (padding || spacing) {
        removeFixedSpacing(this);
    }

    if (padding) {
        outer->setContentsMargins(*padding);
    }

    if (spacing) {
        if (QLayout* itemLayout = itemLayoutOf(this)) {
            itemLayout->setSpacing(*spacing);
        }
    }

    applyHeight(height);
}

void FCStatusBar::releaseHeight()
{
    setMaximumHeight(QWIDGETSIZE_MAX);
    setMinimumHeight(0);
}

void FCStatusBar::applyHeight(std::optional<int> stated)
{
    if (stated) {
        // Held at both ends rather than floored. Qt sizes the bar to its tallest item, and which
        // item that is depends on the profile: a workbench's toolbar docked into the bar, a
        // widget a module added, an item still hidden. A bar stated to be one height has to be
        // that height whoever is standing in it.
        setFixedHeight(*stated);
        return;
    }

    // The main window sizes the bar from its minimum and never from its size hint, so an inset
    // written above reaches the window only by being counted in here. The ceiling a previous
    // theme set is lifted first: a theme that states no height leaves the bar sizing itself.
    setMaximumHeight(QWIDGETSIZE_MAX);
    setMinimumHeight(minimumSizeHint().height());
}

bool FCStatusBar::event(QEvent* event)
{
    // Qt answers a layout request by rebuilding the layout, so the tokens go onto whatever it
    // leaves behind rather than onto the layout that is about to be thrown away.
    const bool handled = QStatusBar::event(event);

    // A theme reload changes what the tokens resolve to without rebuilding anything, so the
    // layout that was adjusted has to stop counting as adjusted.
    if (event->type() == QEvent::StyleChange) {
        _adjustedLayout = nullptr;
    }

    if (event->type() == QEvent::LayoutRequest || event->type() == QEvent::StyleChange) {
        applyLayoutTokens();
    }

    return handled;
}

}  // namespace Gui
