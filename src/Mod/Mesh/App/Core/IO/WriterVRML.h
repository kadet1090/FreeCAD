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


#ifndef MESH_IO_WRITER_VRML_H
#define MESH_IO_WRITER_VRML_H

#include <Base/Matrix.h>
#include <Mod/Mesh/MeshGlobal.h>
#include <iosfwd>

namespace MeshCore
{

struct Material;
class MeshKernel;

/** Saves the mesh object into VRML format. */
class MeshExport WriterVRML
{
public:
    /*!
     * \brief ReaderSTL
     */
    explicit WriterVRML(const MeshKernel& kernel, const Material* mat = nullptr);
    /*!
     * \brief Apply a transformation for the exported mesh.
     */
    void SetTransform(const Base::Matrix4D&);
    /*!
     * \brief Save the mesh to a VRML file.
     * \return true if the data could be written successfully, false otherwise.
     */
    bool Save(std::ostream& out) const;

private:
    const MeshKernel& _kernel;
    const Material* _material;
    Base::Matrix4D _transform;
    bool apply_transform {false};
};

}  // namespace MeshCore


#endif  // MESH_IO_WRITER_VRML_H
