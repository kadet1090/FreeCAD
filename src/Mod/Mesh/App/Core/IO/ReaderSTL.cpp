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

#include "Core/Builder.h"
#include "Core/MeshKernel.h"
#include <Base/Exception.h>

#include "ReaderSTL.h"


using namespace MeshCore;

ReaderSTL::ReaderSTL(MeshKernel& kernel)
    : _kernel(kernel)
{}

bool ReaderSTL::Load(std::istream& input)
{
    char szBuf[200];

    if (!input || input.bad()) {
        return false;
    }

    // Read in 50 characters from position 80 on and check for keywords like 'SOLID', 'FACET',
    // 'NORMAL', 'VERTEX', 'ENDFACET' or 'ENDLOOP'. As the file can be binary with one triangle only
    // we must not read in more than (max.) 54 bytes because the file size has only 134 bytes in
    // this case. On the other hand we must overread the first 80 bytes because it can happen that
    // the file is binary but contains one of these keywords.
    std::streambuf* buf = input.rdbuf();
    if (!buf) {
        return false;
    }
    buf->pubseekoff(80, std::ios::beg, std::ios::in);
    uint32_t ulCt {}, ulBytes = 50;
    input.read((char*)&ulCt, sizeof(ulCt));
    // if we have a binary STL with a single triangle we can only read-in 50 bytes
    if (ulCt > 1) {
        ulBytes = 100;
    }
    // Either it's really an invalid STL file or it's just empty. In this case the number of facets
    // must be 0.
    if (!input.read(szBuf, ulBytes)) {
        return (ulCt == 0);
    }
    szBuf[ulBytes] = 0;
    boost::algorithm::to_upper(szBuf);

    try {
        if (!strstr(szBuf, "SOLID") && !strstr(szBuf, "FACET") && !strstr(szBuf, "NORMAL")
            && !strstr(szBuf, "VERTEX") && !strstr(szBuf, "ENDFACET") && !strstr(szBuf, "ENDLOOP")) {
            // probably binary STL
            buf->pubseekoff(0, std::ios::beg, std::ios::in);
            return LoadBinary(input);
        }

        // Ascii STL
        buf->pubseekoff(0, std::ios::beg, std::ios::in);
        return LoadAscii(input);
    }
    catch (const Base::MemoryException&) {
        _kernel.Clear();
        throw;  // Throw the same instance of Base::MemoryException
    }
    catch (const Base::AbortException&) {
        _kernel.Clear();
        return false;
    }
    catch (const Base::Exception&) {
        _kernel.Clear();
        throw;  // Throw the same instance of Base::Exception
    }
    catch (...) {
        _kernel.Clear();
        throw;
    }

    return true;
}

/** Loads an ASCII STL file. */
bool ReaderSTL::LoadAscii(std::istream& input)
{
    boost::regex rx_p(
        "^\\s*VERTEX\\s+([-+]?[0-9]*)\\.?([0-9]+([eE][-+]?[0-9]+)?)"
        "\\s+([-+]?[0-9]*)\\.?([0-9]+([eE][-+]?[0-9]+)?)"
        "\\s+([-+]?[0-9]*)\\.?([0-9]+([eE][-+]?[0-9]+)?)\\s*$"
    );
    boost::regex rx_f(
        "^\\s*FACET\\s+NORMAL\\s+([-+]?[0-9]*)\\.?([0-9]+([eE][-+]?[0-9]+)?)"
        "\\s+([-+]?[0-9]*)\\.?([0-9]+([eE][-+]?[0-9]+)?)"
        "\\s+([-+]?[0-9]*)\\.?([0-9]+([eE][-+]?[0-9]+)?)\\s*$"
    );
    boost::cmatch what;

    std::string line;
    float fX {}, fY {}, fZ {};
    unsigned long ulVertexCt {}, ulFacetCt {};
    MeshGeomFacet clFacet;

    if (!input || input.bad()) {
        return false;
    }

    std::streamoff ulSize = 0;
    std::streambuf* buf = input.rdbuf();
    ulSize = buf->pubseekoff(0, std::ios::end, std::ios::in);
    buf->pubseekoff(0, std::ios::beg, std::ios::in);
    ulSize -= 20;

    // count facets
    while (std::getline(input, line)) {
        boost::algorithm::to_upper(line);
        if (line.find("ENDFACET") != std::string::npos) {
            ulFacetCt++;
        }
        // prevent from reading EOF (as I don't know how to reread the file then)
        if (input.tellg() > ulSize) {
            break;
        }
        if (line.find("ENDSOLID") != std::string::npos) {
            break;
        }
    }

    // restart from the beginning
    buf->pubseekoff(0, std::ios::beg, std::ios::in);

    MeshFastBuilder builder(_kernel);
    builder.Initialize(ulFacetCt);

    ulVertexCt = 0;
    while (std::getline(input, line)) {
        boost::algorithm::to_upper(line);
        if (boost::regex_match(line.c_str(), what, rx_f)) {
            fX = (float)std::atof(what[1].first);
            fY = (float)std::atof(what[4].first);
            fZ = (float)std::atof(what[7].first);
            clFacet.SetNormal(Base::Vector3f(fX, fY, fZ));
        }
        else if (boost::regex_match(line.c_str(), what, rx_p)) {
            fX = (float)std::atof(what[1].first);
            fY = (float)std::atof(what[4].first);
            fZ = (float)std::atof(what[7].first);
            clFacet._aclPoints[ulVertexCt++].Set(fX, fY, fZ);
            if (ulVertexCt == 3) {
                ulVertexCt = 0;
                builder.AddFacet(clFacet);
            }
        }
    }

    builder.Finish();

    return true;
}

/** Loads a binary STL file. */
bool ReaderSTL::LoadBinary(std::istream& input)
{
    char szInfo[80];
    Base::Vector3f clVects[4];
    uint16_t usAtt = 0;
    uint32_t ulCt = 0;

    if (!input || input.bad()) {
        return false;
    }

    // Header-Info ueberlesen
    input.read(szInfo, sizeof(szInfo));

    // Anzahl Facets
    input.read((char*)&ulCt, sizeof(ulCt));
    if (input.bad()) {
        return false;
    }

    // get file size and calculate the number of facets
    std::streamoff ulSize = 0;
    std::streambuf* buf = input.rdbuf();
    if (buf) {
        std::streamoff ulCurr {};
        ulCurr = buf->pubseekoff(0, std::ios::cur, std::ios::in);
        ulSize = buf->pubseekoff(0, std::ios::end, std::ios::in);
        buf->pubseekoff(ulCurr, std::ios::beg, std::ios::in);
    }

    uint32_t ulFac = (ulSize - (80 + sizeof(uint32_t))) / 50;

    // compare the calculated with the read value
    if (ulCt > ulFac) {
        return false;  // not a valid STL file
    }

    MeshFastBuilder builder(_kernel);
    builder.Initialize(ulCt);

    for (uint32_t i = 0; i < ulCt; i++) {
        // read normal, points
        input.read((char*)&clVects, sizeof(clVects));

        std::swap(clVects[0], clVects[3]);
        builder.AddFacet(clVects);

        // overread 2 bytes attribute
        input.read((char*)&usAtt, sizeof(usAtt));
    }

    builder.Finish();

    return true;
}
