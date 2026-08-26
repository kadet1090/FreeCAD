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

#include <QList>
#include <QMargins>
#include <QPointer>
#include <QRect>
#include <QWidget>

#include <FCGlobal.h>

namespace Gui
{

/**
 * Marks one widget inside a host with a halo, without altering that widget.
 *
 * The halo is painted from the HighlightOverlay* style tokens, so it follows the theme, and the
 * marked widget is only ever read. Nothing about its own styling changes, so it stays painted
 * by FreeCADStyle for as long as it is marked.
 *
 * The halo may reach one Margin beyond the host, so a control sitting flush against the host's
 * edge is still ringed rather than clipped. Whether that overhang is visible depends on there
 * being room around the host; where there is none it is clipped away, as it would have been
 * anyway.
 */
class GuiExport HighlightOverlay: public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief Marks widgets inside @p host, painting above everything in @p host's window.
     *
     * A scroll area marks widgets inside its viewport and follows its scrolling; any other host
     * marks widgets inside itself. Starts with no target.
     */
    explicit HighlightOverlay(QWidget* host);

    /// Marks @p target, or clears the highlight when it is nullptr.
    void setTarget(QWidget* target);

    /// The widget currently marked, or nullptr.
    QWidget* target() const;

    /**
     * @brief @p target's rect in @p surface coordinates, grown by @p margins.
     *
     * Returns a null rect whenever there is nothing to mark: no target, no surface, a target
     * that does not live inside @p surface, or one an ancestor hides — the inactive page of a
     * QStackedWidget, for instance.
     */
    static QRect highlightRect(const QWidget* target, const QWidget* surface, QMargins margins);

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    /// Starts watching the target and every ancestor of it up to the surface.
    void watchTarget();

    /// Undoes watchTarget(), tolerating anything that has since been destroyed.
    void unwatchTarget();

    QWidget* _surface;
    QPointer<QWidget> _target;
    QList<QPointer<QWidget>> _watched;
    QMetaObject::Connection _targetDestroyed;
};

}  // namespace Gui
