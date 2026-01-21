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
# include <boost/regex.hpp>
# include <istream>
#endif

#include "Core/MeshKernel.h"
#include "Core/MeshIO.h"

#include "ReaderSMF.h"


using namespace MeshCore;

ReaderSMF::ReaderSMF(MeshKernel& kernel)
    : _kernel(kernel)
{}

bool ReaderSMF::Load(std::istream& input)
{
    boost::regex rx_p(
        "^v\\s+([-+]?[0-9]*)\\.?([0-9]+([eE][-+]?[0-9]+)?)"
        "\\s+([-+]?[0-9]*)\\.?([0-9]+([eE][-+]?[0-9]+)?)"
        "\\s+([-+]?[0-9]*)\\.?([0-9]+([eE][-+]?[0-9]+)?)\\s*$"
    );
    boost::regex rx_f3(
        "^f\\s+([-+]?[0-9]+)"
        "\\s+([-+]?[0-9]+)"
        "\\s+([-+]?[0-9]+)\\s*$"
    );
    boost::cmatch what;

    unsigned long segment = 0;
    MeshPointArray meshPoints;
    MeshFacetArray meshFacets;

    std::string line;
    float fX {}, fY {}, fZ {};
    int i1 = 1, i2 = 1, i3 = 1;
    MeshFacet item;

    if (!input || input.bad()) {
        return false;
    }

    std::streambuf* buf = input.rdbuf();
    if (!buf) {
        return false;
    }

    while (std::getline(input, line)) {
        if (boost::regex_match(line.c_str(), what, rx_p)) {
            fX = (float)std::atof(what[1].first);
            fY = (float)std::atof(what[4].first);
            fZ = (float)std::atof(what[7].first);
            meshPoints.push_back(MeshPoint(Base::Vector3f(fX, fY, fZ)));
        }
        else if (boost::regex_match(line.c_str(), what, rx_f3)) {
            // 3-vertex face
            i1 = std::atoi(what[1].first);
            i1 = i1 > 0 ? i1 - 1 : i1 + static_cast<int>(meshPoints.size());
            i2 = std::atoi(what[2].first);
            i2 = i2 > 0 ? i2 - 1 : i2 + static_cast<int>(meshPoints.size());
            i3 = std::atoi(what[3].first);
            i3 = i3 > 0 ? i3 - 1 : i3 + static_cast<int>(meshPoints.size());
            item.SetVertices(i1, i2, i3);
            item.SetProperty(segment);
            meshFacets.push_back(item);
        }
    }

    _kernel.Clear();  // remove all data before

    MeshCleanup meshCleanup(meshPoints, meshFacets);
    meshCleanup.RemoveInvalids();
    MeshPointFacetAdjacency meshAdj(meshPoints.size(), meshFacets);
    meshAdj.SetFacetNeighbourhood();
    _kernel.Adopt(meshPoints, meshFacets);

    return true;
}
