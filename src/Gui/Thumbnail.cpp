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


#include <algorithm>
#include <array>
#include <cmath>

#include <QApplication>
#include <QBuffer>
#include <QByteArray>
#include <QDateTime>
#include <QImage>
#include <QThread>


#include <App/Application.h>
#include <Base/Reader.h>
#include <Base/Writer.h>
#ifdef _MSC_VER
# include <zipios++/zipios-config.h>
#endif
#include <zipios++/zipfile.h>

#include <Inventor/SbBox3f.h>
#include <Inventor/SbMatrix.h>
#include <Inventor/SbVec3f.h>
#include <Inventor/nodes/SoOrthographicCamera.h>

#include "Thumbnail.h"
#include "BitmapFactory.h"
#include "Camera.h"
#include "View3DInventorViewer.h"


using namespace Gui;

namespace
{
std::array<SbVec3f, 8> cornersOf(const SbBox3f& box)
{
    const SbVec3f low = box.getMin();
    const SbVec3f high = box.getMax();
    return {
        SbVec3f(low[0], low[1], low[2]),
        SbVec3f(high[0], low[1], low[2]),
        SbVec3f(low[0], high[1], low[2]),
        SbVec3f(high[0], high[1], low[2]),
        SbVec3f(low[0], low[1], high[2]),
        SbVec3f(high[0], low[1], high[2]),
        SbVec3f(low[0], high[1], high[2]),
        SbVec3f(high[0], high[1], high[2]),
    };
}
}  // namespace

Thumbnail::Thumbnail(int s)
    : size(s)
    , camera(new SoOrthographicCamera)
{
    camera->ref();
    camera->orientation.setValue(Camera::rotation(Camera::Isometric));
}

Thumbnail::~Thumbnail()
{
    camera->unref();
}

void Thumbnail::setViewer(View3DInventorViewer* v)
{
    this->viewer = v;
}

void Thumbnail::setSize(int s)
{
    this->size = s;
}

void Thumbnail::setFileName(const char* fn)
{
    this->uri = QUrl::fromLocalFile(QString::fromUtf8(fn));
}

void Thumbnail::fitToBox(SoOrthographicCamera& camera, const SbBox3f& box, float aspect)
{
    if (box.isEmpty()) {
        return;
    }

    SbMatrix intoCameraSpace;
    intoCameraSpace.setRotate(camera.orientation.getValue().inverse());

    // The eight corners of an axis-aligned box stay symmetric about its center under rotation,
    // so their projected extents are all the fit needs.
    const SbVec3f center = box.getCenter();
    float halfWidth = 0.0F;
    float halfHeight = 0.0F;
    for (const SbVec3f& corner : cornersOf(box)) {
        SbVec3f projected;
        intoCameraSpace.multVecMatrix(corner - center, projected);
        halfWidth = std::max(halfWidth, std::abs(projected[0]));
        halfHeight = std::max(halfHeight, std::abs(projected[1]));
    }

    const float halfExtent = std::max(halfHeight, halfWidth / aspect);
    if (halfExtent <= 0.0F) {
        // A box that projects to a single point offers no frame to fit, and sizing one from it
        // would leave the view volume degenerate.
        return;
    }

    // Coin aims the camera and sets the clipping planes correctly, but sizes the frame from
    // the circumscribing sphere, which leaves a lot of the image empty.
    //
    // Coin puts the clipping planes exactly tangent to the bounding sphere at a slack of 1,
    // which leaves no room for geometry that is rendered but excluded from the bounding box —
    // a sketch's edit-mode cross axes, for one. A slack of 2 clears the sphere by its own
    // radius on both sides. Orthographic projection is happy with a negative near distance.
    camera.viewBoundingBox(box, aspect, 2.0F);
    camera.height = 2.0F * halfExtent * fitMargin;
}

unsigned int Thumbnail::getMemSize() const
{
    return 0;
}

void Thumbnail::Save(Base::Writer& writer) const
{
    // It's only possible to add extra information if force of XML is disabled
    if (!writer.isForceXML()) {
        writer.addFile("thumbnails/Thumbnail.png", this);
    }
}

void Thumbnail::Restore(Base::XMLReader& reader)
{
    Q_UNUSED(reader);
    // reader.addFile("Thumbnail.png",this);
}

void Thumbnail::SaveDocFile(Base::Writer& writer) const
{
    QImage img;
    bool created = false;

    // 1. Try to create the thumbnail from the viewer
    if (this->viewer) {
        if (this->viewer->thread() != QThread::currentThread()) {
            qWarning("Cannot create a thumbnail from non-GUI thread");
        }
        else {
            // An empty document leaves the box empty and fitToBox no-ops, so it yields a fully
            // transparent thumbnail rather than falling back to a stale one of geometry that
            // has since been deleted.
            SbBox3f box;
            this->viewer->getSceneBoundBox(box);
            fitToBox(*this->camera, box, 1.0F);

            const View3DInventorViewer::RenderImageOptions options {
                .width = this->size,
                .height = this->size,
                .samples = 4,
                .background = QColor(0, 0, 0, 0),
                .intent = View3DInventorViewer::RenderIntent::RasterCapture,
                .includeViewerLighting = true,
                .camera = this->camera,
                .trueAlpha = true,
            };
            img = this->viewer->renderToImage(options);
            created = !img.isNull();
        }
    }

    // 2. If creation failed (e.g. no viewer or background thread), try to restore from the existing file
    if (!created) {
        QString filename = this->uri.toLocalFile();
        Base::FileInfo fi(filename.toUtf8().constData());
        if (fi.exists()) {
            try {
                zipios::ZipFile zf(fi.filePath());
                // getEntry uses default MatchPath=MATCH.
                zipios::ConstEntryPointer entry = zf.getEntry("thumbnails/Thumbnail.png");
                if (entry && entry->isValid()) {
                    // getInputStream returns a pointer that must be deleted
                    std::istream* is = zf.getInputStream(entry);
                    if (is) {
                        if (is->good()) {
                            writer.Stream() << is->rdbuf();
                            delete is;
                            return;
                        }
                        delete is;
                    }
                }
            }
            catch (const std::exception&) {
                // If the file isn't a zip or is locked, we ignore it and proceed to fallback
            }
            catch (...) {
                // Ignore unknown exceptions
            }
        }
    }

    // If we still have no image and no viewer to generate one, we can do nothing more
    if (!this->viewer) {
        return;
    }

    // Get app icon and resize to half size to insert in topbottom position over the current view
    // snapshot
    QPixmap appIcon = Gui::BitmapFactory().pixmap(App::Application::Config()["AppIcon"].c_str());
    QPixmap px = appIcon;
    if (!img.isNull()) {
        // Create a small "Fc" Application icon in the bottom right of the thumbnail
        if (App::GetApplication()
                .GetParameterGroupByPath("User parameter:BaseApp/Preferences/Document")
                ->GetBool("AddThumbnailLogo", false)) {
            // only scale app icon if an offscreen image could be created
            appIcon = appIcon.scaled(
                this->size / 4,
                this->size / 4,
                Qt::KeepAspectRatio,
                Qt::SmoothTransformation
            );
            px = BitmapFactory().merge(QPixmap::fromImage(img), appIcon, BitmapFactoryInst::BottomRight);
        }
        else {
            px = QPixmap::fromImage(img);
        }
    }

    if (!px.isNull()) {
        // according to specification add some meta-information to the image
        qint64 mt = QDateTime::currentDateTimeUtc().toSecsSinceEpoch();
        QString mtime = QStringLiteral("%1").arg(mt);
        img.setText(QLatin1String("Software"), qApp->applicationName());
        img.setText(QLatin1String("Thumb::Mimetype"), QLatin1String("application/x-extension-fcstd"));
        img.setText(QLatin1String("Thumb::MTime"), mtime);
        img.setText(QLatin1String("Thumb::URI"), this->uri.toString());

        QByteArray ba;
        QBuffer buffer(&ba);
        buffer.open(QIODevice::WriteOnly);
        px.save(&buffer, "PNG");
        writer.Stream().write(ba.constData(), ba.length());
    }
}

void Thumbnail::RestoreDocFile(Base::Reader& reader)
{
    Q_UNUSED(reader);
}
