// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2011 Juergen Riegel <FreeCAD@juergen-riegel.net>        *
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

#include <Precision.hxx>

#include <QLabel>
#include <QVBoxLayout>

#include <Gui/GeometrySelection.h>
#include <Gui/GeometrySelectorWidget.h>
#include <Mod/PartDesign/App/FeaturePad.h>
#include <Mod/PartDesign/App/FeatureSketchBased.h>

#include "EnumFlags.h"
#include "ReferenceSelection.h"
#include "ui_TaskPadPocketParameters.h"
#include "TaskPadParameters.h"


using namespace PartDesignGui;
using namespace Gui;

/* TRANSLATOR PartDesignGui::TaskPadParameters */

TaskPadParameters::TaskPadParameters(ViewProviderPad* PadView, QWidget* parent, bool newObj)
    : TaskExtrudeParameters(PadView, parent, "PartDesign_Pad", tr("Pad Parameters"))
{
    ui->offsetEdit->setToolTip(tr("Offset the pad from the face at which the pad will end on side 1"));
    ui->offsetEdit2->setToolTip(tr("Offset the pad from the face at which the pad will end on side 2"));
    ui->checkBoxReversed->setToolTip(tr("Reverses pad direction"));

    // set the history path
    ui->lengthEdit->setEntryName(QByteArray("Length"));
    ui->lengthEdit->setParamGrpPath(QByteArray("User parameter:BaseApp/History/PadLength"));
    ui->lengthEdit2->setEntryName(QByteArray("Length2"));
    ui->lengthEdit2->setParamGrpPath(QByteArray("User parameter:BaseApp/History/PadLength2"));
    ui->offsetEdit->setEntryName(QByteArray("Offset"));
    ui->offsetEdit->setParamGrpPath(QByteArray("User parameter:BaseApp/History/PadOffset"));
    ui->offsetEdit2->setEntryName(QByteArray("Offset2"));
    ui->offsetEdit2->setParamGrpPath(QByteArray("User parameter:BaseApp/History/PadOffset2"));
    ui->taperEdit->setEntryName(QByteArray("TaperAngle"));
    ui->taperEdit->setParamGrpPath(QByteArray("User parameter:BaseApp/History/PadTaperAngle"));
    ui->taperEdit2->setEntryName(QByteArray("TaperAngle2"));
    ui->taperEdit2->setParamGrpPath(QByteArray("User parameter:BaseApp/History/PadTaperAngle2"));

    setupDialog();

    // Profile selector — inserted above the shared extrude parameters proxy
    profileSelector = ui->profileGeometrySelector;
    profileSelector->setQuantity(GeometryQuantity::AllowMultiple);

    auto* core = profileSelector->selection();

    core->bind(getObject<PartDesign::ProfileBased>()->Profile);

    // While picking a profile, hide this Pad's transparent result preview and its
    // final solid so the profile is selected against the geometry that precedes it
    // (the visibility swap DressUp/Fillet use). The profile preview is left off too:
    // during a pick session the geometry highlighter's overlay is what tells the user
    // which reference is selected, so nothing else needs to draw the profile. The prior
    // visibility is snapshotted and restored on exit so the Preview box's own
    // checkboxes are respected.
    connect(core, &Gui::GeometrySelection::selectionModeEntered, this, [this] {
        auto* viewObject = getViewObject<PartDesignGui::ViewProviderSketchBased>();
        if (!viewObject) {
            return;
        }
        previewShownBeforeSelecting = viewObject->isPreviewEnabled();
        finalShownBeforeSelecting = viewObject->isVisible();
        viewObject->showPreview(false);
        viewObject->showPreviousFeature(true);
    });
    connect(core, &Gui::GeometrySelection::selectionModeExited, this, [this] {
        auto* viewObject = getViewObject<PartDesignGui::ViewProviderSketchBased>();
        if (!viewObject) {
            return;
        }
        viewObject->showPreview(previewShownBeforeSelecting);
        viewObject->showPreviousFeature(!finalShownBeforeSelecting);
    });
    connect(core, &Gui::GeometrySelection::referencesChanged, this, [this] { onProfileChanged(); });

    // if it is a newly created object use the last value of the history
    if (newObj) {
        readValuesFromHistory();
    }
}

TaskPadParameters::~TaskPadParameters() = default;

void TaskPadParameters::translateModeList(QComboBox* box, int index)
{
    box->clear();
    box->addItem(tr("Dimension"));
    box->addItem(tr("To last"));
    box->addItem(tr("To first"));
    box->addItem(tr("Up to face"));
    box->addItem(tr("Up to shape"));
    box->setCurrentIndex(index);
}

void TaskPadParameters::updateUI(Side side)
{
    // update direction combobox
    fillDirectionCombo();
    // set and enable checkboxes
    updateWholeUI(Type::Pad, side);
}

void TaskPadParameters::onModeChanged(int index, Side side)
{
    auto& sideCtrl = getSideController(side);

    switch (static_cast<Mode>(index)) {
        case Mode::Dimension:
            sideCtrl.Type->setValue("Length");
            if (side == Side::First) {
                // Avoid error message
                double L = sideCtrl.lengthEdit->value().getValue();
                Side otherSide = side == Side::First ? Side::Second : Side::First;
                auto& sideCtrl2 = getSideController(otherSide);
                double L2 = static_cast<SidesMode>(getSidesMode()) == SidesMode::TwoSides
                    ? sideCtrl2.lengthEdit->value().getValue()
                    : 0;
                if (std::abs(L + L2) < Precision::Confusion()) {
                    sideCtrl.lengthEdit->setValue(5.0);
                }
            }
            break;
        case Mode::ToLast:
            sideCtrl.Type->setValue("UpToLast");
            break;
        case Mode::ToFirst:
            sideCtrl.Type->setValue("UpToFirst");
            break;
        case Mode::ToFace:
            sideCtrl.Type->setValue("UpToFace");
            if (sideCtrl.faceSelector->selection()->references().empty()) {
                sideCtrl.faceSelector->selection()->startSelecting();
            }
            break;
        case Mode::ToShape:
            sideCtrl.Type->setValue("UpToShape");
            break;
    }

    updateUI(side);
    recomputeFeature();
}

void TaskPadParameters::apply()
{
    applyParameters();
}

//**************************************************************************
//**************************************************************************
// TaskDialog
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TaskDlgPadParameters::TaskDlgPadParameters(ViewProviderPad* PadView, bool /*newObj*/)
    : TaskDlgExtrudeParameters(PadView)
    , parameters(new TaskPadParameters(PadView))
{
    Content.push_back(parameters);
    Content.push_back(preview);
}

//==== calls from the TaskView ===============================================================

#include "moc_TaskPadParameters.cpp"
