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
#endif

#include <boost/convert.hpp>
#include <boost/convert/spirit.hpp>
#include <boost/utility/string_view.hpp>

#include <Base/Console.h>
#include "Core/MeshKernel.h"
#include "Core/MeshIO.h"

#include "ReaderNAS.h"


using namespace MeshCore;

namespace
{

std::string& ltrim(std::string& str)
{
    std::string::size_type pos = 0;
    for (char it : str) {
        if (it != 0x20 && it != 0x09) {
            break;
        }
        pos++;
    }
    if (pos > 0) {
        str = str.substr(pos);
    }
    return str;
}

/* Usage by CMeshNastran, CMeshCadmouldFE. Added by Sergey Sukhov (26.04.2002)*/
struct NODE
{
    float x, y, z;
};

struct TRIA
{
    int iV[3];
};

struct QUAD
{
    int iV[4];
};

}  // namespace

ReaderNAS::ReaderNAS(MeshKernel& kernel)
    : _kernel(kernel)
{}

bool ReaderNAS::Load(std::istream& input)
{
    if (!input || input.bad()) {
        return false;
    }

    boost::regex rx_t(
        "\\s*CTRIA3\\s+([0-9]+)\\s+([0-9]+)"
        "\\s+([0-9]+)\\s+([0-9]+)\\s+([0-9]+)\\s*"
    );
    boost::regex rx_q(
        "\\s*CQUAD4\\s+([0-9]+)\\s+([0-9]+)"
        "\\s+([0-9]+)\\s+([0-9]+)\\s+([0-9]+)\\s+([0-9]+)\\s*"
    );
    boost::cmatch what;

    std::string line;
    MeshFacet clMeshFacet;
    MeshPointArray vVertices;
    MeshFacetArray vTriangle;

    int index {};
    std::map<int, NODE> mNode;
    std::map<int, TRIA> mTria;
    std::map<int, QUAD> mQuad;

    int badElementCounter = 0;

    while (std::getline(input, line)) {
        boost::algorithm::to_upper(ltrim(line));
        if (line.empty()) {
            // Skip all the following tests
        }
        else if (line.rfind("GRID*", 0) == 0) {
            // This element is the 16-digit-precision GRID element, which occupies two lines of the
            // card. Note that FreeCAD discards the extra precision, downcasting to an four-byte
            // float.
            //
            // The two lines are:
            // 1      8               24             40             56
            // GRID*  Index(16)       Blank(16)      x(16)          y(at least one)
            // *      z(at least one)
            //
            // The first character is typically the sign, and may be omitted for positive numbers,
            // so it is possible for a field to begin with a blank. Trailing zeros may be omitted,
            // so a field may also end with blanks. No space or other delimiter is required between
            // the numbers. The following is a valid NASTRAN GRID* element:
            //
            // GRID*  1                               0.1234567890120.
            // *      1.
            //
            // Element type(8), index(16), empty(16), x(16), y(>=1)
            if (line.length() < 8 + 16 + 16 + 16 + 1) {
                badElementCounter++;
                continue;
            }
            auto indexView = boost::string_view(&line[8], 16);
            // auto blankView = boost::string_view(&line[8+16], 16); // No data is needed here
            auto xView = boost::string_view(&line[8 + 16 + 16], 16);
            auto yView = boost::string_view(&line[8 + 16 + 16 + 16]);

            std::string line2;
            std::getline(input, line2);
            if ((!line2.empty() && line2[0] != '*') || line2.length() < 9) {
                badElementCounter++;
                continue;  // File format error: second line is not a continuation line
            }
            auto zView = boost::string_view(&line2[8]);

            // We have to strip off any whitespace (technically really just any *trailing*
            // whitespace):
            auto indexString = boost::trim_copy(std::string(indexView));
            auto xString = boost::trim_copy(std::string(xView));
            auto yString = boost::trim_copy(std::string(yView));
            auto zString = boost::trim_copy(std::string(zView));

            auto converter = boost::cnv::spirit();
            auto indexCheck = boost::convert<int>(indexString, converter);
            if (!indexCheck.is_initialized()) {
                // File format error: index couldn't be converted to an integer
                badElementCounter++;
                continue;
            }
            index = indexCheck.get() - 1;  // Minus one so we are zero-indexed to match existing code

            // Get the high-precision versions first
            auto x = boost::convert<double>(xString, converter);
            auto y = boost::convert<double>(yString, converter);
            auto z = boost::convert<double>(zString, converter);

            if (!x.is_initialized() || !y.is_initialized() || !z.is_initialized()) {
                // File format error: x, y or z could not be converted
                badElementCounter++;
                continue;
            }

            // Now drop precision:
            mNode[index].x = (float)x.get();
            mNode[index].y = (float)y.get();
            mNode[index].z = (float)z.get();
        }
        else if (line.rfind("GRID", 0) == 0) {

            boost::regex rx_spaceDelimited(
                "\\s*GRID\\s+([0-9]+)"
                "\\s+([-+]?[0-9]*)\\.?([0-9]+([eE][-+]?[0-9]+)?)"
                "\\s+([-+]?[0-9]*)\\.?([0-9]+([eE][-+]?[0-9]+)?)"
                "\\s+([-+]?[0-9]*)\\.?([0-9]+([eE][-+]?[0-9]+)?)\\s*"
            );

            if (boost::regex_match(line.c_str(), what, rx_spaceDelimited)) {
                // insert the read-in vertex into a map to preserve the order
                index = std::atol(what[1].first) - 1;
                mNode[index].x = (float)std::atof(what[2].first);
                mNode[index].y = (float)std::atof(what[5].first);
                mNode[index].z = (float)std::atof(what[8].first);
            }
            else {
                // Classic NASTRAN uses a fixed 8 character field width:
                // 1       8       16      24      32      40
                // $-------ID------CP------X1------X2------X3------CD------PS------9-------+-------
                // GRID    1               1.2345671.2345671.234567
                // GRID    112             6.0000000.5000000.00E+00

                // Element type(8), id(8), cp(8), x(8), y(8), z(at least 1)
                if (line.length() < 41) {
                    badElementCounter++;
                    continue;
                }
                auto indexView = boost::string_view(&line[8], 8);
                auto xView = boost::string_view(&line[24], 8);
                auto yView = boost::string_view(&line[32], 8);
                auto zView = boost::string_view(&line[40], 8);

                auto indexString = boost::trim_copy(std::string(indexView));
                auto xString = boost::trim_copy(std::string(xView));
                auto yString = boost::trim_copy(std::string(yView));
                auto zString = boost::trim_copy(std::string(zView));

                auto converter = boost::cnv::spirit();
                auto indexCheck = boost::convert<int>(indexString, converter);
                if (!indexCheck.is_initialized()) {
                    // File format error: index couldn't be converted to an integer
                    badElementCounter++;
                    continue;
                }
                // Minus one so we are zero-indexed to match existing code
                index = indexCheck.get() - 1;

                auto x = boost::convert<float>(xString, converter);
                auto y = boost::convert<float>(yString, converter);
                auto z = boost::convert<float>(zString, converter);

                if (!x.is_initialized() || !y.is_initialized() || !z.is_initialized()) {
                    // File format error: x, y or z could not be converted
                    badElementCounter++;
                    continue;
                }

                mNode[index].x = x.get();
                mNode[index].y = y.get();
                mNode[index].z = z.get();
            }
        }
        else if (line.rfind("CTRIA3 ", 0) == 0) {
            if (boost::regex_match(line.c_str(), what, rx_t)) {
                // insert the read-in triangle into a map to preserve the order
                index = std::atol(what[1].first) - 1;
                mTria[index].iV[0] = std::atol(what[3].first) - 1;
                mTria[index].iV[1] = std::atol(what[4].first) - 1;
                mTria[index].iV[2] = std::atol(what[5].first) - 1;
            }
        }
        else if (line.rfind("CQUAD4", 0) == 0) {
            if (boost::regex_match(line.c_str(), what, rx_q)) {
                // insert the read-in quadrangle into a map to preserve the order
                index = std::atol(what[1].first) - 1;
                mQuad[index].iV[0] = std::atol(what[3].first) - 1;
                mQuad[index].iV[1] = std::atol(what[4].first) - 1;
                mQuad[index].iV[2] = std::atol(what[5].first) - 1;
                mQuad[index].iV[3] = std::atol(what[6].first) - 1;
            }
        }
    }

    if (badElementCounter > 0) {
        Base::Console().Warning("Found bad elements while reading NASTRAN file.\n");
    }

    // Check the triangles to make sure the vertices they refer to actually exist:
    for (const auto& tri : mTria) {
        for (int i : tri.second.iV) {
            if (mNode.find(i) == mNode.end()) {
                Base::Console().Error(
                    "CTRIA3 element refers to a node that does not exist, or could not be read.\n"
                );
                return false;
            }
        }
    }

    // Check the quads to make sure the vertices they refer to actually exist:
    for (const auto& quad : mQuad) {
        for (int i : quad.second.iV) {
            if (mNode.find(i) == mNode.end()) {
                Base::Console().Error(
                    "CQUAD4 element refers to a node that does not exist, or could not be read.\n"
                );
                return false;
            }
        }
    }

    float fLength[2];
    if (mTria.empty()) {
        index = 0;
    }
    else {
        index = mTria.rbegin()->first + 1;
    }
    for (const auto& QI : mQuad) {
        for (int i = 0; i < 2; i++) {
            float fDx = mNode[QI.second.iV[i + 2]].x - mNode[QI.second.iV[i]].x;
            float fDy = mNode[QI.second.iV[i + 2]].y - mNode[QI.second.iV[i]].y;
            float fDz = mNode[QI.second.iV[i + 2]].z - mNode[QI.second.iV[i]].z;
            fLength[i] = fDx * fDx + fDy * fDy + fDz * fDz;
        }
        if (fLength[0] < fLength[1]) {
            mTria[index].iV[0] = QI.second.iV[0];
            mTria[index].iV[1] = QI.second.iV[1];
            mTria[index].iV[2] = QI.second.iV[2];

            mTria[index + 1].iV[0] = QI.second.iV[0];
            mTria[index + 1].iV[1] = QI.second.iV[2];
            mTria[index + 1].iV[2] = QI.second.iV[3];
        }
        else {
            mTria[index].iV[0] = QI.second.iV[0];
            mTria[index].iV[1] = QI.second.iV[1];
            mTria[index].iV[2] = QI.second.iV[3];

            mTria[index + 1].iV[0] = QI.second.iV[1];
            mTria[index + 1].iV[1] = QI.second.iV[2];
            mTria[index + 1].iV[2] = QI.second.iV[3];
        }

        index += 2;
    }

    // Applying the nodes
    vVertices.reserve(mNode.size());
    for (const auto& MI : mNode) {
        vVertices.push_back(Base::Vector3f(MI.second.x, MI.second.y, MI.second.z));
    }

    // Converting data to Mesh. Negative conversion for right orientation of normal-vectors.
    vTriangle.reserve(mTria.size());
    for (const auto& MI : mTria) {
        clMeshFacet._aulPoints[0] = MI.second.iV[1];
        clMeshFacet._aulPoints[1] = MI.second.iV[0];
        clMeshFacet._aulPoints[2] = MI.second.iV[2];
        vTriangle.push_back(clMeshFacet);
    }

    // make sure to add only vertices which are referenced by the triangles
    _kernel.Merge(vVertices, vTriangle);

    return true;
}
