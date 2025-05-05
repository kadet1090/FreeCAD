# ***************************************************************************
# *   Copyright (c) 2013 Yorik van Havre <yorik@uncreated.net>              *
# *   Copyright (c) 2019 Eliud Cabrera Castillo <e.cabrera-castillo@tum.de> *
# *   Copyright (c) 2025 FreeCAD Project Association                        *
# *                                                                         *
# *   This file is part of the FreeCAD CAx development system.              *
# *                                                                         *
# *   This program is free software; you can redistribute it and/or modify  *
# *   it under the terms of the GNU Lesser General Public License (LGPL)    *
# *   as published by the Free Software Foundation; either version 2 of     *
# *   the License, or (at your option) any later version.                   *
# *   for detail see the LICENCE text file.                                 *
# *                                                                         *
# *   FreeCAD is distributed in the hope that it will be useful,            *
# *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
# *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
# *   GNU Library General Public License for more details.                  *
# *                                                                         *
# *   You should have received a copy of the GNU Library General Public     *
# *   License along with FreeCAD; if not, write to the Free Software        *
# *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  *
# *   USA                                                                   *
# *                                                                         *
# ***************************************************************************

"""Unit tests for the Draft Workbench, DXF import and export tests."""

## @package test_dxf
# \ingroup drafttests
# \brief Unit tests for the Draft Workbench, DXF import and export tests.

## \addtogroup drafttests
# @{

import os

import FreeCAD as App
import Draft
from drafttests import auxiliary as aux
from drafttests import test_base
from draftutils.messages import _msg


class DraftDXF(test_base.DraftTestCaseDoc):
    """Test reading and writing of DXF files with Draft."""

    def test_read_dxf(self):
        """Read a DXF file and import its elements as Draft objects."""
        operation = "importDXF.import"
        _msg("  Test '{}'".format(operation))
        _msg("  This test requires a DXF file to read.")

        file = "Mod/Draft/drafttest/test.dxf"
        in_file = os.path.join(App.getResourceDir(), file)
        _msg("  file={}".format(in_file))
        _msg("  exists={}".format(os.path.exists(in_file)))

        Draft.import_dxf = aux.fake_function
        obj = Draft.import_dxf(in_file)
        self.assertTrue(obj, "'{}' failed".format(operation))

    def test_export_dxf(self):
        """Create some figures and export them to a DXF file."""
        operation = "importDXF.export"
        _msg("  Test '{}'".format(operation))

        file = "Mod/Draft/drafttest/out_test.dxf"
        out_file = os.path.join(App.getResourceDir(), file)
        _msg("  file={}".format(out_file))
        _msg("  exists={}".format(os.path.exists(out_file)))

        Draft.export_dxf = aux.fake_function
        obj = Draft.export_dxf(out_file)
        self.assertTrue(obj, "'{}' failed".format(operation))

    def testDXFImportCPPIssue20195(self):
        operation = "importDXF.import"
        _msg("  Test '{}'".format(operation))
        _msg("  This test requires a DXF file to read.")
        import importDXF
        from draftutils import params

        # Set options, doing our best to restore them:
        wasShowDialog = params.get_param("dxfShowDialog")
        wasUseLayers = params.get_param("dxfUseDraftVisGroups")
        wasUseLegacyImporter = params.get_param("dxfUseLegacyImporter")
        wasCreatePart = params.get_param("dxfCreatePart")
        wasCreateDraft = params.get_param("dxfCreateDraft")
        wasCreateSketch = params.get_param("dxfCreateSketch")

        try:
            # disable Preferences dialog in gui mode (avoids popup prompt to user)
            params.set_param("dxfShowDialog", False)
            # Preserve the DXF layers (makes the checking of document contents easier)
            params.set_param("dxfUseDraftVisGroups", True)
            # Use the new C++ importer -- that's where the bug was
            params.set_param("dxfUseLegacyImporter", False)
            # create simple part shapes (3 params)
            # This is required to display the bug because creation of Draft objects clears out the
            # pending exception this test is looking for, whereas creation of the simple shape object
            # actually throws on the pending exception so the entity is absent from the document.
            params.set_param("dxfCreatePart", True)
            params.set_param("dxfCreateDraft", False)
            params.set_param("dxfCreateSketch", False)
            importDXF.insert(
                FreeCAD.getHomePath() + "Mod/Test/TestData/DXFSample.dxf", "ImportedDocName"
            )
        finally:
            params.set_param("dxfShowDialog", wasShowDialog)
            params.set_param("dxfUseDraftVisGroups", wasUseLayers)
            params.set_param("dxfUseLegacyImporter", wasUseLegacyImporter)
            params.set_param("dxfCreatePart", wasCreatePart)
            params.set_param("dxfCreateDraft", wasCreateDraft)
            params.set_param("dxfCreateSketch", wasCreateSketch)
        doc = FreeCAD.getDocument("ImportedDocName")
        # This doc should ahve 3 objects: The Layers containter, the DXF layer called 0, and one Line
        self.assertEqual(len(doc.Objects), 3)
        FreeCAD.closeDocument("ImportedDocName")

## @}
