/***************************************************************************
 *   Copyright (c) 2009 Jürgen Riegel <juergen.riegel@web.de>              *
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
# include <QEvent>
# include <QGridLayout>
# include <QSplitter>
#endif

#include "ComboView.h"
#include "PropertyView.h"
#include "Tree.h"


using namespace Gui;
using namespace Gui::DockWnd;

/* TRANSLATOR Gui::DockWnd::ComboView */

ComboView::ComboView(Gui::Document* pcDocument, QWidget *parent)
  : DockWindow(pcDocument,parent)
{
    hGrp = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/DockWindows/ComboView");

    auto pLayout = new QGridLayout(this);
    pLayout->setSpacing( 0 );
    pLayout->setContentsMargins ( 0, 0, 0, 0 );

    // tabs to switch between Tree/Properties and TaskPanel
    auto splitter = new QSplitter();
    pLayout->addWidget( splitter, 0, 0 );

    // splitter between tree and property view
    splitter->setOrientation(Qt::Vertical);

    int treeViewSize = hGrp->GetInt("TreeViewSize", 0);
    int propertyViewSize = hGrp->GetInt("PropertyViewSize", 0);

    if (treeViewSize || propertyViewSize) {
        splitter->setSizes({ 
            treeViewSize, 
            propertyViewSize 
        });
    }

    connect(splitter, SIGNAL(splitterMoved(int, int)), this, SLOT(onSplitterMoved()));

    tree =  new TreePanel("ComboView", this);
    splitter->addWidget(tree);

    // property view
    prop = new PropertyView(this);
    splitter->addWidget(prop);
}

ComboView::~ComboView() = default;

void ComboView::onSplitterMoved()
{
    auto splitter = qobject_cast<QSplitter*>(sender());
    if (splitter) {
        auto sizes = splitter->sizes();
        hGrp->SetInt("TreeViewSize", sizes[0]);
        hGrp->SetInt("PropertyViewSize", sizes[1]);
    }
}

#include "moc_ComboView.cpp"
