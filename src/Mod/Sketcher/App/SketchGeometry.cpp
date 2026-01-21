// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2025 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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

#include "SketchGeometry.h"
#include "SketchObject.h"
#include <Mod/Part/App/Geometry.h>

using namespace Sketcher;

namespace Sketcher
{

// Handle ellipses and arcs of ellipses
template<typename GeometryT>
class SketchEllipticT
{
public:
    int exposeInternalGeometry(int GeoId, const Part::Geometry* geo, SketchObject* sketch) const
    {
        // First we search what has to be restored
        bool major = false;
        bool minor = false;
        bool focus1 = false;
        bool focus2 = false;

        const std::vector<Sketcher::Constraint*>& vals = sketch->Constraints.getValues();

        for (const auto& constr : vals) {
            if (constr->Type != Sketcher::InternalAlignment || constr->Second != GeoId) {
                continue;
            }

            switch (constr->AlignmentType) {
                case Sketcher::EllipseMajorDiameter:
                    major = true;
                    break;
                case Sketcher::EllipseMinorDiameter:
                    minor = true;
                    break;
                case Sketcher::EllipseFocus1:
                    focus1 = true;
                    break;
                case Sketcher::EllipseFocus2:
                    focus2 = true;
                    break;
                default:
                    return -1;
            }
        }

        int currentgeoid = sketch->getHighestCurveIndex();
        int incrgeo = 0;

        std::vector<Part::Geometry*> igeo;
        std::vector<Constraint*> icon;

        const auto* geom = dynamic_cast<const GeometryT*>(geo);

        Base::Vector3d center {geom->getCenter()};
        double majord {geom->getMajorRadius()};
        double minord {geom->getMinorRadius()};
        Base::Vector3d majdir {geom->getMajorAxisDir()};

        Base::Vector3d mindir(-majdir.y, majdir.x);

        Base::Vector3d majorpositiveend = center + majord * majdir;
        Base::Vector3d majornegativeend = center - majord * majdir;
        Base::Vector3d minorpositiveend = center + minord * mindir;
        Base::Vector3d minornegativeend = center - minord * mindir;

        double df = sqrt(majord * majord - minord * minord);

        Base::Vector3d focus1P = center + df * majdir;
        Base::Vector3d focus2P = center - df * majdir;

        if (!major) {
            auto lmajor = new Part::GeomLineSegment();
            lmajor->setPoints(majorpositiveend, majornegativeend);

            igeo.push_back(lmajor);

            auto newConstr = new Sketcher::Constraint();
            newConstr->Type = Sketcher::InternalAlignment;
            newConstr->AlignmentType = EllipseMajorDiameter;
            newConstr->First = currentgeoid + incrgeo + 1;
            newConstr->Second = GeoId;

            icon.push_back(newConstr);
            incrgeo++;
        }
        if (!minor) {
            auto lminor = new Part::GeomLineSegment();
            lminor->setPoints(minorpositiveend, minornegativeend);

            igeo.push_back(lminor);

            auto newConstr = new Sketcher::Constraint();
            newConstr->Type = Sketcher::InternalAlignment;
            newConstr->AlignmentType = EllipseMinorDiameter;
            newConstr->First = currentgeoid + incrgeo + 1;
            newConstr->Second = GeoId;

            icon.push_back(newConstr);
            incrgeo++;
        }
        if (!focus1) {
            auto pf1 = new Part::GeomPoint();
            pf1->setPoint(focus1P);

            igeo.push_back(pf1);

            auto newConstr = new Sketcher::Constraint();
            newConstr->Type = Sketcher::InternalAlignment;
            newConstr->AlignmentType = EllipseFocus1;
            newConstr->First = currentgeoid + incrgeo + 1;
            newConstr->FirstPos = Sketcher::PointPos::start;
            newConstr->Second = GeoId;

            icon.push_back(newConstr);
            incrgeo++;
        }
        if (!focus2) {
            auto pf2 = new Part::GeomPoint();
            pf2->setPoint(focus2P);
            igeo.push_back(pf2);

            auto newConstr = new Sketcher::Constraint();
            newConstr->Type = Sketcher::InternalAlignment;
            newConstr->AlignmentType = EllipseFocus2;
            newConstr->First = currentgeoid + incrgeo + 1;
            newConstr->FirstPos = Sketcher::PointPos::start;
            newConstr->Second = GeoId;

            icon.push_back(newConstr);
            incrgeo++;
        }

        sketch->addGeometry(igeo, true);
        sketch->addConstraints(icon);

        for (auto& geo : igeo) {
            delete geo;
        }

        for (auto& con : icon) {
            delete con;
        }

        return incrgeo;  // number of added elements
    }
};

template<typename GeometryT>
class SketchGeometryT: public SketchGeometry
{
public:
    using GeomType = GeometryT;
    bool supports(const Part::Geometry* geo) const override
    {
        return geo->is<GeomType>();
    }
};

class SketchPoint: public SketchGeometryT<Part::GeomPoint>
{
public:
    Base::Vector3d getPoint(const Part::Geometry* geo, PointPos PosId) const override
    {
        auto geomPoint = dynamic_cast<const GeomType*>(geo);
        if (PosId == PointPos::start || PosId == PointPos::mid || PosId == PointPos::end) {
            return geomPoint->getPoint();
        }

        return Base::Vector3d();
    }
};

class SketchLineSegment: public SketchGeometryT<Part::GeomLineSegment>
{
public:
    Base::Vector3d getPoint(const Part::Geometry* geo, PointPos PosId) const override
    {
        auto lineSeg = dynamic_cast<const GeomType*>(geo);
        switch (PosId) {
            case PointPos::start: {
                return lineSeg->getStartPoint();
            }
            case PointPos::end: {
                return lineSeg->getEndPoint();
            }
            default:
                break;
        }
        return Base::Vector3d();
    }
};

class SketchCircle: public SketchGeometryT<Part::GeomCircle>
{
public:
    Base::Vector3d getPoint(const Part::Geometry* geo, PointPos PosId) const override
    {
        auto circle = dynamic_cast<const GeomType*>(geo);
        auto pt = circle->getCenter();
        if (PosId != PointPos::mid) {
            pt.x += circle->getRadius();
        }
        return pt;
    }
};

class SketchEllipse: public SketchGeometryT<Part::GeomEllipse>
{
public:
    Base::Vector3d getPoint(const Part::Geometry* geo, PointPos PosId) const override
    {
        auto ellipse = dynamic_cast<const GeomType*>(geo);
        auto pt = ellipse->getCenter();
        if (PosId != PointPos::mid) {
            pt += ellipse->getMajorAxisDir() * ellipse->getMajorRadius();
        }
        return pt;
    }
    int exposeInternalGeometry(int GeoId, const Part::Geometry* geo, SketchObject* sketch) const override
    {
        SketchEllipticT<GeomType> sketchgeo;
        return sketchgeo.exposeInternalGeometry(GeoId, geo, sketch);
    }
};

class SketchArcOfCircle: public SketchGeometryT<Part::GeomArcOfCircle>
{
public:
    Base::Vector3d getPoint(const Part::Geometry* geo, PointPos PosId) const override
    {
        auto aoc = dynamic_cast<const GeomType*>(geo);
        const bool emulateCCW = true;
        switch (PosId) {
            case PointPos::start: {
                return aoc->getStartPoint(emulateCCW);
            }
            case PointPos::end: {
                return aoc->getEndPoint(emulateCCW);
            }
            case PointPos::mid: {
                return aoc->getCenter();
            }
            default:
                break;
        }
        return Base::Vector3d();
    }
};

class SketchArcOfEllipse: public SketchGeometryT<Part::GeomArcOfEllipse>
{
public:
    Base::Vector3d getPoint(const Part::Geometry* geo, PointPos PosId) const override
    {
        auto aoe = dynamic_cast<const GeomType*>(geo);
        const bool emulateCCW = true;
        switch (PosId) {
            case PointPos::start: {
                return aoe->getStartPoint(emulateCCW);
            }
            case PointPos::end: {
                return aoe->getEndPoint(emulateCCW);
            }
            case PointPos::mid: {
                return aoe->getCenter();
            }
            default:
                break;
        }
        return Base::Vector3d();
    }
    int exposeInternalGeometry(int GeoId, const Part::Geometry* geo, SketchObject* sketch) const override
    {
        SketchEllipticT<GeomType> sketchgeo;
        return sketchgeo.exposeInternalGeometry(GeoId, geo, sketch);
    }
};

class SketchArcOfHyperbola: public SketchGeometryT<Part::GeomArcOfHyperbola>
{
public:
    Base::Vector3d getPoint(const Part::Geometry* geo, PointPos PosId) const override
    {
        auto aoh = dynamic_cast<const GeomType*>(geo);
        switch (PosId) {
            case PointPos::start: {
                return aoh->getStartPoint();
            }
            case PointPos::end: {
                return aoh->getEndPoint();
            }
            case PointPos::mid: {
                return aoh->getCenter();
            }
            default:
                break;
        }
        return Base::Vector3d();
    }
    int exposeInternalGeometry(int GeoId, const Part::Geometry* geo, SketchObject* sketch) const override
    {
        // First we search what has to be restored
        bool major = false;
        bool minor = false;
        bool focus = false;

        const std::vector<Sketcher::Constraint*>& vals = sketch->Constraints.getValues();

        for (auto const& constr : vals) {
            if (constr->Type != Sketcher::InternalAlignment || constr->Second != GeoId) {
                continue;
            }

            switch (constr->AlignmentType) {
                case Sketcher::HyperbolaMajor:
                    major = true;
                    break;
                case Sketcher::HyperbolaMinor:
                    minor = true;
                    break;
                case Sketcher::HyperbolaFocus:
                    focus = true;
                    break;
                default:
                    return -1;
            }
        }

        int currentgeoid = sketch->getHighestCurveIndex();
        int incrgeo = 0;

        const auto* aoh = static_cast<const GeomType*>(geo);

        Base::Vector3d center {aoh->getCenter()};
        double majord {aoh->getMajorRadius()};
        double minord {aoh->getMinorRadius()};
        Base::Vector3d majdir {aoh->getMajorAxisDir()};

        std::vector<Part::Geometry*> igeo;
        std::vector<Constraint*> icon;

        Base::Vector3d mindir(-majdir.y, majdir.x);

        Base::Vector3d majorpositiveend = center + majord * majdir;
        Base::Vector3d majornegativeend = center - majord * majdir;
        Base::Vector3d minorpositiveend = majorpositiveend + minord * mindir;
        Base::Vector3d minornegativeend = majorpositiveend - minord * mindir;

        double df = sqrt(majord * majord + minord * minord);

        Base::Vector3d focus1P = center + df * majdir;

        if (!major) {
            auto lmajor = new Part::GeomLineSegment();
            lmajor->setPoints(majorpositiveend, majornegativeend);

            igeo.push_back(lmajor);

            auto newConstr = new Sketcher::Constraint();
            newConstr->Type = Sketcher::InternalAlignment;
            newConstr->AlignmentType = Sketcher::HyperbolaMajor;
            newConstr->First = currentgeoid + incrgeo + 1;
            newConstr->Second = GeoId;

            icon.push_back(newConstr);
            incrgeo++;
        }
        if (!minor) {
            auto lminor = new Part::GeomLineSegment();
            lminor->setPoints(minorpositiveend, minornegativeend);

            igeo.push_back(lminor);

            auto newConstr = new Sketcher::Constraint();
            newConstr->Type = Sketcher::InternalAlignment;
            newConstr->AlignmentType = Sketcher::HyperbolaMinor;
            newConstr->First = currentgeoid + incrgeo + 1;
            newConstr->Second = GeoId;

            icon.push_back(newConstr);
            incrgeo++;
        }
        if (!focus) {
            auto pf1 = new Part::GeomPoint();
            pf1->setPoint(focus1P);

            igeo.push_back(pf1);

            auto newConstr = new Sketcher::Constraint();
            newConstr->Type = Sketcher::InternalAlignment;
            newConstr->AlignmentType = Sketcher::HyperbolaFocus;
            newConstr->First = currentgeoid + incrgeo + 1;
            newConstr->FirstPos = Sketcher::PointPos::start;
            newConstr->Second = GeoId;

            icon.push_back(newConstr);
            incrgeo++;
        }

        sketch->addGeometry(igeo, true);
        sketch->addConstraints(icon);

        for (auto& geo : igeo) {
            delete geo;
        }

        for (auto& con : icon) {
            delete con;
        }

        return incrgeo;  // number of added elements
    }
};

class SketchArcOfParabola: public SketchGeometryT<Part::GeomArcOfParabola>
{
public:
    Base::Vector3d getPoint(const Part::Geometry* geo, PointPos PosId) const override
    {
        auto aop = dynamic_cast<const GeomType*>(geo);
        switch (PosId) {
            case PointPos::start: {
                return aop->getStartPoint();
            }
            case PointPos::end: {
                return aop->getEndPoint();
            }
            case PointPos::mid: {
                return aop->getCenter();
            }
            default:
                break;
        }
        return Base::Vector3d();
    }
    int exposeInternalGeometry(int GeoId, const Part::Geometry* geo, SketchObject* sketch) const override
    {
        // First we search what has to be restored
        bool focus = false;
        bool focus_to_vertex = false;

        const std::vector<Sketcher::Constraint*>& vals = sketch->Constraints.getValues();

        for (auto const& constr : vals) {
            if (constr->Type != Sketcher::InternalAlignment || constr->Second != GeoId) {
                continue;
            }

            switch (constr->AlignmentType) {
                case Sketcher::ParabolaFocus:
                    focus = true;
                    break;
                case Sketcher::ParabolaFocalAxis:
                    focus_to_vertex = true;
                    break;
                default:
                    return -1;
            }
        }

        int currentgeoid = sketch->getHighestCurveIndex();
        int incrgeo = 0;

        const auto* aop = static_cast<const GeomType*>(geo);

        Base::Vector3d center {aop->getCenter()};
        Base::Vector3d focusp {aop->getFocus()};

        std::vector<Part::Geometry*> igeo;
        std::vector<Constraint*> icon;

        if (!focus) {
            auto pf1 = new Part::GeomPoint();
            pf1->setPoint(focusp);

            igeo.push_back(pf1);

            auto newConstr = new Sketcher::Constraint();
            newConstr->Type = Sketcher::InternalAlignment;
            newConstr->AlignmentType = Sketcher::ParabolaFocus;
            newConstr->First = currentgeoid + incrgeo + 1;
            newConstr->FirstPos = Sketcher::PointPos::start;
            newConstr->Second = GeoId;

            icon.push_back(newConstr);
            incrgeo++;
        }

        if (!focus_to_vertex) {
            auto paxis = new Part::GeomLineSegment();
            paxis->setPoints(center, focusp);

            igeo.push_back(paxis);

            auto newConstr = new Sketcher::Constraint();
            newConstr->Type = Sketcher::InternalAlignment;
            newConstr->AlignmentType = Sketcher::ParabolaFocalAxis;
            newConstr->First = currentgeoid + incrgeo + 1;
            newConstr->FirstPos = Sketcher::PointPos::none;
            newConstr->Second = GeoId;

            icon.push_back(newConstr);
            incrgeo++;
        }

        sketch->addGeometry(igeo, true);
        sketch->addConstraints(icon);

        for (auto& geo : igeo) {
            delete geo;
        }

        for (auto& con : icon) {
            delete con;
        }

        return incrgeo;  // number of added elements
    }
};

class SketchBSplineCurve: public SketchGeometryT<Part::GeomBSplineCurve>
{
public:
    Base::Vector3d getPoint(const Part::Geometry* geo, PointPos PosId) const override
    {
        auto bsp = dynamic_cast<const GeomType*>(geo);
        switch (PosId) {
            case PointPos::start: {
                return bsp->getStartPoint();
            }
            case PointPos::end: {
                return bsp->getEndPoint();
            }
            default:
                break;
        }
        return Base::Vector3d();
    }
    int exposeInternalGeometry(int GeoId, const Part::Geometry* geo, SketchObject* sketch) const override
    {
        const auto* bsp = static_cast<const GeomType*>(geo);
        // First we search what has to be restored
        std::vector<int> controlpointgeoids(bsp->countPoles(), GeoEnum::GeoUndef);

        std::vector<int> knotgeoids(bsp->countKnots(), GeoEnum::GeoUndef);

        bool isfirstweightconstrained = false;

        const std::vector<Sketcher::Constraint*>& vals = sketch->Constraints.getValues();

        // search for existing poles
        for (auto const& constr : vals) {
            if (constr->Type != Sketcher::InternalAlignment || constr->Second != GeoId) {
                continue;
            }

            switch (constr->AlignmentType) {
                case Sketcher::BSplineControlPoint:
                    controlpointgeoids[constr->InternalAlignmentIndex] = constr->First;
                    break;
                case Sketcher::BSplineKnotPoint:
                    knotgeoids[constr->InternalAlignmentIndex] = constr->First;
                    break;
                default:
                    return -1;
            }
        }

        if (controlpointgeoids[0] != GeoEnum::GeoUndef) {
            isfirstweightconstrained
                = std::any_of(vals.begin(), vals.end(), [&controlpointgeoids](const auto& constr) {
                      return (
                          constr->Type == Sketcher::Weight && constr->First == controlpointgeoids[0]
                      );
                  });
        }

        int currentgeoid = sketch->getHighestCurveIndex();
        int incrgeo = 0;

        std::vector<Part::Geometry*> igeo;
        std::vector<Constraint*> icon;

        std::vector<Base::Vector3d> poles = bsp->getPoles();
        std::vector<double> weights = bsp->getWeights();
        std::vector<double> knots = bsp->getKnots();

        double distance_p0_p1 = (poles[1] - poles[0]).Length();  // for visual purposes only

        for (size_t index = 0; index < controlpointgeoids.size(); ++index) {
            auto& cpGeoId = controlpointgeoids.at(index);
            if (cpGeoId != GeoEnum::GeoUndef) {
                continue;
            }

            // if controlpoint not existing
            auto pc = new Part::GeomCircle();
            pc->setCenter(poles[index]);
            pc->setRadius(distance_p0_p1 / 6);

            igeo.push_back(pc);
            incrgeo++;

            auto newConstr = new Sketcher::Constraint();
            newConstr->Type = Sketcher::InternalAlignment;
            newConstr->AlignmentType = Sketcher::BSplineControlPoint;
            newConstr->First = currentgeoid + incrgeo;
            newConstr->FirstPos = Sketcher::PointPos::mid;
            newConstr->Second = GeoId;
            newConstr->InternalAlignmentIndex = index;

            icon.push_back(newConstr);

            if (index == 0) {
                controlpointgeoids[0] = currentgeoid + incrgeo;
                if (weights[0] == 1.0) {
                    // if the first weight is 1.0 it's probably going to be non-rational
                    auto newConstr3 = new Sketcher::Constraint();
                    newConstr3->Type = Sketcher::Weight;
                    newConstr3->First = controlpointgeoids[0];
                    newConstr3->setValue(weights[0]);

                    icon.push_back(newConstr3);

                    isfirstweightconstrained = true;
                }

                continue;
            }

            if (isfirstweightconstrained && weights[0] == weights[index]) {
                // if pole-weight newly created AND first weight is radius-constrained,
                // AND these weights are equal, constrain them to be equal
                auto newConstr2 = new Sketcher::Constraint();
                newConstr2->Type = Sketcher::Equal;
                newConstr2->First = currentgeoid + incrgeo;
                newConstr2->FirstPos = Sketcher::PointPos::none;
                newConstr2->Second = controlpointgeoids[0];
                newConstr2->SecondPos = Sketcher::PointPos::none;

                icon.push_back(newConstr2);
            }
        }

        for (size_t index = 0; index < knotgeoids.size(); ++index) {
            auto& kGeoId = knotgeoids.at(index);
            if (kGeoId != GeoEnum::GeoUndef) {
                continue;
            }

            // if knot point not existing
            auto kp = new Part::GeomPoint();

            kp->setPoint(bsp->pointAtParameter(knots[index]));

            igeo.push_back(kp);
            incrgeo++;

            auto newConstr = new Sketcher::Constraint();
            newConstr->Type = Sketcher::InternalAlignment;
            newConstr->AlignmentType = Sketcher::BSplineKnotPoint;
            newConstr->First = currentgeoid + incrgeo;
            newConstr->FirstPos = Sketcher::PointPos::start;
            newConstr->Second = GeoId;
            newConstr->InternalAlignmentIndex = index;

            icon.push_back(newConstr);
        }

        Q_UNUSED(isfirstweightconstrained);

        sketch->addGeometry(igeo, true);
        sketch->addConstraints(icon);

        for (auto& geo : igeo) {
            delete geo;
        }

        for (auto& con : icon) {
            delete con;
        }

        return incrgeo;  // number of added elements
    }
};

}  // namespace Sketcher

std::list<SketchGeometryPtr> SketchGeometryType::sketchGeoms;  // NOLINT

void SketchGeometryType::init()
{
    static bool init = true;
    if (init) {
        init = false;
        addType(std::make_shared<SketchPoint>());
        addType(std::make_shared<SketchLineSegment>());
        addType(std::make_shared<SketchCircle>());
        addType(std::make_shared<SketchEllipse>());
        addType(std::make_shared<SketchArcOfCircle>());
        addType(std::make_shared<SketchArcOfEllipse>());
        addType(std::make_shared<SketchArcOfHyperbola>());
        addType(std::make_shared<SketchArcOfParabola>());
        addType(std::make_shared<SketchBSplineCurve>());
    }
}

void SketchGeometryType::addType(const SketchGeometryPtr& type)
{
    sketchGeoms.emplace_back(type);
}

Base::Vector3d SketchGeometryType::getPoint(const Part::Geometry* geo, PointPos PosId)
{
    for (const auto& it : sketchGeoms) {
        if (it->supports(geo)) {
            return it->getPoint(geo, PosId);
        }
    }

    return Base::Vector3d();
}

SketchGeometryType::SketchGeometryType(SketchObject* sketch)
    : sketch {sketch}
{}

int SketchGeometryType::exposeInternalGeometry(int GeoId) const
{
    const Part::Geometry* geo = sketch->getGeometry(GeoId);
    for (const auto& it : sketchGeoms) {
        if (it->supports(geo)) {
            return it->exposeInternalGeometry(GeoId, geo, sketch);
        }
    }

    return -1;
}
