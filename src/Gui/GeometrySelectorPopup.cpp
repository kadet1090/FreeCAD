// SPDX-License-Identifier: LGPL-2.1-or-later
/****************************************************************************
 *                                                                          *
 *   Copyright (c) 2026 Kacper Donat <kacper@kadet.net>                     *
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

#include "GeometrySelectorPopup.h"

#include <algorithm>

#include <QEvent>
#include <QHideEvent>
#include <QKeyEvent>
#include <QListView>
#include <QMouseEvent>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include "Application.h"
#include "FreeCADStyle.h"

using namespace Gui;

GeometrySelectorPopup::GeometrySelectorPopup(
    std::vector<GeometrySelectorOption> options,
    std::vector<GeometrySelectorOption> history,
    bool allowCustom,
    int currentIndex,
    QWidget* parent
)
    : QFrame(parent, Qt::Popup)
    , m_options(std::move(options))
    , m_history(std::move(history))
    , m_allowCustom(allowCustom)
    , m_currentIndex(currentIndex)
{
    setObjectName(QStringLiteral("gsw_options_popup"));
    // The frame style a combo popup takes, which is what routes the surface through PE_Frame
    // and the contents inset through SE_ShapedFrameContents.
    setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
    // Any close() — Escape or an outside click — schedules deletion, so a dismissed popup
    // never lingers as a hidden child of the widget.
    setAttribute(Qt::WA_DeleteOnClose);

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);
    // The default constraint pins this top-level widget's minimum height to the layout's — once
    // Qt has activated it that floor never comes back down, which silently defeats the style's
    // own trim: a capped, scrolled popup can no longer shrink to a whole number of rows and ends
    // with a sliver of the next one showing (see FreeCADStyle::snapComboPopupToWholeRows).
    outerLayout->setSizeConstraint(QLayout::SetNoConstraint);

    m_view = new QListView(this);
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Qt sets no WA_Hover on an item view's viewport, so a row is only told the pointer is over
    // it once the view sees plain moves — the same line Tree and the property editor carry.
    // Redundant while FreeCADStyle is the one painting: its polish() sets WA_MouseTracking on
    // every QAbstractItemView and QAbstractScrollArea::event() forwards that to the viewport.
    // It is kept for the case that does not hold — an ambient style that is not FreeCADStyle,
    // where nothing else turns tracking on and the rows would never see a hover.
    m_view->viewport()->setMouseTracking(true);
    m_view->viewport()->installEventFilter(this);
    m_view->installEventFilter(this);
    outerLayout->addWidget(m_view);

    // Keyboard reaches the view rather than the popup, so QListView's own navigation applies.
    // setFocus() is what actually resolves through the proxy onto the view; without it the
    // popup itself stays the focus widget and the view never sees a key.
    setFocusProxy(m_view);
    setFocus();

    buildModel();
    adoptAsDropdown();

    connect(m_view, &QAbstractItemView::clicked, this, [this](const QModelIndex& index) {
        // A valid clicked() index is always in [0, rowCount()) == [0, m_rowToIndex.size()), so
        // the subscript is safe even for a rule's row — that entry is -1, and activateIndex()
        // rejects it.
        activateIndex(m_rowToIndex.at(index.row()));
    });
}

int GeometrySelectorPopup::optionCount() const
{
    return static_cast<int>(m_options.size() + m_history.size()) + (m_allowCustom ? 1 : 0);
}

void GeometrySelectorPopup::buildModel()
{
    m_model = new QStandardItemModel(this);
    m_rowToIndex.clear();

    int index = 0;
    for (const GeometrySelectorOption& option : m_options) {
        appendOptionRow(option, index++);
    }

    if (!m_history.empty()) {
        appendSeparatorRow();
        for (const GeometrySelectorOption& entry : m_history) {
            appendOptionRow(entry, index++);
        }
    }

    if (m_allowCustom) {
        appendSeparatorRow();
        appendOptionRow(GeometrySelectorOption::customEntry(), index++);
    }

    m_view->setModel(m_model);

    // Selects as well as moves the cursor, which is what paints the chosen entry and what the
    // "current" placement measures its offset from. The row, not the index — a rule above the
    // entry shifts one and not the other.
    if (const int row = rowForIndex(m_currentIndex); row >= 0) {
        m_view->setCurrentIndex(m_model->index(row, 0));
    }
}

void GeometrySelectorPopup::appendOptionRow(const GeometrySelectorOption& option, int index)
{
    auto* item = new QStandardItem(option.label);
    item->setIcon(option.icon);
    item->setEditable(false);
    m_model->appendRow(item);
    m_rowToIndex.push_back(index);
}

void GeometrySelectorPopup::appendSeparatorRow()
{
    // A rule needs a group on both sides. The groups above are the only ones that can be empty,
    // so an empty model is the whole of that test.
    if (m_model->rowCount() == 0) {
        return;
    }

    auto* item = new QStandardItem();
    // The marker QComboBox::insertSeparator() writes, which is what FreeCADStyle reads the row
    // back as. Clearing the flags is what makes QListView::moveCursor step over the row.
    item->setData(QStringLiteral("separator"), Qt::AccessibleDescriptionRole);
    item->setFlags(item->flags() & ~(Qt::ItemIsSelectable | Qt::ItemIsEnabled));
    m_model->appendRow(item);
    m_rowToIndex.push_back(-1);
}

int GeometrySelectorPopup::rowForIndex(int index) const
{
    const auto found = std::ranges::find(m_rowToIndex, index);
    return index < 0 || found == m_rowToIndex.end()
        ? -1
        : static_cast<int>(std::distance(m_rowToIndex.begin(), found));
}

void GeometrySelectorPopup::adoptAsDropdown()
{
    if (!Application::Instance) {
        return;  // headless: the popup still builds, navigates and activates
    }
    FreeCADStyle* style = Application::Instance->freeCADStyle();
    setStyle(style);
    // setStyle() does not propagate to children — QWidget::style() answers the widget's own
    // style or qApp's, never a parent's — so the view has to be told separately or the frame
    // and the rows inside it are painted by two different styles whenever the user's QtStyle
    // preference is not FreeCAD (the Classic pack, for one, removes it).
    m_view->setStyle(style);
    // The row, because that is what the style aligns the popup against, and a rule above the
    // chosen entry shifts the row without shifting the index.
    style->constrainDropdown(m_view, rowForIndex(m_currentIndex));
}

QSize GeometrySelectorPopup::sizeHint() const
{
    // Adding the view to the layout activates it, and an activating layout asks for this before
    // the model exists.
    if (!m_model) {
        return QFrame::sizeHint();
    }

    // QListView reports a fixed default extent, so the popup measures its own rows instead.
    // The frame's margins are the styled inset QFrame derives from SE_ShapedFrameContents.
    const QMargins frame = contentsMargins();

    int rowsHeight = 0;
    for (int row = 0; row < m_model->rowCount(); ++row) {
        rowsHeight += m_view->sizeHintForRow(row);
    }

    return {
        m_view->sizeHintForColumn(0) + frame.left() + frame.right(),
        rowsHeight + frame.top() + frame.bottom()
    };
}

void GeometrySelectorPopup::activateIndex(int index)
{
    if (index < 0 || index >= optionCount()) {
        return;
    }
    Q_EMIT optionActivated(index);
}

void GeometrySelectorPopup::setHoveredIndex(int index)
{
    if (index == m_hoveredIndex) {
        return;
    }
    m_hoveredIndex = index;
    Q_EMIT optionHovered(index);
}

void GeometrySelectorPopup::leaveEvent(QEvent* event)
{
    setHoveredIndex(-1);
    QFrame::leaveEvent(event);
}

void GeometrySelectorPopup::hideEvent(QHideEvent* event)
{
    // Escape, an outside click and an activation all end here, so this is the one place a
    // dismissed dropdown is guaranteed to withdraw its highlight.
    setHoveredIndex(-1);
    QFrame::hideEvent(event);
}

bool GeometrySelectorPopup::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_view->viewport() && event->type() == QEvent::MouseMove) {
        // The row under the pointer becomes the cursor, so Enter picks what the pointer is on —
        // the behaviour QComboBoxPrivateContainer gives a combo box's popup. A rule is disabled,
        // so QAbstractItemView::setCurrentIndex() already refuses it on its own; the cursor
        // stays where it was and Enter still activates something.
        const auto* move = static_cast<QMouseEvent*>(event);
        const QModelIndex under = m_view->indexAt(move->position().toPoint());
        if (under.isValid()) {
            m_view->setCurrentIndex(under);
        }
        // The cursor cannot land on a rule, but the hover can: it reports what the pointer is
        // actually over, and below the last row it is over nothing.
        setHoveredIndex(under.isValid() ? m_rowToIndex.at(under.row()) : -1);
        return false;
    }

    if (watched == m_view && event->type() == QEvent::KeyPress
        && handleViewKeyPress(static_cast<QKeyEvent*>(event))) {
        return true;
    }

    return QFrame::eventFilter(watched, event);
}

/// Handles the keys a dropdown must answer for itself. Returns whether @p event was consumed;
/// an unconsumed key falls back to the base class through the caller.
bool GeometrySelectorPopup::handleViewKeyPress(QKeyEvent* event)
{
    switch (event->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (const QModelIndex current = m_view->currentIndex(); current.isValid()) {
                activateIndex(m_rowToIndex.at(current.row()));
            }
            return true;
        case Qt::Key_Escape:
            close();
            return true;
        default:
            return false;
    }
}

void GeometrySelectorPopup::mousePressEvent(QMouseEvent* event)
{
    // A Qt::Popup grabs the mouse; a press outside its geometry dismisses it. WA_DeleteOnClose
    // then frees the popup, so both the outside-click and Escape paths release it.
    if (!rect().contains(event->pos())) {
        close();
        return;
    }
    QFrame::mousePressEvent(event);
}
