// SPDX-License-Identifier: LGPL-2.1-or-later
/****************************************************************************
 *                                                                          *
 *   Copyright (c) 2024 Kacper Donat <kacper@kadet.net>                     *
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

#ifndef GUI_INPUTHINT_H
#define GUI_INPUTHINT_H

#include "FCGlobal.h"

#include <Qt>
#include <QString>

#include <list>
#include <variant>

namespace Gui
{

enum class MouseInput
{
    // Mouse Keys
    MouseMove = 1 << 16,
    MouseLeft = 2 << 16,
    MouseRight = 3 << 16,
    MouseMiddle = 4 << 16,
    MouseScroll = 5 << 16,
    MouseScrollUp = 6 << 16,
    MouseScrollDown = 7 << 16,
};

struct InputHint
{
    using UserInput = std::variant<Qt::KeyboardModifier, Qt::Key, MouseInput>;
    struct InputSequence
    {
        std::list<UserInput> keys {};

        InputSequence(UserInput key)
            : InputSequence({key})
        {}

        explicit InputSequence(std::list<UserInput> keys)
            : keys(std::move(keys))
        {}

        InputSequence(const std::initializer_list<UserInput> keys)
            : keys(keys)
        {}
    };

    QString message;
    std::list<InputSequence> sequences;
};

}  // namespace Gui

#endif // GUI_INPUTHINT_H
