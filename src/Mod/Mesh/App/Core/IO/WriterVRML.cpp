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
#include "Core/MeshIO.h"
#include "Core/Iterator.h"
#include <Base/Exception.h>
#include <Base/Sequencer.h>

#include "WriterVRML.h"


using namespace MeshCore;

WriterVRML::WriterVRML(const MeshKernel& kernel, const Material* mat)
    : _kernel(kernel)
    , _material {mat}
{}

void WriterVRML::SetTransform(const Base::Matrix4D& mat)
{
    _transform = mat;
    if (mat != Base::Matrix4D()) {
        apply_transform = true;
    }
}

/** Writes a VRML file. */
bool WriterVRML::Save(std::ostream& output) const
{
    if (!output || output.bad() || (_kernel.CountFacets() == 0)) {
        return false;
    }

    Base::BoundBox3f clBB = _kernel.GetBoundBox();

    Base::SequencerLauncher seq("Saving VRML file...", _kernel.CountPoints() + _kernel.CountFacets());

    output << "#VRML V2.0 utf8\n";
    output << "WorldInfo {\n"
           << "  title \"Exported triangle mesh to VRML97\"\n"
           << "  info [\"Created by FreeCAD\"\n"
           << "        \"<https://www.freecad.org>\"]\n"
           << "}\n\n";

    // Transform
    output.precision(3);
    output.setf(std::ios::fixed | std::ios::showpoint);
    output << "Transform {\n"
           << "  scale 1 1 1\n"
           << "  rotation 0 0 1 0\n"
           << "  scaleOrientation 0 0 1 0\n"
           << "  center " << 0.0F << " " << 0.0F << " " << 0.0F << "\n"
           << "  translation " << 0.0F << " " << 0.0F << " " << 0.0F << "\n";

    output << "  children\n";
    output << "    Shape { \n";

    // write appearance
    output << "      appearance\n"
           << "      Appearance {\n"
           << "        material\n"
           << "        Material {\n";
    if (_material && _material->binding == MeshIO::OVERALL) {
        if (!_material->diffuseColor.empty()) {
            Base::Color c = _material->diffuseColor.front();
            output << "          diffuseColor " << c.r << " " << c.g << " " << c.b << "\n";
        }
        else {
            output << "          diffuseColor 0.8 0.8 0.8\n";
        }
    }
    else {
        output << "          diffuseColor 0.8 0.8 0.8\n";
    }
    output << "        }\n      }\n";  // end write appearance


    // write IndexedFaceSet
    output << "      geometry\n"
           << "      IndexedFaceSet {\n";

    output.precision(2);
    output.setf(std::ios::fixed | std::ios::showpoint);

    // write coords
    output << "        coord\n        Coordinate {\n          point [\n";
    MeshPointIterator pPIter(_kernel);
    pPIter.Transform(this->_transform);
    unsigned long i = 0, k = _kernel.CountPoints();
    output.precision(3);
    output.setf(std::ios::fixed | std::ios::showpoint);
    for (pPIter.Init(); pPIter.More(); pPIter.Next()) {
        output << "            " << pPIter->x << " " << pPIter->y << " " << pPIter->z;
        if (i++ < (k - 1)) {
            output << ",\n";
        }
        else {
            output << "\n";
        }

        seq.next();
    }

    output << "          ]\n        }\n";  // end write coord

    if (_material && _material->binding != MeshIO::OVERALL) {
        // write colors for each vertex
        output << "        color\n        Color {\n          color [\n";
        output.precision(3);
        output.setf(std::ios::fixed | std::ios::showpoint);
        for (auto pCIter = _material->diffuseColor.begin(); pCIter != _material->diffuseColor.end();
             ++pCIter) {
            output << "          " << float(pCIter->r) << " " << float(pCIter->g) << " "
                   << float(pCIter->b);
            if (pCIter < (_material->diffuseColor.end() - 1)) {
                output << ",\n";
            }
            else {
                output << "\n";
            }
        }

        output << "      ]\n    }\n";
        if (_material->binding == MeshIO::PER_VERTEX) {
            output << "    colorPerVertex TRUE\n";
        }
        else {
            output << "    colorPerVertex FALSE\n";
        }
    }

    // write face index
    output << "        coordIndex [\n";
    MeshFacetIterator pFIter(_kernel);
    pFIter.Transform(this->_transform);
    i = 0, k = _kernel.CountFacets();

    for (pFIter.Init(); pFIter.More(); pFIter.Next()) {
        MeshFacet clFacet = pFIter.GetIndices();
        output << "          " << clFacet._aulPoints[0] << ", " << clFacet._aulPoints[1] << ", "
               << clFacet._aulPoints[2] << ", -1";
        if (i++ < (k - 1)) {
            output << ",\n";
        }
        else {
            output << "\n";
        }

        seq.next();
    }

    output << "        ]\n      }\n";  // End IndexedFaceSet
    output << "    }\n";               // End Shape
    output << "}\n";                   // close children and Transform

    return true;
}
