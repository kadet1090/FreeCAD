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
# include <istream>
#endif

#include "Core/Builder.h"
#include "Core/MeshKernel.h"
#include "Core/Iterator.h"
#include <Base/Exception.h>
#include <Base/Sequencer.h>

#include "WriterSTL.h"


using namespace MeshCore;

WriterSTL::WriterSTL(const MeshKernel& kernel)
    : _kernel(kernel)
    , stlHeader {"MESH-MESH-MESH-MESH-MESH-MESH-MESH-MESH-"
                 "MESH-MESH-MESH-MESH-MESH-MESH-MESH-MESH\n"}
{}

void WriterSTL::SetHeaderData(const std::string& header)
{
    if (header.size() > 80) {
        stlHeader = header.substr(0, 80);
    }
    else if (header.size() < 80) {
        std::fill(stlHeader.begin(), stlHeader.end(), ' ');
        std::copy(header.begin(), header.end(), stlHeader.begin());
    }
    else {
        stlHeader = header;
    }
}

void WriterSTL::SetTransform(const Base::Matrix4D& mat)
{
    _transform = mat;
    if (mat != Base::Matrix4D()) {
        apply_transform = true;
    }
}

/** Saves the mesh object into an ASCII file. */
bool WriterSTL::SaveAscii(std::ostream& output) const
{
    MeshFacetIterator clIter(_kernel), clEnd(_kernel);
    clIter.Transform(this->_transform);
    const MeshGeomFacet* pclFacet {};

    if (!output || output.bad() || _kernel.CountFacets() == 0) {
        return false;
    }

    output.precision(6);
    output.setf(std::ios::fixed | std::ios::showpoint);
    Base::SequencerLauncher seq("saving...", _kernel.CountFacets() + 1);

    if (this->objectName.empty()) {
        output << "solid Mesh\n";
    }
    else {
        output << "solid " << this->objectName << '\n';
    }

    clIter.Begin();
    clEnd.End();
    while (clIter < clEnd) {
        pclFacet = &(*clIter);

        // normal
        output << "  facet normal " << pclFacet->GetNormal().x << " " << pclFacet->GetNormal().y
               << " " << pclFacet->GetNormal().z << '\n';
        output << "    outer loop\n";

        // vertices
        for (const auto& pnt : pclFacet->_aclPoints) {
            output << "      vertex " << pnt.x << " " << pnt.y << " " << pnt.z << '\n';
        }

        output << "    endloop\n";
        output << "  endfacet\n";

        ++clIter;
        seq.next(true);  // allow to cancel
    }

    output << "endsolid Mesh\n";

    return true;
}

/** Saves the mesh object into a binary file. */
bool WriterSTL::SaveBinary(std::ostream& output) const
{
    MeshFacetIterator clIter(_kernel), clEnd(_kernel);
    clIter.Transform(this->_transform);
    const MeshGeomFacet* pclFacet {};
    uint16_t usAtt {};
    char szInfo[81];

    if (!output || output.bad()) {
        return false;
    }

    Base::SequencerLauncher seq("saving...", _kernel.CountFacets() + 1);

    // stl_header has a length of 80
    strcpy(szInfo, stlHeader.c_str());
    output.write(szInfo, std::strlen(szInfo));

    uint32_t uCtFts = (uint32_t)_kernel.CountFacets();
    output.write((const char*)&uCtFts, sizeof(uCtFts));

    usAtt = 0;
    clIter.Begin();
    clEnd.End();
    while (clIter < clEnd) {
        pclFacet = &(*clIter);
        // normal
        Base::Vector3f normal = pclFacet->GetNormal();
        output.write((const char*)&(normal.x), sizeof(float));
        output.write((const char*)&(normal.y), sizeof(float));
        output.write((const char*)&(normal.z), sizeof(float));

        // vertices
        for (uint32_t i = 0; i < 3; i++) {
            output.write((const char*)&(pclFacet->_aclPoints[i].x), sizeof(float));
            output.write((const char*)&(pclFacet->_aclPoints[i].y), sizeof(float));
            output.write((const char*)&(pclFacet->_aclPoints[i].z), sizeof(float));
        }

        // attribute
        output.write((const char*)&usAtt, sizeof(usAtt));

        ++clIter;
        seq.next(true);  // allow to cancel
    }

    return true;
}
