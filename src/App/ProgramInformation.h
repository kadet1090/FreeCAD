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

#ifndef APP_PROGRAM_INFORMATION_H
#define APP_PROGRAM_INFORMATION_H

#include <FCGlobal.h>
#include <map>
#include <string>
#include <sstream>

namespace App
{

using Config = std::map<std::string, std::string>;

class BaseExport ProgramInformation
{
public:
    static std::string prettyProductInfoWrapper();
    static void addModuleInfo(std::stringstream& str, const std::string& path);
    static void getVerboseCommonInfo(std::stringstream& str, const Config& mConfig);
    static void getVerboseAddOnsInfo(std::stringstream& str, const Config& mConfig);
    static constexpr const char* verboseVersionEmitMessage{"verbose_version"};

private:
    static void getSystemInformation(std::stringstream& str);
    static void getVersionInformation(const Config& mConfig, std::stringstream& str);
    static void getBuildInformation(const Config& mConfig, std::stringstream& str);
    static void getPackageInformation(std::stringstream& str);
    static void getLibraryVersions(std::stringstream& str);
    static void getIfcInfo(std::stringstream& str);
    static void getLocale(std::stringstream& str);
    static std::string getValueOrEmpty(const Config& map, const std::string& key);
};

}  // namespace App

#endif  // APP_PROGRAM_INFORMATION_H
