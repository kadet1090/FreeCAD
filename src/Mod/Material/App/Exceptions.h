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

#ifndef MATERIAL_EXCEPTIONS_H
#define MATERIAL_EXCEPTIONS_H

#include <QString>

#include <Base/Exception.h>
#include <Mod/Material/MaterialGlobal.h>

namespace Materials
{

class MaterialsExport Uninitialized: public Base::Exception
{
public:
    Uninitialized();
    explicit Uninitialized(const char* msg);
    explicit Uninitialized(const QString& msg);
    ~Uninitialized() noexcept override;
};

class MaterialsExport ModelNotFound: public Base::Exception
{
public:
    ModelNotFound();
    explicit ModelNotFound(const char* msg);
    explicit ModelNotFound(const QString& msg);
    ~ModelNotFound() noexcept override;
};

class MaterialsExport InvalidMaterialType: public Base::Exception
{
public:
    InvalidMaterialType();
    explicit InvalidMaterialType(const char* msg);
    explicit InvalidMaterialType(const QString& msg);
    ~InvalidMaterialType() noexcept override;
};

class MaterialsExport MaterialNotFound: public Base::Exception
{
public:
    MaterialNotFound();
    explicit MaterialNotFound(const char* msg);
    explicit MaterialNotFound(const QString& msg);
    ~MaterialNotFound() noexcept override;
};

class MaterialsExport MaterialExists: public Base::Exception
{
public:
    MaterialExists();
    explicit MaterialExists(const char* msg);
    explicit MaterialExists(const QString& msg);
    ~MaterialExists() noexcept override;
};

class MaterialsExport MaterialReadError: public Base::Exception
{
public:
    MaterialReadError();
    explicit MaterialReadError(const char* msg);
    explicit MaterialReadError(const QString& msg);
    ~MaterialReadError() noexcept override;
};

class MaterialsExport PropertyNotFound: public Base::Exception
{
public:
    PropertyNotFound();
    explicit PropertyNotFound(const char* msg);
    explicit PropertyNotFound(const QString& msg);
    ~PropertyNotFound() noexcept override;
};

class MaterialsExport LibraryNotFound: public Base::Exception
{
public:
    LibraryNotFound();
    explicit LibraryNotFound(const char* msg);
    explicit LibraryNotFound(const QString& msg);
    ~LibraryNotFound() noexcept override;
};

class MaterialsExport InvalidModel: public Base::Exception
{
public:
    InvalidModel();
    explicit InvalidModel(const char* msg);
    explicit InvalidModel(const QString& msg);
    ~InvalidModel() noexcept override;
};

class MaterialsExport InvalidIndex: public Base::Exception
{
public:
    InvalidIndex();
    explicit InvalidIndex(const char* msg);
    explicit InvalidIndex(const QString& msg);
    ~InvalidIndex() noexcept override;
};

class MaterialsExport UnknownValueType: public Base::Exception
{
public:
    UnknownValueType();
    explicit UnknownValueType(const char* msg);
    explicit UnknownValueType(const QString& msg);
    ~UnknownValueType() noexcept override;
};

class MaterialsExport DeleteError: public Base::Exception
{
public:
    DeleteError();
    explicit DeleteError(const char* msg);
    explicit DeleteError(const QString& msg);
    ~DeleteError() noexcept override;
};

class MaterialsExport InvalidProperty: public Base::Exception
{
public:
    InvalidProperty();
    explicit InvalidProperty(const char* msg);
    explicit InvalidProperty(const QString& msg);
    ~InvalidProperty() noexcept override;
};

class MaterialsExport InvalidLibrary: public Base::Exception
{
public:
    InvalidLibrary();
    explicit InvalidLibrary(const char* msg);
    explicit InvalidLibrary(const QString& msg);
    ~InvalidLibrary() noexcept override;
};

class MaterialsExport RenameError: public Base::Exception
{
public:
    RenameError();
    explicit RenameError(const char* msg);
    explicit RenameError(const QString& msg);
    ~RenameError() noexcept override;
};

class MaterialsExport ReplacementError: public Base::Exception
{
public:
    ReplacementError();
    explicit ReplacementError(const char* msg);
    explicit ReplacementError(const QString& msg);
    ~ReplacementError() noexcept override;
};

class MaterialsExport ConnectionError: public Base::Exception
{
public:
    ConnectionError();
    explicit ConnectionError(const char* msg);
    explicit ConnectionError(const QString& msg);
    ~ConnectionError() noexcept override;
};

}  // namespace Materials

#endif  // MATERIAL_EXCEPTIONS_H
