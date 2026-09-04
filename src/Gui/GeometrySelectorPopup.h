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

#pragma once

#include <vector>

#include <QFrame>

#include <FCGlobal.h>

#include "GeometrySelectorWidget.h"  // GeometrySelectorOption

class QListView;
class QStandardItemModel;
class QHideEvent;

namespace Gui
{

/**
 * The geometry selector's dropdown: one selectable row per predefined option, then one per recent
 * history entry, then an optional trailing "Custom…" row, each group divided from its neighbours
 * by a rule, painted and placed as any other dropdown. A top-level Qt::Popup positioned under the
 * control by the caller. Emits optionActivated(index) on mouse click or keyboard activation; the
 * index space is predefined options, then history, then Custom last.
 */
class GuiExport GeometrySelectorPopup: public QFrame
{
    Q_OBJECT

public:
    GeometrySelectorPopup(
        std::vector<GeometrySelectorOption> options,
        std::vector<GeometrySelectorOption> history,
        bool allowCustom,
        int currentIndex,
        QWidget* parent = nullptr
    );

    /// Number of selectable entries: predefined options, history and the Custom row when enabled.
    int optionCount() const;
    /// Validates @p index and emits optionActivated; ignored when out of range.
    void activateIndex(int index);

    /// The extent every row needs, so a caller can size the popup before showing it.
    QSize sizeHint() const override;

Q_SIGNALS:
    void optionActivated(int index);
    /// The option index under the pointer, or -1 when it names nothing — a rule, empty
    /// space below the rows, the pointer leaving, or the popup hiding.
    void optionHovered(int index);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void buildModel();
    void adoptAsDropdown();
    bool handleViewKeyPress(QKeyEvent* event);
    /// Appends one selectable row for @p option, recording the index it activates.
    void appendOptionRow(const GeometrySelectorOption& option, int index);
    /// Appends a rule, unless the model is empty — a rule needs a group on both sides.
    void appendSeparatorRow();
    /// The model row @p index occupies, or -1 when it is out of range.
    int rowForIndex(int index) const;
    /// Publishes @p index as hovered unless it is already what was published, so a pointer
    /// moving within one row does not rebuild the 3D highlight on every move.
    void setHoveredIndex(int index);

    std::vector<GeometrySelectorOption> m_options;
    std::vector<GeometrySelectorOption> m_history;
    bool m_allowCustom;
    int m_currentIndex;
    QListView* m_view = nullptr;
    QStandardItemModel* m_model = nullptr;
    /// The index each model row activates, -1 for a separator. Row and index stop being the
    /// same number as soon as a rule sits between two groups.
    std::vector<int> m_rowToIndex;
    /// The last index handed to optionHovered().
    int m_hoveredIndex = -1;
};

}  // namespace Gui
