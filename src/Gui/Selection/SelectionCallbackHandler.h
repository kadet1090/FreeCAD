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


#ifndef GUI_SELECTION_CALLBACK_HANDLER_H
#define GUI_SELECTION_CALLBACK_HANDLER_H

#include <memory>
#include <QCursor>
#include <Gui/View3DInventorViewer.h>


namespace Gui
{

class GuiExport SelectionCallbackHandler
{

private:
    static std::unique_ptr<SelectionCallbackHandler> currentSelectionHandler;
    QCursor prevSelectionCursor;
    using FnCb = void (*)(void* userdata, SoEventCallback* node);
    FnCb fnCb;
    void* userData;
    bool prevSelectionEn;

public:
    // Creates a selection handler used to implement the common behaviour of BoxZoom, BoxSelection
    // and BoxElementSelection.
    // Takes the viewer, a selection mode, a cursor, a function pointer to be called on success and
    // a void pointer for user data to be passed to the given function.
    // The selection handler class stores all necessary previous states, registers a event callback
    // and starts the selection in the given mode.
    // If there is still a selection handler active, this call will generate a message and returns.
    static void Create(View3DInventorViewer* viewer,
                       View3DInventorViewer::SelectionMode selectionMode,
                       const QCursor& cursor,
                       FnCb doFunction = nullptr,
                       void* ud = nullptr);

    void* getUserData() const
    {
        return userData;
    }

    // Implements the event handler. In the normal case the provided function is called.
    // Also supports aborting the selection mode by pressing (releasing) the Escape key.
    static void selectionCallback(void* ud, SoEventCallback* n);

    static void restoreState(SelectionCallbackHandler* selectionHandler,
                             View3DInventorViewer* view);
    static QCursor
    makeCursor(QWidget* widget, const QSize& size, const char* svgFile, int hotX, int hotY);
};
}  // namespace Gui

#endif  // GUI_SELECTION_CALLBACK_HANDLER_H
