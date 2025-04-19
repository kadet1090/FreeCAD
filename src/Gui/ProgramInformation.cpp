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
#include <string>
#include <QApplication>
#include <QScreen>
#include <QStyle>
#endif

#include <App/Application.h>
#include "ProgramInformation.h"

using namespace Gui;

void ProgramInformation::getStyleInformation(std::stringstream& str)
{
    // Add Stylesheet/Theme/Qtstyle information
    std::string styleSheet =
        App::GetApplication()
            .GetParameterGroupByPath("User parameter:BaseApp/Preferences/MainWindow")
            ->GetASCII("StyleSheet");
    std::string theme =
        App::GetApplication()
            .GetParameterGroupByPath("User parameter:BaseApp/Preferences/MainWindow")
            ->GetASCII("Theme");
#if QT_VERSION >= QT_VERSION_CHECK(6, 1, 0)
    std::string style = qApp->style()->name().toStdString();
#else
    std::string style =
        App::GetApplication()
            .GetParameterGroupByPath("User parameter:BaseApp/Preferences/MainWindow")
            ->GetASCII("QtStyle");
    if (style.empty()) {
        style = "Qt default";
    }
#endif
    if (styleSheet.empty()) {
        styleSheet = "unset";
    }
    if (theme.empty()) {
        theme = "unset";
    }

    str << "Stylesheet/Theme/QtStyle: " << styleSheet << "/" << theme << "/" << style << "\n";
}

void ProgramInformation::getDpiInformation(std::stringstream& str)
{
    // Add DPI information
    str << "Logical DPI/Physical DPI/Pixel Ratio: "
        << QApplication::primaryScreen()->logicalDotsPerInch()
        << "/"
        << QApplication::primaryScreen()->physicalDotsPerInch()
        << "/"
        << QApplication::primaryScreen()->devicePixelRatio()
        << "\n";
}
