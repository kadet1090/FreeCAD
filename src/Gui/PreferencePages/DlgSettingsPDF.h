// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
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


#ifndef GUI_DIALOG_DLGSETTINGSPDF_H
#define GUI_DIALOG_DLGSETTINGSPDF_H
 
#include <Gui/PropertyPage.h>
#include <QPagedPaintDevice>
#include <memory>
 
namespace Gui {
namespace Dialog {
class Ui_DlgSettingsPDF;
 
/**
* The DlgSettingsPDF class implements a preference page to change settings
* for the PDF Import-Export.
*/
class GuiExport DlgSettingsPDF: public PreferencePage
{
    Q_OBJECT
 
public:
    explicit DlgSettingsPDF(QWidget* parent = nullptr);
    ~DlgSettingsPDF() override;
 
    void saveSettings() override;
    void loadSettings() override;
    static QPagedPaintDevice::PdfVersion evaluatePDFVersion();
 
protected:
    void changeEvent(QEvent *e) override;
 
private:
    void onComboBoxIndexChanged(int index);

    std::unique_ptr<Ui_DlgSettingsPDF> ui;
 
//      Q_DISABLE_COPY_MOVE(DlgSettingsPDF)
};
 
} // namespace Dialog
} // namespace Gui
 
#endif // GUI_DIALOG_DLGSETTINGSPDF_H
