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

#include "HighlightOverlay.h"

#include <QAbstractScrollArea>
#include <QEvent>
#include <QPainter>
#include <QScrollBar>

#include <Gui/Application.h>
#include <Gui/FreeCADStyle.h>

using namespace Gui;

namespace
{
/// The widget whose coordinates and visible bounds the halo is measured against: a scroll area
/// marks widgets inside its viewport, anything else marks widgets inside itself.
QWidget* surfaceOf(QWidget* host)
{
    if (auto* area = qobject_cast<QAbstractScrollArea*>(host)) {
        return area->viewport();
    }

    return host;
}
}  // namespace

HighlightOverlay::HighlightOverlay(QWidget* host)
    // Parented to the window rather than the surface so the halo can reach past the surface's
    // edge into whatever room the layout leaves around it. paintEvent clips it back.
    : QWidget(host->window())
    , _surface(surfaceOf(host))
{
    // Never take a click: the controls under the halo stay fully usable.
    setAttribute(Qt::WA_TransparentForMouseEvents);

    // Resolves the HighlightOverlay* tokens through FreeCADStyle's custom-namespace mechanism.
    setProperty("component", "HighlightOverlay");

    setGeometry(parentWidget()->rect());
    hide();

    parentWidget()->installEventFilter(this);
    _surface->installEventFilter(this);

    if (auto* area = qobject_cast<QAbstractScrollArea*>(host)) {
        // The overlay stands still while the content slides underneath it.
        connect(
            area->horizontalScrollBar(),
            &QScrollBar::valueChanged,
            this,
            qOverload<>(&QWidget::update)
        );
        connect(area->verticalScrollBar(), &QScrollBar::valueChanged, this, qOverload<>(&QWidget::update));
    }
}

QWidget* HighlightOverlay::target() const
{
    return _target;
}

void HighlightOverlay::setTarget(QWidget* target)
{
    if (_target == target) {
        return;
    }

    unwatchTarget();

    _target = target;

    if (_target) {
        watchTarget();

        // The overlay is added to the window after everything the layout already put there, so
        // it has to be lifted above its siblings.
        raise();
    }

    setVisible(_target != nullptr);
    update();
}

void HighlightOverlay::watchTarget()
{
    // Qt sends no Move to a widget when an ancestor moves, so watching the target alone would
    // leave the halo stale after any relayout between it and the surface.
    for (QWidget* ancestor = _target; ancestor != nullptr && ancestor != _surface;
         ancestor = ancestor->parentWidget()) {
        ancestor->installEventFilter(this);
        _watched.append(ancestor);
    }

    _targetDestroyed = connect(_target, &QObject::destroyed, this, [this] { setTarget(nullptr); });
}

void HighlightOverlay::unwatchTarget()
{
    for (const QPointer<QWidget>& watched : _watched) {
        if (watched) {
            watched->removeEventFilter(this);
        }
    }
    _watched.clear();

    // Held as a Connection rather than disconnected by signature: the pointer-to-member
    // disconnect overload cannot take a null method, and a connection left over from a
    // previous target would clear the current halo when that old widget is eventually deleted.
    disconnect(_targetDestroyed);
}

QRect HighlightOverlay::highlightRect(const QWidget* target, const QWidget* surface, QMargins margins)
{
    if (target == nullptr || surface == nullptr) {
        return {};
    }

    // isVisibleTo answers "would this show if the surface were on screen", which is what the
    // halo needs: a target parked on an inactive stacked page has nothing to mark, but one on
    // the live page does even before the dialog is first shown.
    if (!surface->isAncestorOf(target) || !target->isVisibleTo(surface)) {
        return {};
    }

    const QRect inSurface {target->mapTo(surface, QPoint(0, 0)), target->size()};

    return inSurface.marginsAdded(margins);
}

void HighlightOverlay::paintEvent(QPaintEvent* /*event*/)
{
    if (Application::Instance == nullptr) {
        return;
    }

    auto* style = Application::Instance->freeCADStyle();

    const StyleParameters::StyleContext context = FreeCADStyle::contextOf(this);
    const QMargins halo = style->resolveBoxGeometry(context).margin.toMargins();

    const QRect borderBox = highlightRect(_target, _surface, halo);
    if (borderBox.isNull()) {
        return;
    }

    const QPoint origin = _surface->mapTo(parentWidget(), QPoint(0, 0));

    QPainter painter(this);

    // One margin past the surface and no further: enough that a control flush against the edge
    // is still ringed, while a target scrolled out of view cannot smear across the window.
    painter.setClipRect(QRect(origin, _surface->size()).marginsAdded(halo));

    // paintBox insets what it is handed by this same Margin, so hand it a rect one margin
    // larger than the border box actually wanted.
    style->paintBox(&painter, borderBox.translated(origin).marginsAdded(halo), context);
}

bool HighlightOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        setGeometry(parentWidget()->rect());
    }

    switch (event->type()) {
        case QEvent::Move:
        case QEvent::Resize:
        case QEvent::Show:
        case QEvent::Hide:
            update();
            break;
        default:
            break;
    }

    return QWidget::eventFilter(watched, event);
}
