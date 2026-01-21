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
#ifndef _PreComp_
# include <boost/algorithm/string.hpp>
# include <boost/regex.hpp>
# include <istream>
# include <sstream>
#endif

#include "Core/MeshKernel.h"
#include "Core/MeshIO.h"

#include "ReaderOFF.h"


using namespace MeshCore;

ReaderOFF::ReaderOFF(MeshKernel& kernel, Material* material)
    : _kernel(kernel)
    , _material(material)
{}

bool ReaderOFF::Load(std::istream& input)
{
    // http://edutechwiki.unige.ch/en/3D_file_format
    boost::regex rx_n(R"(^\s*([0-9]+)\s+([0-9]+)\s+([0-9]+)\s*$)");
    boost::cmatch what;

    bool colorPerVertex = false;
    std::vector<Base::Color> diffuseColor;
    MeshPointArray meshPoints;
    MeshFacetArray meshFacets;

    std::string line;
    MeshFacet item;

    if (!input || input.bad()) {
        return false;
    }

    std::streambuf* buf = input.rdbuf();
    if (!buf) {
        return false;
    }

    std::getline(input, line);
    boost::algorithm::to_lower(line);
    if (line.find("coff") != std::string::npos) {
        // we expect colors to be there per vertex: x y z r g b a
        colorPerVertex = true;
    }
    else if (line.find("off") == std::string::npos) {
        return false;  // not an OFF file
    }

    // get number of vertices and faces
    int numPoints = 0, numFaces = 0;

    while (true) {
        std::getline(input, line);
        boost::algorithm::to_lower(line);
        if (boost::regex_match(line.c_str(), what, rx_n)) {
            numPoints = std::atoi(what[1].first);
            numFaces = std::atoi(what[2].first);
            break;
        }
    }

    if (numPoints == 0 || numFaces == 0) {
        return false;
    }

    meshPoints.reserve(numPoints);
    meshFacets.reserve(numFaces);
    if (colorPerVertex) {
        diffuseColor.reserve(numPoints);
    }
    else {
        diffuseColor.reserve(numFaces);
    }

    int cntPoints = 0;
    while (cntPoints < numPoints) {
        if (!std::getline(input, line)) {
            break;
        }
        std::istringstream str(line);
        str.unsetf(std::ios_base::skipws);
        str >> std::ws;
        if (str.eof()) {
            continue;  // empty line
        }

        float fX {}, fY {}, fZ {};
        str >> fX >> std::ws >> fY >> std::ws >> fZ;
        if (str) {
            meshPoints.push_back(MeshPoint(Base::Vector3f(fX, fY, fZ)));
            cntPoints++;

            if (colorPerVertex) {
                std::size_t pos = std::size_t(str.tellg());
                if (line.size() > pos) {
                    float r {}, g {}, b {}, a {};
                    str >> std::ws >> r >> std::ws >> g >> std::ws >> b;
                    if (str) {
                        str >> std::ws >> a;
                        // no transparency
                        if (!str) {
                            a = 1.0F;
                        }

                        if (r > 1.0F || g > 1.0F || b > 1.0F || a > 1.0F) {
                            r = static_cast<float>(r) / 255.0F;
                            g = static_cast<float>(g) / 255.0F;
                            b = static_cast<float>(b) / 255.0F;
                            a = static_cast<float>(a) / 255.0F;
                        }
                        diffuseColor.emplace_back(r, g, b, a);
                    }
                }
            }
        }
    }

    int cntFaces = 0;
    while (cntFaces < numFaces) {
        if (!std::getline(input, line)) {
            break;
        }
        std::istringstream str(line);
        str.unsetf(std::ios_base::skipws);
        str >> std::ws;
        if (str.eof()) {
            continue;  // empty line
        }
        int count {}, index {};
        str >> count;
        if (count >= 3) {
            std::vector<int> faces;
            faces.reserve(count);

            for (int i = 0; i < count; i++) {
                str >> std::ws;
                str >> index;
                faces.push_back(index);
            }

            for (int i = 0; i < count - 2; i++) {
                item.SetVertices(faces[0], faces[i + 1], faces[i + 2]);
                meshFacets.push_back(item);
            }
            cntFaces++;

            std::size_t pos = std::size_t(str.tellg());
            if (line.size() > pos) {
                float r {}, g {}, b {}, a {};
                str >> std::ws >> r >> std::ws >> g >> std::ws >> b;
                if (str) {
                    str >> std::ws >> a;
                    // no transparency
                    if (!str) {
                        a = 1.0F;
                    }

                    if (r > 1.0F || g > 1.0F || b > 1.0F || a > 1.0F) {
                        r = static_cast<float>(r) / 255.0F;
                        g = static_cast<float>(g) / 255.0F;
                        b = static_cast<float>(b) / 255.0F;
                        a = static_cast<float>(a) / 255.0F;
                    }
                    for (int i = 0; i < count - 2; i++) {
                        diffuseColor.emplace_back(r, g, b, a);
                    }
                }
            }
        }
    }

    if (_material) {
        if (colorPerVertex) {
            if (meshPoints.size() == diffuseColor.size()) {
                _material->binding = MeshIO::PER_VERTEX;
                _material->diffuseColor.swap(diffuseColor);
            }
        }
        else {
            if (meshFacets.size() == diffuseColor.size()) {
                _material->binding = MeshIO::PER_FACE;
                _material->diffuseColor.swap(diffuseColor);
            }
        }
    }
    this->_kernel.Clear();  // remove all data before

    MeshCleanup meshCleanup(meshPoints, meshFacets);
    if (_material) {
        meshCleanup.SetMaterial(_material);
    }
    meshCleanup.RemoveInvalids();
    MeshPointFacetAdjacency meshAdj(meshPoints.size(), meshFacets);
    meshAdj.SetFacetNeighbourhood();
    _kernel.Adopt(meshPoints, meshFacets);

    return true;
}
