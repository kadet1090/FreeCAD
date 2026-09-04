# SPDX-License-Identifier: LGPL-2.1-or-later
"""Framing geometry for icon export.

Given the projected outline of a model in camera-frame millimetres, decide the
scale and centre that put it on the 24-unit icon canvas: the same optical area
as its neighbours, corners allowed into the trim band but never straight edges,
and as many straight runs as possible landing on the pixel grid.

Imports nothing from FreeCAD or Qt on purpose, so the whole algorithm runs
under plain `python -m unittest` with no binary and no GUI.

Input points are camera-frame mm with Y up. Output coordinates are SVG units on
the 24x24 canvas with Y down; the flip lives in `_place`, matching the panel's
own transform.
"""

import dataclasses
import enum
import math


CANVAS = 24.0
TRIM = 2.0                 # untouchable border, per the keyline convention
SILHOUETTE_STROKE = 1.0    # ink reaches half this beyond the centreline
TARGET_INK_AREA = 320.0    # 18x18 square = pi*10^2 circle = 16x20 rectangle
MAX_TRIM_CHORD = 3.0       # longest straight edge or contiguous arc in trim band
CIRCULARITY_MIN = 0.97
BBOX_FILL_MIN = 0.90
SQUARE_ASPECT_MIN = 0.9
SQUARE_ASPECT_MAX = 1.1
CORNER_TURN_DEG = 5.0      # below this a vertex is polyline noise, not a corner
LATTICE_PITCH = 1.0        # a 1px stroke centreline is crisp on half-integers
LATTICE_OFFSET = 0.5
SCALE_BAND = 0.03          # how far the snap search may bend the target scale
MIN_SCALE_STEPS = 64
SCALE_RESOLUTION = 0.02    # canvas units of travel between scale samples

_INK = SILHOUETTE_STROKE / 2.0
CENTRE = CANVAS / 2.0
LIVE_MIN = TRIM + _INK
LIVE_MAX = CANVAS - TRIM - _INK
CANVAS_MIN = _INK
CANVAS_MAX = CANVAS - _INK


class Keyline(enum.Enum):
    AUTO = "auto"
    SQUARE = "square"
    CIRCLE = "circle"
    VERTICAL = "vertical"
    HORIZONTAL = "horizontal"
    AREA = "area"


@dataclasses.dataclass(frozen=True)
class Frame:
    """Framing for the 24-unit canvas. `scale` is SVG units per camera mm and
    `center` the camera-frame point that lands at the canvas centre — the pair
    the panel already keeps as `_zoom` and `_pan`."""
    scale: float
    center: tuple


@dataclasses.dataclass(frozen=True)
class FitReport:
    """`trim_clamped` means no candidate in the scale band was feasible, so the
    scale fell back to the largest the trim rule allows. The main search can
    still return a scale the trim rule bounded and report False.
    `snap_residual` is the RMS distance of the snapped corners from the pixel
    lattice, in canvas units."""
    keyline: Keyline
    frame: Frame
    ink_size: tuple
    ink_area: float
    snap_residual: float
    trim_clamped: bool


# Keyline extents measured on the centreline hull: the inked figure inset by
# half the silhouette stroke, because a keyline bounds the artwork.
_KEYLINE_BOX = {
    Keyline.SQUARE: (18.0 - SILHOUETTE_STROKE, 18.0 - SILHOUETTE_STROKE),
    Keyline.CIRCLE: (20.0 - SILHOUETTE_STROKE, 20.0 - SILHOUETTE_STROKE),
    Keyline.VERTICAL: (16.0 - SILHOUETTE_STROKE, 20.0 - SILHOUETTE_STROKE),
    Keyline.HORIZONTAL: (20.0 - SILHOUETTE_STROKE, 16.0 - SILHOUETTE_STROKE),
}


def _edges(poly):
    """Consecutive vertex pairs, including the one that closes the ring."""
    return zip(poly, poly[1:] + poly[:1])


def convex_hull(points):
    """Counter-clockwise hull by monotone chain, without a repeated closing
    point. Collinear points are dropped, so a straight run contributes one edge
    rather than several — which is what the snap search wants to see. Degenerate
    input comes back as the deduplicated points themselves."""
    unique = sorted({(round(x, 9), round(y, 9)) for x, y in points})
    if len(unique) < 3:
        return unique

    def cross(origin, first, second):
        return ((first[0] - origin[0]) * (second[1] - origin[1])
                - (first[1] - origin[1]) * (second[0] - origin[0]))

    lower = []
    for point in unique:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], point) <= 0:
            lower.pop()
        lower.append(point)
    upper = []
    for point in reversed(unique):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], point) <= 0:
            upper.pop()
        upper.append(point)
    return lower[:-1] + upper[:-1]


def polygon_area(poly):
    """Unsigned shoelace area; zero for anything without three vertices."""
    if len(poly) < 3:
        return 0.0
    return abs(sum(x0 * y1 - x1 * y0
                   for (x0, y0), (x1, y1) in _edges(poly))) / 2.0


def polygon_perimeter(poly):
    if len(poly) < 2:
        return 0.0
    return sum(math.hypot(x1 - x0, y1 - y0)
               for (x0, y0), (x1, y1) in _edges(poly))


def polygon_centroid(poly):
    """Area centroid — the optical centre. Falls back to the vertex mean when
    the polygon has no area, which is what a degenerate hull needs."""
    if not poly:
        return (0.0, 0.0)
    twice_area = 0.0
    accumulated_x = accumulated_y = 0.0
    for (x0, y0), (x1, y1) in _edges(poly):
        cross = x0 * y1 - x1 * y0
        twice_area += cross
        accumulated_x += (x0 + x1) * cross
        accumulated_y += (y0 + y1) * cross
    if abs(twice_area) < 1e-12:
        count = len(poly)
        return (sum(point[0] for point in poly) / count,
                sum(point[1] for point in poly) / count)
    return (accumulated_x / (3.0 * twice_area),
            accumulated_y / (3.0 * twice_area))


def bbox_size(poly):
    xs = [point[0] for point in poly]
    ys = [point[1] for point in poly]
    return (max(xs) - min(xs), max(ys) - min(ys))


def scale_for_ink_area(hull, target=TARGET_INK_AREA):
    """Scale at which the hull, dilated by half the silhouette stroke, encloses
    `target` area. Steiner's formula is exact for convex sets, so this is a
    quadratic root rather than a search. A hull with no area degenerates to the
    linear case, which is still correct for a dilated segment."""
    area = polygon_area(hull)
    perimeter = polygon_perimeter(hull)
    constant = math.pi * _INK * _INK - target
    linear = perimeter * _INK
    if area > 1e-12:
        discriminant = linear * linear - 4.0 * area * constant
        return (-linear + math.sqrt(discriminant)) / (2.0 * area)
    if linear > 1e-12:
        return -constant / linear
    raise ValueError("hull has neither area nor extent")


def classify(hull):
    """Pick the keyline a hull belongs to; never returns AUTO.

    Circularity is the isoperimetric quotient rather than a ratio against the
    bounding ellipse: a regular hexagon scores 0.955 on the latter, close enough
    to a circle to misfire, but only 0.907 here."""
    width, height = bbox_size(hull)
    if width <= 1e-12 or height <= 1e-12:
        return Keyline.AREA
    aspect = width / height
    area = polygon_area(hull)
    perimeter = polygon_perimeter(hull)
    circularity = (4.0 * math.pi * area / (perimeter * perimeter)
                   if perimeter > 1e-12 else 0.0)
    squarish = SQUARE_ASPECT_MIN <= aspect <= SQUARE_ASPECT_MAX

    if circularity >= CIRCULARITY_MIN and squarish:
        return Keyline.CIRCLE
    if area / (width * height) >= BBOX_FILL_MIN:
        if squarish:
            return Keyline.SQUARE
        return Keyline.VERTICAL if aspect < SQUARE_ASPECT_MIN else Keyline.HORIZONTAL
    return Keyline.AREA


def target_scale(hull, keyline):
    """Scale that puts the hull on its keyline, or on the area target."""
    if keyline is Keyline.AREA:
        return scale_for_ink_area(hull)
    box_width, box_height = _KEYLINE_BOX[keyline]
    width, height = bbox_size(hull)
    options = []
    if width > 1e-12:
        options.append(box_width / width)
    if height > 1e-12:
        options.append(box_height / height)
    if not options:
        raise ValueError("hull has no extent")
    return min(options)


def _segment_inside_span(start, end, low, high):
    """Liang-Barsky clip: the [first, last] parameter range of the segment that
    lies inside the axis-aligned box, or None when it misses entirely."""
    delta_x = end[0] - start[0]
    delta_y = end[1] - start[1]
    first, last = 0.0, 1.0
    for gradient, distance in ((-delta_x, start[0] - low),
                               (delta_x, high - start[0]),
                               (-delta_y, start[1] - low),
                               (delta_y, high - start[1])):
        if abs(gradient) < 1e-12:
            if distance < 0.0:
                return None
            continue
        crossing = distance / gradient
        if gradient < 0.0:
            if crossing > last:
                return None
            first = max(first, crossing)
        else:
            if crossing < first:
                return None
            last = min(last, crossing)
    return (first, last)


def _boundary_fragments(poly, low, high):
    """The closed boundary as (is_outside, length) pieces in walk order."""
    fragments = []
    for start, end in _edges(poly):
        length = math.hypot(end[0] - start[0], end[1] - start[1])
        if length <= 1e-12:
            continue
        span = _segment_inside_span(start, end, low, high)
        if span is None:
            fragments.append((True, length))
            continue
        first, last = span
        if first > 0.0:
            fragments.append((True, first * length))
        if last > first:
            fragments.append((False, (last - first) * length))
        if last < 1.0:
            fragments.append((True, (1.0 - last) * length))
    return fragments


def longest_outside_run(poly, low=LIVE_MIN, high=LIVE_MAX):
    """Longest contiguous run of boundary lying outside the live area.

    Measured along the boundary rather than per edge, because discretisation
    would otherwise defeat the rule: a circle sitting entirely in the trim band
    is hundreds of half-pixel chords, none of which is individually too long.
    The doubled walk lets a run straddle the start vertex."""
    fragments = _boundary_fragments(poly, low, high)
    if not fragments:
        return 0.0
    if all(outside for outside, _ in fragments):
        return sum(length for _, length in fragments)
    worst = run = 0.0
    for outside, length in fragments + fragments:
        run = run + length if outside else 0.0
        worst = max(worst, run)
    return worst


def within_trim(poly):
    """True when corners may cross into the trim band but straight runs do not,
    and nothing leaves the canvas."""
    for x, y in poly:
        if not (CANVAS_MIN <= x <= CANVAS_MAX and CANVAS_MIN <= y <= CANVAS_MAX):
            return False
    return longest_outside_run(poly) <= MAX_TRIM_CHORD


def corner_candidates(outline, min_turn_deg=CORNER_TURN_DEG):
    """Corners of a closed outline worth landing on the pixel grid, each paired
    with a weight.

    A discretised curve is dozens of shallow turns that read as a smooth arc,
    not as edges; the turn threshold drops them, and weighting by the shorter
    adjacent edge keeps a corner between two long straight runs on top. An empty
    result means the shape has nothing straight to align."""
    count = len(outline)
    if count < 3:
        return [(point, 1.0) for point in outline]
    threshold = math.radians(min_turn_deg)
    found = []
    for index in range(count):
        previous = outline[index - 1]
        here = outline[index]
        following = outline[(index + 1) % count]
        incoming = (here[0] - previous[0], here[1] - previous[1])
        outgoing = (following[0] - here[0], following[1] - here[1])
        incoming_length = math.hypot(*incoming)
        outgoing_length = math.hypot(*outgoing)
        if incoming_length < 1e-9 or outgoing_length < 1e-9:
            continue
        turn = abs(math.atan2(
            incoming[0] * outgoing[1] - incoming[1] * outgoing[0],
            incoming[0] * outgoing[0] + incoming[1] * outgoing[1]))
        if turn < threshold:
            continue
        found.append((here, min(incoming_length, outgoing_length)))
    return found


def _best_offset(values, weights):
    """Offset minimising the weighted squared distance of `values` from the
    half-integer lattice, returned with its cost.

    Solved exactly rather than sampled. The cost is piecewise quadratic in the
    offset, and a value's nearest lattice point only changes where that value
    sits exactly between two of them; on each such interval the minimiser is a
    weighted mean, clamped to the interval."""
    if not values:
        return 0.0, 0.0
    shifted = [value - LATTICE_OFFSET for value in values]
    breakpoints = sorted((0.5 - value) % LATTICE_PITCH for value in shifted)
    total_weight = sum(weights)

    best_offset, best_cost = 0.0, float("inf")
    for index, low in enumerate(breakpoints):
        high = (breakpoints[index + 1] if index + 1 < len(breakpoints)
                else breakpoints[0] + LATTICE_PITCH)
        if high - low < 1e-12:
            continue
        nearest = [round(value + (low + high) / 2.0) for value in shifted]
        offset = sum(weight * (target - value)
                     for weight, target, value in zip(weights, nearest, shifted))
        offset = min(max(offset / total_weight, low), high)
        cost = sum(weight * (value + offset - target) ** 2
                   for weight, value, target in zip(weights, shifted, nearest))
        if cost < best_cost:
            best_offset, best_cost = offset, cost
    return ((best_offset + 0.5) % LATTICE_PITCH) - 0.5, best_cost


def _place(point, centre, scale, offset=(0.0, 0.0)):
    """Camera mm to canvas units, including the Y flip the panel applies."""
    return (CENTRE + (point[0] - centre[0]) * scale + offset[0],
            CENTRE - (point[1] - centre[1]) * scale + offset[1])


def _largest_feasible_scale(hull, centre, upper):
    """Largest centroid-centred scale respecting the trim rule."""
    if within_trim([_place(point, centre, upper) for point in hull]):
        return upper
    # low = 0.0 is always trim-feasible without checking: as scale approaches
    # zero every point collapses onto the canvas centre, which is inside the
    # live area.
    low, high = 0.0, upper
    for _ in range(48):
        middle = (low + high) / 2.0
        if within_trim([_place(point, centre, middle) for point in hull]):
            low = middle
        else:
            high = middle
    return low


def _scale_steps(hull, centre, low, high):
    """Scale samples fine enough that the outermost hull point moves no further
    than SCALE_RESOLUTION between neighbours."""
    if high <= low:
        return [high]
    radius = max(math.hypot(point[0] - centre[0], point[1] - centre[1])
                 for point in hull)
    count = max(MIN_SCALE_STEPS,
                int(math.ceil(radius * (high - low) / SCALE_RESOLUTION)))
    return [low + (high - low) * step / (count - 1) for step in range(count)]


def _report(hull, centre, scale, offset, keyline, residual, clamped):
    placed = [_place(point, centre, scale, offset) for point in hull]
    width, height = bbox_size(placed)
    ink_area = (polygon_area(placed) + polygon_perimeter(placed) * _INK
                + math.pi * _INK * _INK)
    frame = Frame(scale=scale,
                  center=(centre[0] - offset[0] / scale,
                          centre[1] + offset[1] / scale))
    return FitReport(keyline=keyline, frame=frame,
                     ink_size=(width + SILHOUETTE_STROKE,
                               height + SILHOUETTE_STROKE),
                     ink_area=ink_area, snap_residual=residual,
                     trim_clamped=clamped)


def fit(points, keyline=Keyline.AUTO, silhouette=None):
    """Frame a projected model on the icon canvas.

    `points` is every visible point in camera-frame mm. `silhouette` is meant
    to be an ordered outline whose corners are worth snapping — concave
    corners included, which the hull alone cannot offer. A caller that passes
    an unordered point set or a hull-derived outline (as `IconStudio.auto_fit`
    currently does) gets the hull-corner fallback: convex corners only,
    identical to leaving `silhouette` unset.
    """
    hull = convex_hull(points)
    if len(hull) < 2:
        centre = hull[0] if hull else (0.0, 0.0)
        return _report(hull or [(0.0, 0.0)], centre, 1.0, (0.0, 0.0),
                       Keyline.AREA, 0.0, False)
    if keyline is Keyline.AUTO:
        keyline = classify(hull)
    centre = polygon_centroid(hull)
    base = target_scale(hull, keyline)

    candidates = corner_candidates(silhouette if silhouette else hull)
    if not candidates:
        # Nothing straight to align — a sphere, a fillet blob. Snapping the
        # chords of a discretised curve would nudge the icon off its keyline to
        # align points that do not read as edges anyway.
        scale = _largest_feasible_scale(hull, centre, base)
        return _report(hull, centre, scale, (0.0, 0.0), keyline, 0.0,
                       scale < base)
    weights = [weight for _, weight in candidates]

    # A keyline is a maximum, not a target to straddle, so only the area class
    # may search upward.
    high = base * (1.0 + SCALE_BAND) if keyline is Keyline.AREA else base
    low = base / (1.0 + SCALE_BAND)

    best = None
    for scale in _scale_steps(hull, centre, low, high):
        placed = [_place(point, centre, scale) for point, _ in candidates]
        offset_x, cost_x = _best_offset([p[0] for p in placed], weights)
        offset_y, cost_y = _best_offset([p[1] for p in placed], weights)
        offset = (offset_x, offset_y)
        if not within_trim([_place(point, centre, scale, offset)
                            for point in hull]):
            continue
        cost = cost_x + cost_y
        if best is None or cost < best[0]:
            best = (cost, scale, offset)

    if best is None:
        clamped_scale = _largest_feasible_scale(hull, centre, high)
        return _report(hull, centre, clamped_scale, (0.0, 0.0), keyline,
                       0.0, True)

    cost, scale, offset = best
    return _report(hull, centre, scale, offset, keyline,
                   math.sqrt(cost / sum(weights)), False)
