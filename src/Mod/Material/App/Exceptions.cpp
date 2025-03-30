/***************************************************************************
 *   Copyright (c) 2023 David Carter <dcarter@david.carter.ca>             *
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
#include "Exceptions.h"

using namespace Materials;

Uninitialized::Uninitialized() = default;

Uninitialized::Uninitialized(const char* msg)
{
    this->setMessage(msg);
}

Uninitialized::Uninitialized(const QString& msg)
{
    this->setMessage(msg.toStdString().c_str());
}

Uninitialized::~Uninitialized() noexcept = default;

// ----------------------------------------------------------------------------

ModelNotFound::ModelNotFound()
{
    this->setMessage("Model not found");
}

ModelNotFound::ModelNotFound(const char* msg)
{
    this->setMessage(msg);
}

ModelNotFound::ModelNotFound(const QString& msg)
{
    this->setMessage(msg.toStdString().c_str());
}

ModelNotFound::~ModelNotFound() noexcept = default;

// ----------------------------------------------------------------------------

InvalidMaterialType::InvalidMaterialType() = default;

InvalidMaterialType::InvalidMaterialType(const char* msg)
{
    this->setMessage(msg);
}

InvalidMaterialType::InvalidMaterialType(const QString& msg)
{
    this->setMessage(msg.toStdString().c_str());
}

InvalidMaterialType::~InvalidMaterialType() noexcept = default;

// ----------------------------------------------------------------------------

MaterialNotFound::MaterialNotFound()
{
    this->setMessage("Material not found");
}

MaterialNotFound::MaterialNotFound(const char* msg)
{
    this->setMessage(msg);
}

MaterialNotFound::MaterialNotFound(const QString& msg)
{
    this->setMessage(msg.toStdString().c_str());
}

MaterialNotFound::~MaterialNotFound() noexcept = default;

// ----------------------------------------------------------------------------

MaterialExists::MaterialExists() = default;

MaterialExists::MaterialExists(const char* msg)
{
    this->setMessage(msg);
}

MaterialExists::MaterialExists(const QString& msg)
{
    this->setMessage(msg.toStdString().c_str());
}

MaterialExists::~MaterialExists() noexcept = default;

// ----------------------------------------------------------------------------

MaterialReadError::MaterialReadError() = default;

MaterialReadError::MaterialReadError(const char* msg)
{
    this->setMessage(msg);
}

MaterialReadError::MaterialReadError(const QString& msg)
{
    this->setMessage(msg.toStdString().c_str());
}

MaterialReadError::~MaterialReadError() noexcept = default;

// ----------------------------------------------------------------------------

PropertyNotFound::PropertyNotFound()
{
    this->setMessage("Property not found");
}

PropertyNotFound::PropertyNotFound(const char* msg)
{
    this->setMessage(msg);
}

PropertyNotFound::PropertyNotFound(const QString& msg)
{
    this->setMessage(msg.toStdString().c_str());
}

PropertyNotFound::~PropertyNotFound() noexcept = default;

// ----------------------------------------------------------------------------

LibraryNotFound::LibraryNotFound()
{
    this->setMessage("Library not found");
}

LibraryNotFound::LibraryNotFound(const char* msg)
{
    this->setMessage(msg);
}

LibraryNotFound::LibraryNotFound(const QString& msg)
{
    this->setMessage(msg.toStdString().c_str());
}

LibraryNotFound::~LibraryNotFound() noexcept = default;

// ----------------------------------------------------------------------------

InvalidModel::InvalidModel()
{
    this->setMessage("Invalid model");
}

InvalidModel::InvalidModel(const char* msg)
{
    this->setMessage(msg);
}

InvalidModel::InvalidModel(const QString& msg)
{
    this->setMessage(msg.toStdString().c_str());
}

InvalidModel::~InvalidModel() noexcept = default;

// ----------------------------------------------------------------------------

InvalidIndex::InvalidIndex()
{
    this->setMessage("Invalid index");
}

InvalidIndex::InvalidIndex(char* msg)
{
    this->setMessage(msg);
}

InvalidIndex::InvalidIndex(const QString& msg)
{
    this->setMessage(msg.toStdString().c_str());
}

InvalidIndex::~InvalidIndex() noexcept = default;

// ----------------------------------------------------------------------------

UnknownValueType::UnknownValueType() = default;

UnknownValueType::UnknownValueType(char* msg)
{
    this->setMessage(msg);
}

UnknownValueType::UnknownValueType(const QString& msg)
{
    this->setMessage(msg.toStdString().c_str());
}

UnknownValueType::~UnknownValueType() noexcept = default;

// ----------------------------------------------------------------------------

DeleteError::DeleteError() = default;

DeleteError::DeleteError(char* msg)
{
    this->setMessage(msg);
}

DeleteError::DeleteError(const QString& msg)
{
    this->setMessage(msg.toStdString().c_str());
}

DeleteError::~DeleteError() noexcept = default;
