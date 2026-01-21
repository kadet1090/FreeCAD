/***************************************************************************
 *   Copyright (c) 2014 Yorik van Havre <yorik@uncreated.net>              *
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
#ifndef _PreComp_
# include <boost/algorithm/string.hpp>
# include <boost/algorithm/string/join.hpp>
#endif

#include <fmt/format.h>
#include <Base/Exception.h>
#include <Base/PlacementPy.h>
#include <Base/PyWrapParseTupleAndKeywords.h>

// files generated out of CommandPy.xml
#include "CommandPy.h"
#include "CommandPy.cpp"


using namespace Path;

// returns a string which represents the object e.g. when printed in python
std::string CommandPy::representation() const
{
    std::vector<std::string> items;
    items.reserve(getCommandPtr()->Parameters.size());
    for (const auto& it : getCommandPtr()->Parameters) {
        items.emplace_back(fmt::format("'{}': {}", it.first, it.second));
    }

    std::stringstream str;
    str.precision(5);
    str << "Command(";
    str << "'" << getCommandPtr()->Name << "',";
    str << " {";
    str << boost::algorithm::join(items, ", ");
    str << "})";
    return str.str();
}

//
// Py::Dict parameters_copy_dict is now a class member to avoid delete/create/copy on every read
// access from python code Now the pre-filled Py::Dict is returned which is more consistent with
// normal python behaviour. It should be cleared whenever the c++ Parameters object is changed eg
// setParameters() or other objects invalidate its content, eg setPlacement()
// https://forum.freecad.org/viewtopic.php?f=15&t=50583

PyObject* CommandPy::PyMake(struct _typeobject*, PyObject*, PyObject*)  // Python wrapper
{
    // create a new instance of CommandPy and the Twin object
    return new CommandPy(new Command);
}

// constructor method
int CommandPy::PyInit(PyObject* args, PyObject* kwd)
{
    PyObject* parameters = nullptr;
    const char* name = "";
    static const std::array<const char*, 3> kwlist {"name", "parameters", nullptr};
    if (Base::Wrapped_ParseTupleAndKeywords(args, kwd, "|sO!", kwlist, &name, &PyDict_Type, &parameters)) {
        std::string sname(name);
        boost::to_upper(sname);
        try {
            if (!sname.empty()) {
                getCommandPtr()->setFromGCode(name);
            }
        }
        catch (const Base::Exception& e) {
            PyErr_SetString(PyExc_ValueError, e.what());
            return -1;
        }

        Py::Dict arg;
        if (parameters) {
            arg = Py::Dict(parameters);
        }

        for (const auto& it : arg) {
            std::string ckey;
            Py::Object key(it.first);
            if (key.isString()) {
                ckey = static_cast<std::string>(Py::String(key));
                boost::to_upper(ckey);
            }
            else {
                PyErr_SetString(PyExc_TypeError, "The dictionary can only contain string keys");
                return -1;
            }

            double cvalue {};
            Py::Object value(it.second);
            if (value.isNumeric()) {
                cvalue = static_cast<double>(Py::Float(value));
            }
            else {
                PyErr_SetString(PyExc_TypeError, "The dictionary can only contain number values");
                return -1;
            }

            getCommandPtr()->Parameters[ckey] = cvalue;
        }

        parameters_copy_dict.clear();
        return 0;
    }
    PyErr_Clear();  // set by PyArg_ParseTuple()

    if (Base::Wrapped_ParseTupleAndKeywords(
            args,
            kwd,
            "|sO!",
            kwlist,
            &name,
            &(Base::PlacementPy::Type),
            &parameters
        )) {
        std::string sname(name);
        boost::to_upper(sname);
        try {
            if (!sname.empty()) {
                getCommandPtr()->setFromGCode(name);
            }
        }
        catch (const Base::Exception& e) {
            PyErr_SetString(PyExc_ValueError, e.what());
            return -1;
        }
        Base::PlacementPy* p = static_cast<Base::PlacementPy*>(parameters);
        getCommandPtr()->setFromPlacement(*p->getPlacementPtr());
        return 0;
    }
    return -1;
}

// Name attribute

Py::String CommandPy::getName() const
{
    return Py::String(getCommandPtr()->Name.c_str());
}

void CommandPy::setName(Py::String arg)
{
    std::string cmd = arg.as_std_string();
    boost::to_upper(cmd);
    getCommandPtr()->Name = cmd;
}

// Parameters attribute get/set

Py::Dict CommandPy::getParameters() const
{
    // dict now a class member , https://forum.freecad.org/viewtopic.php?f=15&t=50583
    if (parameters_copy_dict.length() == 0) {
        for (const auto& it : getCommandPtr()->Parameters) {
            parameters_copy_dict.setItem(it.first, Py::Float(it.second));
        }
    }
    return parameters_copy_dict;
}

void CommandPy::setParameters(Py::Dict arg)
{
    std::map<std::string, double> parameter;
    for (const auto& it : arg) {
        std::string ckey;
        Py::Object key(it.first);
        if (key.isString()) {
            ckey = static_cast<std::string>(Py::String(key));
            boost::to_upper(ckey);
        }
        else {
            throw Py::TypeError("The dictionary can only contain string keys");
        }

        double cvalue {};
        Py::Object value(it.second);
        if (value.isNumeric()) {
            cvalue = static_cast<double>(Py::Float(value));
        }
        else {
            throw Py::TypeError("The dictionary can only contain number values");
        }

        parameter[ckey] = cvalue;
    }

    getCommandPtr()->Parameters = parameter;
    parameters_copy_dict.clear();
}

// GCode methods

PyObject* CommandPy::toGCode(PyObject* args) const
{
    if (PyArg_ParseTuple(args, "")) {
        return PyUnicode_FromString(getCommandPtr()->toGCode().c_str());
    }
    throw Py::TypeError("This method accepts no argument");
}

PyObject* CommandPy::setFromGCode(PyObject* args)
{
    char* pstr = nullptr;
    if (PyArg_ParseTuple(args, "s", &pstr)) {
        std::string gcode(pstr);
        try {
            getCommandPtr()->setFromGCode(gcode);
            parameters_copy_dict.clear();
        }
        catch (const Base::Exception& e) {
            PyErr_SetString(PyExc_ValueError, e.what());
            return nullptr;
        }

        Py_INCREF(Py_None);
        return Py_None;
    }
    throw Py::TypeError("Argument must be a string");
}

// Placement attribute get/set

Py::Object CommandPy::getPlacement() const
{
    return Py::asObject(new Base::PlacementPy(new Base::Placement(getCommandPtr()->getPlacement())));
}

void CommandPy::setPlacement(Py::Object arg)
{
    Py::Type PlacementType(Base::getTypeAsObject(&(Base::PlacementPy::Type)));
    if (arg.isType(PlacementType)) {
        getCommandPtr()->setFromPlacement(*static_cast<Base::PlacementPy*>((*arg))->getPlacementPtr());
        parameters_copy_dict.clear();
    }
    else {
        throw Py::TypeError("Argument must be a placement");
    }
}

PyObject* CommandPy::transform(PyObject* args)
{
    PyObject* placement {};
    if (PyArg_ParseTuple(args, "O!", &(Base::PlacementPy::Type), &placement)) {
        Base::PlacementPy* p = static_cast<Base::PlacementPy*>(placement);
        Path::Command trCmd = getCommandPtr()->transform(*p->getPlacementPtr());
        parameters_copy_dict.clear();
        return new CommandPy(new Path::Command(trCmd));
    }

    throw Py::TypeError("Argument must be a placement");
}

// custom attributes get/set

PyObject* CommandPy::getCustomAttributes(const char* attr) const
{
    std::string satt(attr);
    if (satt.length() == 1) {
        if (isalpha(satt[0])) {
            boost::to_upper(satt);
            if (getCommandPtr()->Parameters.count(satt)) {
                return PyFloat_FromDouble(getCommandPtr()->Parameters[satt]);
            }
            Py_INCREF(Py_None);
            return Py_None;
        }
    }
    return nullptr;
}

int CommandPy::setCustomAttributes(const char* attr, PyObject* obj)
{
    std::string satt(attr);
    if (satt.length() == 1) {
        if (isalpha(satt[0])) {
            boost::to_upper(satt);
            double cvalue {};
            Py::Object value(obj);
            if (value.isNumeric()) {
                cvalue = static_cast<double>(Py::Float(value));
            }
            else {
                return 0;
            }

            getCommandPtr()->Parameters[satt] = cvalue;
            parameters_copy_dict.clear();
            return 1;
        }
    }
    return 0;
}
