# ***************************************************************************
# *   Copyright (c) 2025 Werner Mayer <wmayer[at]users.sourceforge.net>     *
# *                                                                         *
# *   This file is part of FreeCAD.                                         *
# *                                                                         *
# *   FreeCAD is free software: you can redistribute it and/or modify it    *
# *   under the terms of the GNU Lesser General Public License as           *
# *   published by the Free Software Foundation, either version 2.1 of the  *
# *   License, or (at your option) any later version.                       *
# *                                                                         *
# *   FreeCAD is distributed in the hope that it will be useful, but        *
# *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
# *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU      *
# *   Lesser General Public License for more details.                       *
# *                                                                         *
# *   You should have received a copy of the GNU Lesser General Public      *
# *   License along with FreeCAD. If not, see                               *
# *   <https://www.gnu.org/licenses/>.                                      *
# ***************************************************************************/


import unittest
import FreeCAD
import Part
import Sketcher

App = FreeCAD


# ---------------------------------------------------------------------------
# define the test cases to test the FreeCAD Sketcher module
# ---------------------------------------------------------------------------


class TestSketcherSpline(unittest.TestCase):
    def setUp(self):
        self.Doc = FreeCAD.newDocument("SketchSplineTest")

    def tearDown(self):
        FreeCAD.closeDocument("SketchSplineTest")

    def testPeriodicSpline(self):
        # Test issue https://github.com/FreeCAD/FreeCAD/issues/20815
        sketch = self.Doc.addObject("Sketcher::SketchObject", "Sketch")
        sketch.addGeometry(Part.Point(App.Vector(-44.553844, 22.012373, 0)), True)
        sketch.addGeometry(Part.Point(App.Vector(-20.045523, 34.417820, 0)), True)
        sketch.addGeometry(Part.Point(App.Vector(17.775951, 19.440510, 0)), True)
        sketch.addGeometry(Part.Point(App.Vector(-9.304221, -22.314404, 0)), True)
        sketch.addGeometry(Part.Point(App.Vector(-38.804981, -11.119246, 0)), True)

        _finalbsp_poles = []
        _finalbsp_knots = []
        _finalbsp_mults = []
        _bsps = []
        _bsps.append(Part.BSplineCurve())
        _bsps[-1].interpolate(
            [
                App.Vector(-44.5538, 22.0124),
                App.Vector(-20.0455, 34.4178),
                App.Vector(17.776, 19.4405),
                App.Vector(-9.30422, -22.3144),
                App.Vector(-38.805, -11.1192),
            ],
            PeriodicFlag=True,
        )
        _bsps[-1].increaseDegree(3)
        _finalbsp_poles.extend(_bsps[-1].getPoles())
        _finalbsp_knots.extend(_bsps[-1].getKnots())
        _finalbsp_mults.extend(_bsps[-1].getMultiplicities())
        sketch.addGeometry(
            Part.BSplineCurve(
                _finalbsp_poles, _finalbsp_mults, _finalbsp_knots, True, 3, None, False
            ),
            False,
        )

        conList = []
        conList.append(
            Sketcher.Constraint("InternalAlignment:Sketcher::BSplineKnotPoint", 0, 1, 5, 0)
        )
        conList.append(
            Sketcher.Constraint("InternalAlignment:Sketcher::BSplineKnotPoint", 1, 1, 5, 1)
        )
        conList.append(
            Sketcher.Constraint("InternalAlignment:Sketcher::BSplineKnotPoint", 2, 1, 5, 2)
        )
        conList.append(
            Sketcher.Constraint("InternalAlignment:Sketcher::BSplineKnotPoint", 3, 1, 5, 3)
        )
        conList.append(
            Sketcher.Constraint("InternalAlignment:Sketcher::BSplineKnotPoint", 4, 1, 5, 4)
        )

        sketch.addConstraint(conList)
        sketch.exposeInternalGeometry(5)
        self.Doc.recompute()
