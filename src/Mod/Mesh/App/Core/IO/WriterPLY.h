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


#ifndef MESH_IO_WRITER_PLY_H
#define MESH_IO_WRITER_PLY_H

#include <Mod/Mesh/App/Core/MeshIO.h>
#include <Mod/Mesh/MeshGlobal.h>

namespace MeshCore
{

/** Saves the mesh object into PLY format. */
class MeshExport WriterPLY
{
public:
    /*!
     * \brief WriterOBJ
     */
    explicit WriterPLY(const MeshKernel& kernel, const Material* mat = nullptr);
    /*!
     * \brief Apply a transformation for the exported mesh.
     */
    void SetTransform(const Base::Matrix4D&);
    /*!
     * \brief Save the mesh to a PLY file in binary format.
     * \return true if the data could be written successfully, false otherwise.
     */
    bool SaveBinary(std::ostream& out) const;
    /*!
     * \brief Save the mesh to a PLY file in ASCII format.
     * \return true if the data could be written successfully, false otherwise.
     */
    bool SaveAscii(std::ostream& out) const;

private:
    enum Format
    {
        ascii,
        binary_little_endian
    };

    struct Vertex
    {
        Base::Vector3f point;
        bool hasMaterial;
        Base::Color color;
    };

    using VertexOutput = std::function<void(const Vertex&)>;
    using FaceOutput = std::function<void(int, int, int)>;

    bool CheckStream(std::ostream& out) const;
    void SaveHeader(Format format, bool material, std::ostream& out) const;
    bool SaveMaterial() const;
    void SaveVertexes(bool material, const VertexOutput& func) const;
    void SaveFaces(const FaceOutput& func) const;

private:
    const MeshKernel& _kernel;
    const Material* _material;
    Base::Matrix4D _transform;
    bool apply_transform {false};
};

}  // namespace MeshCore

#endif  // MESH_IO_WRITER_PLY_H
