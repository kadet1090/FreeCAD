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

#include "PreCompiled.h"

#include <Base/Stream.h>
#include "WriterPLY.h"


using namespace MeshCore;

WriterPLY::WriterPLY(const MeshKernel& kernel, const Material* mat)
    : _kernel(kernel)
    , _material {mat}
{}

void WriterPLY::SetTransform(const Base::Matrix4D& mat)
{
    _transform = mat;
    if (mat != Base::Matrix4D()) {
        apply_transform = true;
    }
}

bool WriterPLY::SaveMaterial() const
{
    const MeshPointArray& rPoints = _kernel.GetPoints();
    return (
        _material && _material->binding == MeshIO::PER_VERTEX
        && _material->diffuseColor.size() == rPoints.size()
    );
}

bool WriterPLY::CheckStream(std::ostream& out) const
{
    return (out && !out.bad());
}

void WriterPLY::SaveHeader(Format format, bool material, std::ostream& out) const
{
    const MeshPointArray& rPoints = _kernel.GetPoints();
    const MeshFacetArray& rFacets = _kernel.GetFacets();
    std::size_t v_count = rPoints.size();
    std::size_t f_count = rFacets.size();

    out << "ply\n"
        << (format == ascii ? "format ascii 1.0\n" : "format binary_little_endian 1.0\n")
        << "comment Created by FreeCAD <https://www.freecad.org>\n"
        << "element vertex " << v_count << '\n'
        << "property float32 x\n"
        << "property float32 y\n"
        << "property float32 z\n";
    if (material) {
        out << "property uchar red\n"
            << "property uchar green\n"
            << "property uchar blue\n";
    }
    out << "element face " << f_count << '\n'
        << "property list uchar int vertex_index\n"
        << "end_header\n";
}

void WriterPLY::SaveVertexes(bool material, const VertexOutput& func) const
{
    const MeshPointArray& rPoints = _kernel.GetPoints();
    std::size_t v_count = rPoints.size();

    Vertex vertex;
    vertex.hasMaterial = material;
    for (std::size_t i = 0; i < v_count; i++) {
        vertex.point = rPoints[i];
        if (this->apply_transform) {
            _transform.multVec(vertex.point, vertex.point);
        }

        if (material) {
            vertex.color = _material->diffuseColor[i];
        }

        func(vertex);
    }
}

void WriterPLY::SaveFaces(const FaceOutput& func) const
{
    const MeshFacetArray& rFacets = _kernel.GetFacets();
    std::size_t f_count = rFacets.size();

    int f1 {}, f2 {}, f3 {};
    for (std::size_t i = 0; i < f_count; i++) {
        const MeshFacet& f = rFacets[i];
        f1 = int(f._aulPoints[0]);
        f2 = int(f._aulPoints[1]);
        f3 = int(f._aulPoints[2]);
        func(f1, f2, f3);
    }
}

bool WriterPLY::SaveBinary(std::ostream& out) const
{
    if (!CheckStream(out)) {
        return false;
    }

    bool saveVertexColor = SaveMaterial();
    SaveHeader(binary_little_endian, saveVertexColor, out);

    Base::OutputStream os(out);
    os.setByteOrder(Base::Stream::LittleEndian);

    auto writePoints = [&os](const Vertex& v) {
        os << v.point.x << v.point.y << v.point.z;
        if (v.hasMaterial) {
            // NOLINTBEGIN
            uint8_t r = uint8_t(255.0F * v.color.r);
            uint8_t g = uint8_t(255.0F * v.color.g);
            uint8_t b = uint8_t(255.0F * v.color.b);
            // NOLINTEND
            os << r << g << b;
        }
    };

    SaveVertexes(saveVertexColor, writePoints);

    auto writeFacets = [&os](int f1, int f2, int f3) {
        const unsigned char n = 3;
        os << n;
        os << f1 << f2 << f3;
    };

    SaveFaces(writeFacets);

    return true;
}

bool WriterPLY::SaveAscii(std::ostream& out) const
{
    if (!CheckStream(out)) {
        return false;
    }

    bool saveVertexColor = SaveMaterial();
    SaveHeader(ascii, saveVertexColor, out);

    out.precision(6);
    out.setf(std::ios::fixed | std::ios::showpoint);

    auto writePoints = [&out](const Vertex& v) {
        out << v.point.x << " " << v.point.y << " " << v.point.z;
        if (v.hasMaterial) {
            // NOLINTBEGIN
            int r = int(255.0F * v.color.r);
            int g = int(255.0F * v.color.g);
            int b = int(255.0F * v.color.b);
            // NOLINTEND
            out << " " << r << " " << g << " " << b;
        }

        out << '\n';
    };

    SaveVertexes(saveVertexColor, writePoints);

    auto writeFacets = [&out](int f1, int f2, int f3) {
        const unsigned int n = 3;
        out << n << " " << f1 << " " << f2 << " " << f3 << '\n';
    };

    SaveFaces(writeFacets);

    return true;
}
