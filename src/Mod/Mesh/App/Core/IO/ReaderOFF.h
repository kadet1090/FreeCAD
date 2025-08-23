// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2024 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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


#ifndef MESH_IO_READER_OFF_H
#define MESH_IO_READER_OFF_H

#include <Mod/Mesh/MeshGlobal.h>
#include <iosfwd>

namespace Base
{
class InputStream;
}

namespace MeshCore
{

class MeshKernel;
struct Material;

/** Loads the mesh object from data in OFF format. */
class MeshExport ReaderOFF
{
public:
    /*!
     * \brief ReaderSTL
     */
    explicit ReaderOFF(MeshKernel& kernel, Material* = nullptr);
    /*!
     * \brief Load the mesh from the input stream
     * \return true on success and false otherwise
     */
    bool Load(std::istream& input);

private:
    MeshKernel& _kernel;
    Material* _material;
};

}  // namespace MeshCore


#endif  // MESH_IO_READER_OFF_H
