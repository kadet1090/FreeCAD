/***************************************************************************
 *   Copyright (c) 2008 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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


#pragma once

#include <Base/Persistence.h>
#include <FCGlobal.h>
#include <QUrl>

class QImage;
class SbBox3f;
class SoOrthographicCamera;

namespace Gui
{
class View3DInventorViewer;

class GuiExport Thumbnail: public Base::Persistence
{
public:
    /// How much larger the frame is than the content it holds, so a fitted model does not
    /// touch the image edge.
    static constexpr float fitMargin = 1.1F;

    Thumbnail(int s = 128);
    ~Thumbnail() override;

    FC_DISABLE_COPY_MOVE(Thumbnail)

    /** Position and size @p camera so @p box fills a frame of the given aspect ratio, seen
     * from the camera's current orientation. Leaves the camera untouched for an empty box, or
     * for one that projects to a single point.
     * @param aspect frame width divided by frame height; must be greater than zero.
     */
    static void fitToBox(SoOrthographicCamera& camera, const SbBox3f& box, float aspect);

    void setViewer(View3DInventorViewer*);
    void setSize(int);
    void setFileName(const char*);

    /** @name I/O of the document */
    //@{
    unsigned int getMemSize() const override;
    /// This method is used to save properties or very small amounts of data to an XML document.
    void Save(Base::Writer& writer) const override;
    /// This method is used to restore properties from an XML document.
    void Restore(Base::XMLReader& reader) override;
    /// This method is used to save large amounts of data to a binary file.
    void SaveDocFile(Base::Writer& writer) const override;
    /// This method is used to restore large amounts of data from a binary file.
    void RestoreDocFile(Base::Reader& reader) override;
    //@}

private:
    QUrl uri;
    View3DInventorViewer* viewer {nullptr};
    int size;
    SoOrthographicCamera* camera {nullptr};
};

}  // namespace Gui
