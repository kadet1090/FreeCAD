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


#include "PreCompiled.h"
 
#include <App/Application.h>
#include <Base/Console.h>
 
#include "DlgSettingsPDF.h"
#include "ui_DlgSettingsPDF.h"
 
 
using namespace Gui::Dialog;
 
/* TRANSLATOR Gui::Dialog::DlgSettingsPDF */
 
DlgSettingsPDF::DlgSettingsPDF(QWidget* parent)
    : PreferencePage( parent )
    , ui(new Ui_DlgSettingsPDF)
{
    ui->setupUi(this);
    ui->warningLabel->setWordWrap(true);
    connect(ui->comboBox,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            &DlgSettingsPDF::onComboBoxIndexChanged);
}
 
DlgSettingsPDF::~DlgSettingsPDF() = default;

void DlgSettingsPDF::saveSettings()
{
    ui->comboBox->onSave();
}

void DlgSettingsPDF::loadSettings()
{
    ui->comboBox->onRestore();
    int currentIndex = ui->comboBox->currentIndex();
#if QT_VERSION < QT_VERSION_CHECK(6,8,0)
    if (currentIndex == 3) {
        Base::Console().Warning("When using another copy of FreeCAD you had set the PDF version to X4, but this build of FreeCAD does not support it. Using 1.4 instead.\n");
        currentIndex = 0;
        ui->comboBox->setCurrentIndex(0);
    }
    ui->comboBox->removeItem(3);  // PdfVersion_X4
#endif
    onComboBoxIndexChanged(currentIndex);
}

QPagedPaintDevice::PdfVersion DlgSettingsPDF::evaluatePDFVersion()
{
    auto hGrp = App::GetApplication()
                    .GetUserParameter()
                    .GetGroup("BaseApp")
                    ->GetGroup("Preferences")
                    ->GetGroup("Mod");

    const int ver = hGrp->GetInt("PDFVersion");

    switch (ver) {
        case 1:
            return QPagedPaintDevice::PdfVersion_A1b;
        case 2:
            return QPagedPaintDevice::PdfVersion_1_6;
#if QT_VERSION >= QT_VERSION_CHECK(6,8,0)
        case 3:
            return QPagedPaintDevice::PdfVersion_X4;
#endif
        default:
            return QPagedPaintDevice::PdfVersion_1_4;
    }
}

void DlgSettingsPDF::onComboBoxIndexChanged(int index)
{
    switch (index) {
        case 1:
            ui->warningLabel->setText(
                tr("This archival PDF format does not support transparency or layers. All content must be self-contained and static."));
            break;
        case 2:
            ui->warningLabel->setText(
                tr("While this version supports more modern features, older PDF readers may not fully handle it."));
            break;
        case 3:
            ui->warningLabel->setText(
                tr("This PDF format is intended for professional printing and requires all fonts to be embedded; some interactive features may not be supported."));
            break;
        default:
            ui->warningLabel->setText(
                tr("This PDF version has limited support for modern features like embedded multimedia and advanced transparency effects."));
            break;
    }
}

void DlgSettingsPDF::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
    }

    QWidget::changeEvent(e);
}
 
#include "moc_DlgSettingsPDF.cpp"
