/***************************************************************************
 *   Copyright (c) 2023 David Friedli <david[at]friedli-be.ch>             *
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
# include <QApplication>
# include <Inventor/events/SoEvent.h>
#endif

#include <App/Application.h>
#include <App/Document.h>
#include <Gui/Action.h>
#include <Gui/Application.h>
#include <Gui/Command.h>
#include <Gui/Control.h>
#include <Gui/MainWindow.h>
#include <Gui/MDIView.h>
#include <Gui/View3DInventor.h>
#include <Gui/View3DInventorViewer.h>
#include <Gui/Selection/SelectionCallbackHandler.h>

#include "QuickMeasure.h"
#include "TaskMeasure.h"
#include "ViewProviderMeasureDistance.h"


//===========================================================================
// Std_Measure
// this is the Unified Measurement Facility Measure command
//===========================================================================


DEF_STD_CMD_A(StdCmdMeasure)

StdCmdMeasure::StdCmdMeasure()
    : Command("Std_Measure")
{
    sGroup = "Measure";
    sMenuText = QT_TR_NOOP("&Measure");
    sToolTipText = QT_TR_NOOP("Measure a feature");
    sWhatsThis = "Std_Measure";
    sStatusTip = QT_TR_NOOP("Measure a feature");
    sPixmap = "umf-measurement";
}

void StdCmdMeasure::activated(int iMsg)
{
    Q_UNUSED(iMsg);

    MeasureGui::TaskMeasure* task = new MeasureGui::TaskMeasure();
    task->setDocumentName(this->getDocument()->getName());
    Gui::Control().showDialog(task);
}

bool StdCmdMeasure::isActive()
{
    App::Document* doc = App::GetApplication().getActiveDocument();
    if (!doc || doc->countObjectsOfType<App::GeoFeature>() == 0) {
        return false;
    }

    Gui::MDIView* view = Gui::getMainWindow()->activeWindow();
    if (view && view->isDerivedFrom<Gui::View3DInventor>()) {
        Gui::View3DInventorViewer* viewer = dynamic_cast<Gui::View3DInventor*>(view)->getViewer();
        return !viewer->isEditing();
    }
    return false;
}

//===========================================================================
// Std_MeasureDistance
//===========================================================================

DEF_STD_CMD_A(StdCmdMeasureDistance)

StdCmdMeasureDistance::StdCmdMeasureDistance()
    : Command("Std_MeasureDistance")
{
    sGroup = "View";
    sMenuText = QT_TR_NOOP("Measure distance");
    sToolTipText = QT_TR_NOOP("Activate the distance measurement tool");
    sWhatsThis = "Std_MeasureDistance";
    sStatusTip = QT_TR_NOOP("Activate the distance measurement tool");
    sPixmap = "view-measurement";
    eType = Alter3DView;
}

void StdCmdMeasureDistance::activated(int iMsg)
{
    Q_UNUSED(iMsg);
    Gui::Document* doc = Gui::Application::Instance->activeDocument();
    auto view = static_cast<Gui::View3DInventor*>(doc->getActiveView());
    if (view) {
        Gui::View3DInventorViewer* viewer = view->getViewer();
        viewer->setEditing(true);
        // NOLINTBEGIN
        QCursor cursor = Gui::SelectionCallbackHandler::makeCursor(
            viewer,
            QSize(32, 32),
            "view-measurement-cross",
            6,
            25
        );
        viewer->setEditingCursor(cursor);
        // NOLINTEND
        // Derives from QObject and we have a parent object, so we don't
        // require a delete.
        auto marker = new MeasureGui::PointMarker(viewer);
        viewer->addEventCallback(
            SoEvent::getClassTypeId(),
            MeasureGui::ViewProviderMeasureDistance::measureDistanceCallback,
            marker
        );
    }
}

bool StdCmdMeasureDistance::isActive()
{
    App::Document* doc = App::GetApplication().getActiveDocument();
    if (!doc || doc->countObjectsOfType<App::GeoFeature>() == 0) {
        return false;
    }

    Gui::MDIView* view = Gui::getMainWindow()->activeWindow();
    if (view && view->isDerivedFrom(Gui::View3DInventor::getClassTypeId())) {
        Gui::View3DInventorViewer* viewer = static_cast<Gui::View3DInventor*>(view)->getViewer();
        return !viewer->isEditing();
    }

    return false;
}

class StdCmdQuickMeasure: public Gui::Command
{
public:
    StdCmdQuickMeasure()
        : Command("Std_QuickMeasure")
    {
        sGroup = "Measure";
        sMenuText = QT_TR_NOOP("&Quick measure");
        sToolTipText = QT_TR_NOOP("Toggle quick measure");
        sWhatsThis = "Std_QuickMeasure";
        sStatusTip = QT_TR_NOOP("Toggle quick measure");
        accessParameter();
    }
    ~StdCmdQuickMeasure() override = default;
    StdCmdQuickMeasure(const StdCmdQuickMeasure&) = delete;
    StdCmdQuickMeasure(StdCmdQuickMeasure&&) = delete;
    StdCmdQuickMeasure& operator=(const StdCmdQuickMeasure&) = delete;
    StdCmdQuickMeasure& operator=(StdCmdQuickMeasure&&) = delete;

    const char* className() const override
    {
        return "StdCmdQuickMeasure";
    }

protected:
    void activated(int iMsg) override
    {
        if (parameter.isValid()) {
            parameter->SetBool("EnableQuickMeasure", iMsg > 0);
        }

        if (iMsg == 0) {
            if (quickMeasure) {
                quickMeasure->print(QString());
            }
            quickMeasure.reset();
        }
        else {
            quickMeasure = std::make_unique<MeasureGui::QuickMeasure>(QApplication::instance());
        }
    }
    Gui::Action* createAction() override
    {
        Gui::Action* action = Gui::Command::createAction();
        action->setCheckable(true);
        action->setChecked(parameter->GetBool("EnableQuickMeasure", false));
        return action;
    }
    void accessParameter()
    {
        // clang-format off
        parameter = App::GetApplication().GetUserParameter().
                    GetGroup("BaseApp/Preferences/Mod/Measure");
        // clang-format on
    }

private:
    std::unique_ptr<MeasureGui::QuickMeasure> quickMeasure;
    ParameterGrp::handle parameter;
};

void CreateMeasureCommands()
{
    Gui::CommandManager& rcCmdMgr = Gui::Application::Instance->commandManager();

    auto cmd = new StdCmdMeasure();
    cmd->initAction();
    rcCmdMgr.addCommand(cmd);
    rcCmdMgr.addCommand(new StdCmdMeasureDistance());
    rcCmdMgr.addCommand(new StdCmdQuickMeasure);
}
