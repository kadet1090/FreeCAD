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


#ifndef MESH_IO_WRITER_STL_H
#define MESH_IO_WRITER_STL_H

#include <Base/Matrix.h>
#include <Mod/Mesh/MeshGlobal.h>
#include <iosfwd>

namespace MeshCore
{

class MeshKernel;

/** Saves the mesh object into STL format. */
class MeshExport WriterSTL
{
public:
    /*!
     * \brief WriterSTL
     */
    explicit WriterSTL(const MeshKernel& kernel);
    /*!
     * \brief Apply a transformation for the exported mesh.
     */
    void SetTransform(const Base::Matrix4D&);
    /*!
     * \brief Set the object name used for the ASCII format.
     */
    void SetObjectName(const std::string& n)
    {
        objectName = n;
    }
    /*!
     * \brief Set the header data used for the binary format.
     */
    void SetHeaderData(const std::string& header);
    /*!
     * \brief Save the mesh to an STL file in binary format.
     * \return true if the data could be written successfully, false otherwise.
     */
    bool SaveBinary(std::ostream& out) const;
    /*!
     * \brief Save the mesh to an STL file in ASCII format.
     * \return true if the data could be written successfully, false otherwise.
     */
    bool SaveAscii(std::ostream& out) const;

private:
    const MeshKernel& _kernel;
    Base::Matrix4D _transform;
    std::string objectName;
    std::string stlHeader;
    bool apply_transform {false};
};

}  // namespace MeshCore


#endif  // MESH_IO_WRITER_STL_H
