/***************************************************************************
 *   Copyright (c) 2022 Werner Mayer <wmayer[at]users.sourceforge.net>     *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#include "PreCompiled.h"

#include "Core/Iterator.h"
#include <Base/Console.h>
#include <Base/Tools.h>

#include "WriterOBJ.h"


using namespace MeshCore;

namespace
{

struct Color_Less
{
    bool operator()(const Base::Color& x, const Base::Color& y) const
    {
        if (x.r != y.r) {
            return x.r < y.r;
        }
        if (x.g != y.g) {
            return x.g < y.g;
        }
        if (x.b != y.b) {
            return x.b < y.b;
        }
        return false;  // equal colors
    }
};

std::vector<Base::Color> getUniqueColors(const Material* material)
{
    // make sure to use the 'usemtl' statement as less often as possible
    std::vector<Base::Color> colors = material->diffuseColor;
    std::sort(colors.begin(), colors.end(), Color_Less());
    colors.erase(std::unique(colors.begin(), colors.end()), colors.end());
    return colors;
}

}  // namespace

WriterOBJ::WriterOBJ(const MeshKernel& kernel, const Material* material)
    : _kernel(kernel)
    , _material(material)
{}

void WriterOBJ::SetGroups(const std::vector<Group>& g)
{
    _groups = g;
}

void WriterOBJ::SetTransform(const Base::Matrix4D& mat)
{
    _transform = mat;
    if (mat != Base::Matrix4D()) {
        apply_transform = true;
    }
}

bool WriterOBJ::Save(std::ostream& out)
{
    if (!out || out.bad()) {
        return false;
    }

    Binding binding = GetBinding();
    SaveHeader(binding, out);

    out.precision(6);
    out.setf(std::ios::fixed | std::ios::showpoint);

    SaveVertexes(binding, out);
    SaveNormals(out);
    SaveFaces(binding, out);

    return true;
}

WriterOBJ::Binding WriterOBJ::GetBinding() const
{
    Binding binding = Binding::OVERALL;
    if (_material) {
        const MeshPointArray& rPoints = _kernel.GetPoints();
        const MeshFacetArray& rFacets = _kernel.GetFacets();

        if (_material->binding == MeshIO::PER_FACE) {
            if (_material->diffuseColor.size() != rFacets.size()) {
                Base::Console().Warning(
                    "Cannot export color information because there is a "
                    "different number of faces and colors"
                );
            }
            else {
                binding = Binding::PER_FACE;
            }
        }
        else if (_material->binding == MeshIO::PER_VERTEX) {
            if (_material->diffuseColor.size() != rPoints.size()) {
                Base::Console().Warning(
                    "Cannot export color information because there is a "
                    "different number of points and colors"
                );
            }
            else {
                binding = Binding::PER_VERTEX;
            }
        }
        else if (_material->binding == MeshIO::OVERALL) {
            if (_material->diffuseColor.empty()) {
                Base::Console().Warning(
                    "Cannot export color information because there is no color defined"
                );
            }
            else {
                binding = Binding::PER_VERTEX;
            }
        }
    }

    return binding;
}

void WriterOBJ::SaveHeader(Binding binding, std::ostream& out) const
{
    // Header
    out << "# Created by FreeCAD <https://www.freecad.org>\n";
    if (binding == Binding::PER_FACE) {
        out << "mtllib " << _material->library << '\n';
    }
}

void WriterOBJ::SaveVertexes(Binding binding, std::ostream& out) const
{
    const MeshPointArray& rPoints = _kernel.GetPoints();

    // vertices
    Base::Vector3f pt;
    std::size_t index = 0;
    for (const auto& it : rPoints) {
        if (this->apply_transform) {
            pt = this->_transform * it;
        }
        else {
            pt.Set(it.x, it.y, it.z);
        }

        if (binding == Binding::PER_VERTEX) {
            Base::Color c;
            if (_material->binding == MeshIO::PER_VERTEX) {
                c = _material->diffuseColor[index];
            }
            else {
                c = _material->diffuseColor.front();
            }

            int r = static_cast<int>(c.r * 255.0F);
            int g = static_cast<int>(c.g * 255.0F);
            int b = static_cast<int>(c.b * 255.0F);

            SaveVertex(pt, out) << " ";
            SaveColor(r, g, b, out) << '\n';
        }
        else {
            SaveVertex(pt, out) << '\n';
        }

        ++index;
    }
}

void WriterOBJ::SaveNormals(std::ostream& out) const
{
    MeshFacetIterator it(_kernel);
    MeshFacetIterator end(_kernel);
    const MeshGeomFacet* facet {};

    it.Begin();
    end.End();

    while (it < end) {
        facet = &(*it);
        SaveNormal(facet->GetNormal(), out) << '\n';
        ++it;
    }
}

std::ostream& WriterOBJ::SaveColor(int r, int g, int b, std::ostream& out) const
{
    out << r << " " << g << " " << b;
    return out;
}

std::ostream& WriterOBJ::SaveVertex(const Base::Vector3f& pt, std::ostream& out) const
{
    out << "v " << pt.x << " " << pt.y << " " << pt.z;
    return out;
}

std::ostream& WriterOBJ::SaveNormal(const Base::Vector3f& pt, std::ostream& out) const
{
    out << "vn " << pt.x << " " << pt.y << " " << pt.z;
    return out;
}

void WriterOBJ::SaveFace(std::size_t faceIdx, const MeshFacet& face, std::ostream& out) const
{
    // clang-format off
    out << "f " << face._aulPoints[0] + 1 << "//" << faceIdx << " "
                << face._aulPoints[1] + 1 << "//" << faceIdx << " "
                << face._aulPoints[2] + 1 << "//" << faceIdx << '\n';
    // clang-format on
}

void WriterOBJ::SaveFaces(Binding binding, std::ostream& out) const
{
    if (_groups.empty()) {
        if (binding == Binding::PER_FACE) {
            SaveFacesWithMaterial(out);
        }
        else {
            SaveFaces(out);
        }
    }
    else {
        if (binding == Binding::PER_FACE) {
            SaveGroupsWithMaterial(out);
        }
        else {
            SaveGroups(out);
        }
    }
}

void WriterOBJ::SaveFaces(std::ostream& out) const
{
    const MeshFacetArray& rFacets = _kernel.GetFacets();
    // facet indices (no texture and normal indices)
    std::size_t faceIdx = 1;
    for (const auto& it : rFacets) {
        SaveFace(faceIdx, it, out);
        faceIdx++;
    }
}

void WriterOBJ::SaveFacesWithMaterial(std::ostream& out) const
{
    const MeshFacetArray& rFacets = _kernel.GetFacets();
    // facet indices (no texture and normal indices)

    // make sure to use the 'usemtl' statement as less often as possible
    std::vector<Base::Color> colors = getUniqueColors(_material);

    std::size_t index = 0;
    Base::Color prev;
    int faceIdx = 1;
    const std::vector<Base::Color>& Kd = _material->diffuseColor;
    for (auto it = rFacets.begin(); it != rFacets.end(); ++it, index++) {
        if (index == 0 || prev != Kd[index]) {
            prev = Kd[index];
            auto c_it = std::find(colors.begin(), colors.end(), prev);
            if (c_it != colors.end()) {
                out << "usemtl material_" << (c_it - colors.begin()) << '\n';
            }
        }

        SaveFace(faceIdx, *it, out);
        faceIdx++;
    }
}

void WriterOBJ::SaveGroups(std::ostream& out) const
{
    const MeshFacetArray& rFacets = _kernel.GetFacets();
    for (const auto& gt : _groups) {
        out << "g " << Base::Tools::escapedUnicodeFromUtf8(gt.name.c_str()) << '\n';
        for (FacetIndex it : gt.indices) {
            const MeshFacet& f = rFacets[it];
            SaveFace(it + 1, f, out);
        }
    }
}

void WriterOBJ::SaveGroupsWithMaterial(std::ostream& out) const
{
    const MeshFacetArray& rFacets = _kernel.GetFacets();

    // make sure to use the 'usemtl' statement as less often as possible
    std::vector<Base::Color> colors = getUniqueColors(_material);

    bool first = true;
    Base::Color prev;
    const std::vector<Base::Color>& Kd = _material->diffuseColor;

    for (const auto& gt : _groups) {
        out << "g " << Base::Tools::escapedUnicodeFromUtf8(gt.name.c_str()) << '\n';
        for (FacetIndex it : gt.indices) {
            const MeshFacet& f = rFacets[it];
            if (first || prev != Kd[it]) {
                first = false;
                prev = Kd[it];
                auto c_it = std::find(colors.begin(), colors.end(), prev);
                if (c_it != colors.end()) {
                    out << "usemtl material_" << (c_it - colors.begin()) << '\n';
                }
            }

            SaveFace(it + 1, f, out);
        }
    }
}

bool WriterOBJ::SaveMaterial(std::ostream& out)
{
    if (!out || out.bad()) {
        return false;
    }

    if (_material) {
        if (_material->binding == MeshIO::PER_FACE) {

            std::vector<Base::Color> Kd = _material->diffuseColor;
            std::sort(Kd.begin(), Kd.end(), Color_Less());
            Kd.erase(std::unique(Kd.begin(), Kd.end()), Kd.end());

            out.precision(6);
            out.setf(std::ios::fixed | std::ios::showpoint);
            out << "# Created by FreeCAD <https://www.freecad.org>: 'None'\n";
            out << "# Material Count: " << Kd.size() << '\n';

            for (std::size_t i = 0; i < Kd.size(); i++) {
                out << '\n';
                out << "newmtl material_" << i << '\n';
                out << "    Ka 0.200000 0.200000 0.200000\n";
                out << "    Kd " << Kd[i].r << " " << Kd[i].g << " " << Kd[i].b << '\n';
                out << "    Ks 1.000000 1.000000 1.000000\n";
                out << "    d 1.000000" << '\n';
                out << "    illum 2" << '\n';
                out << "    Ns 0.000000" << '\n';
            }

            return true;
        }
    }

    return false;
}
