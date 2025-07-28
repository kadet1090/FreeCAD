// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2025 Werner Mayer <wmayer[at]users.sourceforge.net>     *
 *                                                                         *
 *   This file is part of FreeCAD.                                         *
 *                                                                         *
 *   FreeCAD is free software: you can redistribute it and/or modify it    *
 *   under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1 of the  *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   FreeCAD is distributed in the hope that it will be useful, but        *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU      *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with FreeCAD. If not, see                               *
 *   <https://www.gnu.org/licenses/>.                                      *
 *                                                                         *
 **************************************************************************/


#include "PreCompiled.h"
#ifndef _PreComp_
#include <Inventor/events/SoKeyboardEvent.h>
#include <Inventor/events/SoMouseButtonEvent.h>
#include <QApplication>
#endif

#include "SelectionCallbackHandler.h"
#include <Gui/Application.h>
#include <Gui/Command.h>
#include <Gui/BitmapFactory.h>


using namespace Gui;

void SelectionCallbackHandler::Create(View3DInventorViewer* viewer,
                                      View3DInventorViewer::SelectionMode selectionMode,
                                      const QCursor& cursor,
                                      FnCb doFunction,
                                      void* ud)
{
    if (currentSelectionHandler) {
        Base::Console().Message("SelectionCallbackHandler: A selection handler already active.");
        return;
    }

    currentSelectionHandler = std::make_unique<SelectionCallbackHandler>();
    if (viewer) {
        currentSelectionHandler->userData = ud;
        currentSelectionHandler->fnCb = doFunction;
        currentSelectionHandler->prevSelectionCursor = viewer->cursor();
        viewer->setEditingCursor(cursor);
        viewer->addEventCallback(SoEvent::getClassTypeId(),
                                 SelectionCallbackHandler::selectionCallback,
                                 currentSelectionHandler.get());
        currentSelectionHandler->prevSelectionEn = viewer->isSelectionEnabled();
        viewer->setSelectionEnabled(false);
        viewer->startSelection(selectionMode);
    }
}

void SelectionCallbackHandler::selectionCallback(void* ud, SoEventCallback* n)
{
    auto selectionHandler = static_cast<SelectionCallbackHandler*>(ud);
    auto view = static_cast<Gui::View3DInventorViewer*>(n->getUserData());
    const SoEvent* ev = n->getEvent();
    if (ev->isOfType(SoKeyboardEvent::getClassTypeId())) {

        n->setHandled();
        n->getAction()->setHandled();

        const auto ke = static_cast<const SoKeyboardEvent*>(ev);
        const SbBool press = ke->getState() == SoButtonEvent::DOWN;
        if (ke->getKey() == SoKeyboardEvent::ESCAPE) {

            if (!press) {
                view->abortSelection();
                restoreState(selectionHandler, view);
            }
        }
    }
    else if (ev->isOfType(SoMouseButtonEvent::getClassTypeId())) {
        const auto mbe = static_cast<const SoMouseButtonEvent*>(ev);

        // Mark all incoming mouse button events as handled, especially, to deactivate the selection
        // node
        n->getAction()->setHandled();

        if (mbe->getButton() == SoMouseButtonEvent::BUTTON1
            && mbe->getState() == SoButtonEvent::UP) {
            if (selectionHandler && selectionHandler->fnCb) {
                selectionHandler->fnCb(selectionHandler->getUserData(), n);
            }
            restoreState(selectionHandler, view);
        }
        // No other mouse events available from Coin3D to implement right mouse up abort
    }
}

void SelectionCallbackHandler::restoreState(SelectionCallbackHandler* selectionHandler,
                                            View3DInventorViewer* view)
{
    if (selectionHandler) {
        selectionHandler->fnCb = nullptr;
        view->setEditingCursor(selectionHandler->prevSelectionCursor);
        view->removeEventCallback(SoEvent::getClassTypeId(),
                                  SelectionCallbackHandler::selectionCallback,
                                  selectionHandler);
        view->setSelectionEnabled(selectionHandler->prevSelectionEn);
    }

    Application::Instance->commandManager().testActive();
    currentSelectionHandler = nullptr;
}

QCursor SelectionCallbackHandler::makeCursor(QWidget* widget,
                                             const QSize& size,
                                             const char* svgFile,
                                             int hotX,
                                             int hotY)
{
    qreal pRatio = widget->devicePixelRatioF();
    qreal hotXF = hotX;
    qreal hotYF = hotY;
#if !defined(Q_OS_WIN32) && !defined(Q_OS_MACOS)
    if (qApp->platformName() == QLatin1String("xcb")) {
        hotXF *= pRatio;
        hotYF *= pRatio;
    }
#endif
    qreal cursorWidth = size.width() * pRatio;
    qreal cursorHeight = size.height() * pRatio;
    QPixmap px(Gui::BitmapFactory().pixmapFromSvg(svgFile, QSizeF(cursorWidth, cursorHeight)));
    px.setDevicePixelRatio(pRatio);
    return QCursor(px, hotXF, hotYF);
}

std::unique_ptr<SelectionCallbackHandler> SelectionCallbackHandler::currentSelectionHandler =
    std::unique_ptr<SelectionCallbackHandler>();
