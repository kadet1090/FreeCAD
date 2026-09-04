# SPDX-License-Identifier: LGPL-2.1-or-later

import math
import unittest

import Fitting


# Screen displacement of one unit of world length at the axonometric23 angle.
# World Z projects to an exact vertical, X to a 3:1 slope and Y to a 2:1 slope.
STEP_X = (math.sqrt(0.6), -math.sqrt(1.0 / 15.0))
STEP_Y = (math.sqrt(0.4), math.sqrt(0.1))
STEP_Z = (0.0, math.sqrt(5.0 / 6.0))


def cube_hexagon(side=1.0):
    """Silhouette of an axis-aligned cube seen at axonometric23: six edges, one
    per axis direction, walked in increasing screen angle so the result is a
    convex ring."""
    walk = [STEP_X, STEP_Y, STEP_Z,
            (-STEP_X[0], -STEP_X[1]), (-STEP_Y[0], -STEP_Y[1])]
    points = [(0.0, 0.0)]
    for step_x, step_y in walk:
        last_x, last_y = points[-1]
        points.append((last_x + step_x * side, last_y + step_y * side))
    return points


def circle_points(radius=1.0, count=128):
    return [(radius * math.cos(2.0 * math.pi * i / count),
             radius * math.sin(2.0 * math.pi * i / count))
            for i in range(count)]


class ModulePurityTest(unittest.TestCase):
    def test_fitting_imports_nothing_from_freecad(self):
        import ast
        import pathlib
        source = pathlib.Path(Fitting.__file__).read_text(encoding="utf-8")
        imported = set()
        for node in ast.walk(ast.parse(source)):
            if isinstance(node, ast.Import):
                imported.update(alias.name.split(".")[0] for alias in node.names)
            elif isinstance(node, ast.ImportFrom) and node.module:
                imported.add(node.module.split(".")[0])
        self.assertTrue(imported <= {"math", "enum", "dataclasses"},
                        "Fitting.py may only import math, enum, dataclasses; found: %s" % sorted(imported))


class ConvexHullTest(unittest.TestCase):
    def test_square_keeps_four_corners(self):
        hull = Fitting.convex_hull([(0, 0), (1, 0), (1, 1), (0, 1)])
        self.assertEqual(len(hull), 4)

    def test_interior_point_is_dropped(self):
        hull = Fitting.convex_hull([(0, 0), (2, 0), (2, 2), (0, 2), (1, 1)])
        self.assertEqual(len(hull), 4)
        self.assertNotIn((1.0, 1.0), hull)

    def test_collinear_points_on_an_edge_are_dropped(self):
        hull = Fitting.convex_hull([(0, 0), (1, 0), (2, 0), (2, 2), (0, 2)])
        self.assertEqual(len(hull), 4)

    def test_duplicate_points_collapse(self):
        hull = Fitting.convex_hull([(0, 0), (0, 0), (1, 0), (1, 1), (1, 1)])
        self.assertEqual(len(hull), 3)

    def test_all_collinear_input_returns_the_two_extremes(self):
        hull = Fitting.convex_hull([(0, 0), (1, 1), (2, 2), (3, 3)])
        self.assertEqual(hull, [(0.0, 0.0), (3.0, 3.0)])

    def test_empty_and_single_point_do_not_raise(self):
        self.assertEqual(Fitting.convex_hull([]), [])
        self.assertEqual(Fitting.convex_hull([(4, 5)]), [(4.0, 5.0)])


class PolygonMeasureTest(unittest.TestCase):
    def test_unit_square_area_and_perimeter(self):
        square = [(0, 0), (1, 0), (1, 1), (0, 1)]
        self.assertAlmostEqual(Fitting.polygon_area(square), 1.0)
        self.assertAlmostEqual(Fitting.polygon_perimeter(square), 4.0)

    def test_area_ignores_winding_direction(self):
        clockwise = [(0, 1), (1, 1), (1, 0), (0, 0)]
        self.assertAlmostEqual(Fitting.polygon_area(clockwise), 1.0)

    def test_degenerate_polygon_has_no_area(self):
        self.assertEqual(Fitting.polygon_area([(0, 0), (1, 0)]), 0.0)

    def test_segment_perimeter_counts_both_directions(self):
        # A degenerate convex body's boundary is walked out and back, which is
        # what makes Steiner's formula correct for it.
        self.assertAlmostEqual(Fitting.polygon_perimeter([(0, 0), (3, 0)]), 6.0)

    def test_cube_hexagon_matches_the_derived_constants(self):
        hexagon = Fitting.convex_hull(cube_hexagon())
        self.assertEqual(len(hexagon), 6)
        self.assertAlmostEqual(Fitting.polygon_area(hexagon), 1.692705, places=6)
        self.assertAlmostEqual(Fitting.polygon_perimeter(hexagon), 4.872949, places=6)

    def test_centroid_of_a_symmetric_shape_is_its_middle(self):
        square = [(0, 0), (2, 0), (2, 2), (0, 2)]
        centroid = Fitting.polygon_centroid(square)
        self.assertAlmostEqual(centroid[0], 1.0)
        self.assertAlmostEqual(centroid[1], 1.0)

    def test_centroid_of_an_asymmetric_hull_leaves_the_bbox_middle(self):
        # The notch pulls the optical centre away from the mechanical one; this
        # is the trade the spec accepts by centring on the centroid.
        pentagon = [(0, 0), (9, 0), (9, 3), (3, 9), (0, 9)]
        centroid = Fitting.polygon_centroid(pentagon)
        self.assertLess(centroid[0], 4.5)
        self.assertLess(centroid[1], 4.5)

    def test_centroid_of_a_zero_area_polygon_falls_back_to_the_vertex_mean(self):
        centroid = Fitting.polygon_centroid([(0, 0), (4, 0)])
        self.assertAlmostEqual(centroid[0], 2.0)
        self.assertAlmostEqual(centroid[1], 0.0)

    def test_bbox_size(self):
        self.assertEqual(Fitting.bbox_size([(1, 2), (4, 2), (4, 8)]), (3.0, 6.0))


class InkAreaTargetTest(unittest.TestCase):
    def ink_area_at(self, hull, scale):
        """Steiner's formula, restated independently of the implementation."""
        scaled = [(x * scale, y * scale) for x, y in hull]
        return (Fitting.polygon_area(scaled)
                + Fitting.polygon_perimeter(scaled) * Fitting._INK
                + math.pi * Fitting._INK ** 2)

    def test_square_hits_the_target_exactly(self):
        square = [(0, 0), (1, 0), (1, 1), (0, 1)]
        scale = Fitting.scale_for_ink_area(square)
        self.assertAlmostEqual(self.ink_area_at(square, scale), 320.0, places=9)

    def test_cube_hexagon_hits_the_target_and_the_derived_scale(self):
        hexagon = Fitting.convex_hull(cube_hexagon())
        scale = Fitting.scale_for_ink_area(hexagon)
        self.assertAlmostEqual(scale, 13.0316, places=3)
        self.assertAlmostEqual(self.ink_area_at(hexagon, scale), 320.0, places=9)

    def test_cube_inked_extent_matches_the_spec(self):
        hexagon = Fitting.convex_hull(cube_hexagon())
        scale = Fitting.scale_for_ink_area(hexagon)
        width, height = Fitting.bbox_size(hexagon)
        self.assertAlmostEqual(width * scale + Fitting.SILHOUETTE_STROKE, 19.34, places=2)
        self.assertAlmostEqual(height * scale + Fitting.SILHOUETTE_STROKE, 20.38, places=2)

    def test_a_degenerate_segment_still_solves(self):
        # Area is zero, so the quadratic collapses to a linear solve rather
        # than dividing by zero.
        scale = Fitting.scale_for_ink_area([(0, 0), (1, 0)])
        self.assertAlmostEqual(self.ink_area_at([(0, 0), (1, 0)], scale), 320.0, places=9)

    def test_a_single_point_cannot_be_sized(self):
        with self.assertRaises(ValueError):
            Fitting.scale_for_ink_area([(0, 0)])


class ClassifierTest(unittest.TestCase):
    def test_circle_is_a_circle(self):
        hull = Fitting.convex_hull(circle_points())
        self.assertIs(Fitting.classify(hull), Fitting.Keyline.CIRCLE)

    def test_square_is_a_square(self):
        self.assertIs(Fitting.classify([(0, 0), (9, 0), (9, 9), (0, 9)]),
                      Fitting.Keyline.SQUARE)

    def test_tall_rectangle_is_vertical(self):
        self.assertIs(Fitting.classify([(0, 0), (4, 0), (4, 20), (0, 20)]),
                      Fitting.Keyline.VERTICAL)

    def test_wide_rectangle_is_horizontal(self):
        self.assertIs(Fitting.classify([(0, 0), (20, 0), (20, 4), (0, 4)]),
                      Fitting.Keyline.HORIZONTAL)

    def test_cube_hexagon_is_area_not_circle(self):
        # Circularity 0.896 and bbox fill 0.809 — below both thresholds. This is
        # the common case: every icon here is a 3-D projection.
        hull = Fitting.convex_hull(cube_hexagon())
        self.assertIs(Fitting.classify(hull), Fitting.Keyline.AREA)

    def test_regular_hexagon_is_area_not_circle(self):
        # Circularity 0.9069. An ellipse-area ratio would score it 0.955 and
        # misfire; the isoperimetric quotient separates it cleanly.
        hexagon = [(math.cos(math.radians(a)), math.sin(math.radians(a)))
                   for a in range(0, 360, 60)]
        self.assertIs(Fitting.classify(hexagon), Fitting.Keyline.AREA)

    def test_classify_never_returns_auto(self):
        for shape in ([(0, 0), (9, 0), (9, 9), (0, 9)],
                      circle_points(),
                      Fitting.convex_hull(cube_hexagon()),
                      [(0, 0), (1, 0)]):
            self.assertIsNot(Fitting.classify(shape), Fitting.Keyline.AUTO)


class KeylineScaleTest(unittest.TestCase):
    def inked_bbox(self, hull, scale):
        width, height = Fitting.bbox_size(hull)
        return (width * scale + Fitting.SILHOUETTE_STROKE,
                height * scale + Fitting.SILHOUETTE_STROKE)

    def test_square_keyline_inks_to_eighteen(self):
        square = [(0, 0), (3, 0), (3, 3), (0, 3)]
        scale = Fitting.target_scale(square, Fitting.Keyline.SQUARE)
        self.assertEqual(self.inked_bbox(square, scale), (18.0, 18.0))

    def test_circle_keyline_inks_to_twenty(self):
        hull = Fitting.convex_hull(circle_points())
        scale = Fitting.target_scale(hull, Fitting.Keyline.CIRCLE)
        width, height = self.inked_bbox(hull, scale)
        self.assertAlmostEqual(width, 20.0, places=9)
        self.assertAlmostEqual(height, 20.0, places=9)

    def test_vertical_keyline_inks_to_twenty_tall(self):
        tall = [(0, 0), (4, 0), (4, 20), (0, 20)]
        scale = Fitting.target_scale(tall, Fitting.Keyline.VERTICAL)
        self.assertAlmostEqual(self.inked_bbox(tall, scale)[1], 20.0, places=9)

    def test_horizontal_keyline_inks_to_twenty_wide(self):
        wide = [(0, 0), (20, 0), (20, 4), (0, 4)]
        scale = Fitting.target_scale(wide, Fitting.Keyline.HORIZONTAL)
        self.assertAlmostEqual(self.inked_bbox(wide, scale)[0], 20.0, places=9)

    def test_keyline_boxes_are_inset_by_half_the_stroke(self):
        # Keylines bound the artwork, so the centreline target is the inked
        # figure minus one stroke width.
        self.assertEqual(Fitting._KEYLINE_BOX[Fitting.Keyline.SQUARE], (17.0, 17.0))
        self.assertEqual(Fitting._KEYLINE_BOX[Fitting.Keyline.VERTICAL], (15.0, 19.0))


class CanvasConstantTest(unittest.TestCase):
    def test_live_area_lands_on_the_half_pixel_lattice(self):
        self.assertEqual((Fitting.LIVE_MIN, Fitting.LIVE_MAX), (2.5, 21.5))
        self.assertEqual((Fitting.CANVAS_MIN, Fitting.CANVAS_MAX), (0.5, 23.5))
        self.assertEqual(Fitting.CENTRE, 12.0)


def centred_square(half):
    return [(12 - half, 12 - half), (12 + half, 12 - half),
            (12 + half, 12 + half), (12 - half, 12 + half)]


def centred_circle(radius, count=128):
    return [(12 + radius * math.cos(2.0 * math.pi * i / count),
             12 + radius * math.sin(2.0 * math.pi * i / count))
            for i in range(count)]


class TrimRuleTest(unittest.TestCase):
    def test_a_shape_inside_the_live_area_has_no_outside_run(self):
        self.assertEqual(Fitting.longest_outside_run(centred_square(8.0)), 0.0)

    def test_a_shape_exactly_on_the_live_boundary_has_no_outside_run(self):
        self.assertAlmostEqual(Fitting.longest_outside_run(centred_square(9.5)), 0.0)

    def test_cube_tips_pass(self):
        # The reference "allow" case: the two 120-degree tips cross 0.19px deep,
        # roughly 1.03px of boundary in total. Well inside the 3px budget.
        hexagon = Fitting.convex_hull(cube_hexagon())
        scale = Fitting.scale_for_ink_area(hexagon)
        centroid = Fitting.polygon_centroid(hexagon)
        placed = [(12 + (x - centroid[0]) * scale, 12 - (y - centroid[1]) * scale)
                  for x, y in hexagon]
        run = Fitting.longest_outside_run(placed)
        self.assertGreater(run, 0.0)
        self.assertLess(run, Fitting.MAX_TRIM_CHORD)
        self.assertTrue(Fitting.within_trim(placed))

    def test_a_flat_edge_in_the_trim_band_fails(self):
        # The reference "forbid" case: a wide plate whose whole top edge sits in
        # the band. Not a corner.
        plate = [(1.2, 1.2), (22.8, 1.2), (22.8, 8.4), (18, 15), (6, 15), (1.2, 8.4)]
        self.assertGreater(Fitting.longest_outside_run(plate), Fitting.MAX_TRIM_CHORD)
        self.assertFalse(Fitting.within_trim(plate))

    def test_an_oversized_circle_fails_even_though_every_chord_is_short(self):
        # The defect a per-edge rule has: at radius 10.17 every one of the 128
        # chords is about 0.5px, so no single edge exceeds 3px — but roughly
        # 7.4px of contiguous arc sits in the trim band.
        oversized = centred_circle(10.17)
        longest_chord = max(
            math.hypot(end[0] - start[0], end[1] - start[1])
            for start, end in zip(oversized, oversized[1:] + oversized[:1]))
        self.assertLess(longest_chord, 1.0)
        self.assertGreater(Fitting.longest_outside_run(oversized),
                           Fitting.MAX_TRIM_CHORD)
        self.assertFalse(Fitting.within_trim(oversized))

    def test_an_outside_run_wraps_around_the_start_vertex(self):
        # Separates a per-fragment maximum (1.5) from an accumulating walk (4.0).
        straddling = [(12.0, 1.0), (13.0, 1.0), (13.0, 23.0), (12.0, 23.0)]
        # The wrapped run is 4.0; the longest single fragment is only 1.5, so a
        # per-fragment rule cannot reach this bound.
        self.assertGreater(Fitting.longest_outside_run(straddling), 3.5)

    def test_a_wrapping_run_is_only_found_by_the_doubled_walk(self):
        # Vertex 0 sits in the middle of one long excursion past x=2.5, so the
        # run spanning the start is uniquely the longest: a single pass sees
        # two halves of 5.5, only the doubled walk joins them into 11.0.
        straddling = [(1.0, 12.0), (1.0, 16.0), (10.0, 16.0), (10.0, 8.0), (1.0, 8.0)]
        self.assertGreater(Fitting.longest_outside_run(straddling), 10.0)

    def test_a_shape_entirely_outside_the_live_area_fails(self):
        far = [(0.6, 0.6), (2.0, 0.6), (2.0, 2.0), (0.6, 2.0)]
        self.assertGreater(Fitting.longest_outside_run(far), Fitting.MAX_TRIM_CHORD)

    def test_leaving_the_canvas_fails_regardless_of_run_length(self):
        escaping = [(11.9, 11.9), (12.1, 11.9), (12.1, 12.1), (-0.2, 12.0)]
        self.assertFalse(Fitting.within_trim(escaping))

    def test_segment_span_reports_a_miss(self):
        self.assertIsNone(Fitting._segment_inside_span((0, 0), (1, 0), 2.5, 21.5))

    def test_segment_span_reports_a_full_crossing(self):
        span = Fitting._segment_inside_span((0, 12), (24, 12), 2.5, 21.5)
        self.assertIsNotNone(span)
        self.assertAlmostEqual(span[0], 2.5 / 24.0)
        self.assertAlmostEqual(span[1], 21.5 / 24.0)


class CornerCandidateTest(unittest.TestCase):
    def test_a_square_offers_four_corners(self):
        corners = Fitting.corner_candidates([(0, 0), (10, 0), (10, 10), (0, 10)])
        self.assertEqual(len(corners), 4)

    def test_a_discretised_circle_offers_none(self):
        # Every turn is 2.8 degrees, below the threshold. These are polyline
        # samples, not corners, and snapping them means nothing.
        self.assertEqual(Fitting.corner_candidates(circle_points()), [])

    def test_the_cube_hexagon_offers_six_corners(self):
        hexagon = Fitting.convex_hull(cube_hexagon())
        self.assertEqual(len(Fitting.corner_candidates(hexagon)), 6)

    def test_weight_is_the_shorter_adjacent_edge(self):
        corners = Fitting.corner_candidates([(0, 0), (10, 0), (10, 2), (0, 2)])
        by_point = dict(corners)
        self.assertAlmostEqual(by_point[(10, 0)], 2.0)

    def test_repeated_points_do_not_raise(self):
        corners = Fitting.corner_candidates([(0, 0), (0, 0), (5, 0), (5, 5)])
        self.assertTrue(all(weight > 0.0 for _, weight in corners))


class BestOffsetTest(unittest.TestCase):
    def test_values_already_on_the_lattice_need_no_offset(self):
        offset, cost = Fitting._best_offset([2.5, 10.5, 21.5], [1.0, 1.0, 1.0])
        self.assertAlmostEqual(offset, 0.0, places=9)
        self.assertAlmostEqual(cost, 0.0, places=9)

    def test_a_uniform_shift_is_undone_exactly(self):
        offset, cost = Fitting._best_offset([2.7, 10.7, 21.7], [1.0, 1.0, 1.0])
        self.assertAlmostEqual(offset, -0.2, places=9)
        self.assertAlmostEqual(cost, 0.0, places=9)

    def test_the_offset_stays_within_half_a_pixel(self):
        for start in (0.0, 0.1, 0.37, 0.5, 0.9, 12.34):
            offset, _ = Fitting._best_offset([start, start + 3.0], [1.0, 1.0])
            self.assertGreaterEqual(offset, -0.5)
            self.assertLess(offset, 0.5)

    def test_weight_decides_which_value_wins(self):
        # Two values a third of a pixel apart cannot both land on the lattice;
        # the heavier one does.
        offset, _ = Fitting._best_offset([2.5, 2.8], [100.0, 1.0])
        self.assertAlmostEqual(offset, 0.0, places=2)

    def test_the_solve_beats_a_dense_sample_of_offsets(self):
        values = [1.13, 4.61, 9.02, 15.77, 20.4]
        weights = [1.0, 2.0, 0.5, 3.0, 1.5]
        offset, cost = Fitting._best_offset(values, weights)

        def cost_at(candidate):
            total = 0.0
            for value, weight in zip(values, weights):
                shifted = value + candidate - Fitting.LATTICE_OFFSET
                total += weight * (shifted - round(shifted)) ** 2
            return total

        best_sampled = min(cost_at(i / 20000.0) for i in range(20000))
        self.assertLessEqual(cost, best_sampled + 1e-9)
        self.assertAlmostEqual(cost, cost_at(offset), places=9)

    def test_empty_input_is_a_no_op(self):
        self.assertEqual(Fitting._best_offset([], []), (0.0, 0.0))


def through_panel_transform(points, frame):
    """Reproduce IconStudioPanel._xfrm exactly, so the tests check the framing
    the renderer will actually apply rather than an internal intermediate."""
    return [(12.0 + (x - frame.center[0]) * frame.scale,
             12.0 - (y - frame.center[1]) * frame.scale) for x, y in points]


class FitTest(unittest.TestCase):
    def test_cube_is_area_classed_snapped_and_inside_the_trim(self):
        report = Fitting.fit(cube_hexagon())
        self.assertIs(report.keyline, Fitting.Keyline.AREA)
        self.assertFalse(report.trim_clamped)
        placed = through_panel_transform(
            Fitting.convex_hull(cube_hexagon()), report.frame)
        self.assertTrue(Fitting.within_trim(placed))
        self.assertLess(Fitting.longest_outside_run(placed), Fitting.MAX_TRIM_CHORD)

    def test_cube_area_stays_inside_the_scale_band(self):
        # The band bounds the scale, so the achieved area moves by about twice
        # that. 309 rather than 320 is the search preferring the snap.
        report = Fitting.fit(cube_hexagon())
        self.assertGreater(report.ink_area, 320.0 / (1.0 + Fitting.SCALE_BAND) ** 2 - 1.0)
        self.assertLess(report.ink_area, 320.0 * (1.0 + Fitting.SCALE_BAND) ** 2 + 1.0)

    def test_cube_vertical_edges_land_on_the_half_pixel_lattice(self):
        report = Fitting.fit(cube_hexagon())
        placed = through_panel_transform(
            Fitting.convex_hull(cube_hexagon()), report.frame)
        left = min(point[0] for point in placed)
        right = max(point[0] for point in placed)
        for edge in (left, right):
            self.assertAlmostEqual(edge - math.floor(edge), 0.5, places=2)

    def test_a_square_lands_exactly_on_its_keyline(self):
        report = Fitting.fit([(0, 0), (10, 0), (10, 10), (0, 10)])
        self.assertIs(report.keyline, Fitting.Keyline.SQUARE)
        self.assertAlmostEqual(report.ink_size[0], 18.0, places=6)
        self.assertAlmostEqual(report.ink_size[1], 18.0, places=6)

    def test_a_circle_lands_exactly_on_its_keyline(self):
        # No corners, so no snapping: the keyline is hit exactly rather than
        # nudged off it to align chords that do not read as edges.
        report = Fitting.fit(circle_points())
        self.assertIs(report.keyline, Fitting.Keyline.CIRCLE)
        self.assertAlmostEqual(report.ink_size[0], 20.0, places=6)
        self.assertAlmostEqual(report.snap_residual, 0.0, places=9)

    def test_a_keyline_is_never_exceeded_by_the_snap_band(self):
        # The band is one-sided for keyline classes: a keyline is a maximum.
        for points in (circle_points(),
                       [(0, 0), (10, 0), (10, 10), (0, 10)],
                       [(0, 0), (4, 0), (4, 20), (0, 20)],
                       [(0, 0), (20, 0), (20, 4), (0, 4)]):
            report = Fitting.fit(points)
            self.assertLessEqual(max(report.ink_size), 20.0 + 1e-6)

    def test_an_explicit_keyline_overrides_the_classifier(self):
        report = Fitting.fit(cube_hexagon(), keyline=Fitting.Keyline.SQUARE)
        self.assertIs(report.keyline, Fitting.Keyline.SQUARE)

    def test_framing_does_not_depend_on_model_units(self):
        small = Fitting.fit(cube_hexagon(1.0))
        large = Fitting.fit(cube_hexagon(10.0))
        self.assertAlmostEqual(small.ink_size[0], large.ink_size[0], places=6)
        self.assertAlmostEqual(small.ink_size[1], large.ink_size[1], places=6)

    def test_a_shape_the_band_cannot_fit_is_clamped_to_the_trim_rule(self):
        report = Fitting.fit([(0, 0), (20, 20), (20.3, 19.7)])
        self.assertTrue(report.trim_clamped)
        placed = through_panel_transform(
            Fitting.convex_hull([(0, 0), (20, 20), (20.3, 19.7)]), report.frame)
        self.assertTrue(Fitting.within_trim(placed))

    def test_an_asymmetric_hull_pays_area_for_optical_centring(self):
        # Centring on the centroid rather than the bbox pushes one side out
        # further, so the trim rule binds before the area target does. This is
        # the accepted cost of decision 5, not a defect.
        report = Fitting.fit([(0, 0), (9, 0), (9, 3), (3, 9), (0, 9)])
        self.assertTrue(report.trim_clamped)
        self.assertLess(report.ink_area, 320.0)

    def test_degenerate_inputs_do_not_raise(self):
        for points in ([], [(3, 3)], [(0, 0), (10, 0)]):
            report = Fitting.fit(points)
            self.assertIsInstance(report, Fitting.FitReport)
            self.assertGreater(report.frame.scale, 0.0)

    def test_a_single_off_origin_point_centres_on_the_canvas(self):
        # The centre must be the point itself, not the origin, or an
        # off-origin single vertex renders off-centre.
        report = Fitting.fit([(50.0, -30.0)])
        placed = through_panel_transform([(50.0, -30.0)], report.frame)
        self.assertAlmostEqual(placed[0][0], 12.0, places=9)
        self.assertAlmostEqual(placed[0][1], 12.0, places=9)

    def test_the_reported_frame_reproduces_the_reported_ink_size(self):
        points = cube_hexagon()
        report = Fitting.fit(points)
        placed = through_panel_transform(Fitting.convex_hull(points), report.frame)
        width, height = Fitting.bbox_size(placed)
        self.assertAlmostEqual(width + Fitting.SILHOUETTE_STROKE,
                               report.ink_size[0], places=9)
        self.assertAlmostEqual(height + Fitting.SILHOUETTE_STROKE,
                               report.ink_size[1], places=9)

    def test_an_explicit_silhouette_changes_the_snap(self):
        # The hull answers "how big"; the silhouette answers "what to align".
        # A notch is concave, so the hull drops it and the two disagree — if the
        # parameter were ignored, both calls would return the identical frame.
        notched = [(0, 0), (10, 0), (10, 10), (6, 10),
                   (6, 7), (4, 7), (4, 10), (0, 10)]
        from_hull = Fitting.fit(notched)
        from_outline = Fitting.fit(notched, silhouette=notched)
        self.assertAlmostEqual(from_hull.snap_residual, 0.0, places=9)
        self.assertGreater(from_outline.snap_residual, 0.05)
        self.assertNotEqual(from_hull.frame, from_outline.frame)


if __name__ == "__main__":
    unittest.main()
