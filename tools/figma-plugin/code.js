// FreeCAD Token Sync — Figma plugin
// Reads W3C design tokens exported from FreeCAD's Theme Editor and syncs:
//   • Figma Variables  — two collections (FreeCAD/Theme, FreeCAD/Density) with N modes
//   • Figma Color Styles — palette primitives + gradients from the primary (first) theme

figma.showUI(__html__, { width: 500, height: 560, title: "FreeCAD Token Sync" });

// ── Constants ─────────────────────────────────────────────────────────────────

var COLLECTION_THEME   = "FreeCAD / Theme";
var COLLECTION_DENSITY = "FreeCAD / Density";
var DENSITY_MODE       = "Default";
var RESERVED_KEYS      = { $schema: 1, $type: 1, $value: 1, $description: 1, $extensions: 1 };

// ── Color parsing ─────────────────────────────────────────────────────────────

function parseColor(raw) {
    var str = String(raw).trim();
    if (str.charAt(0) === "#") {
        var hex = str.slice(1);
        if (hex.length === 6) {
            return {
                r: parseInt(hex.slice(0, 2), 16) / 255,
                g: parseInt(hex.slice(2, 4), 16) / 255,
                b: parseInt(hex.slice(4, 6), 16) / 255,
                a: 1,
            };
        }
        if (hex.length === 8) {
            return {
                r: parseInt(hex.slice(0, 2), 16) / 255,
                g: parseInt(hex.slice(2, 4), 16) / 255,
                b: parseInt(hex.slice(4, 6), 16) / 255,
                a: parseInt(hex.slice(6, 8), 16) / 255,
            };
        }
    }
    var rgba = str.match(/^rgba\(\s*(\d+),\s*(\d+),\s*(\d+),\s*([\d.]+)\s*\)$/);
    if (rgba) {
        return {
            r: parseInt(rgba[1]) / 255,
            g: parseInt(rgba[2]) / 255,
            b: parseInt(rgba[3]) / 255,
            a: parseFloat(rgba[4]),
        };
    }
    return null;
}

function isAliasValue(raw) {
    var s = String(raw).trim();
    return s.charAt(0) === "{" && s.charAt(s.length - 1) === "}";
}

// ── Token flattening ──────────────────────────────────────────────────────────

function flattenTokens(tree, prefix, inheritedCollection, inheritedComponent) {
    var results = [];
    for (var key in tree) {
        if (!tree.hasOwnProperty(key) || RESERVED_KEYS[key]) continue;
        var node = tree[key];
        if (typeof node !== "object" || node === null) continue;
        var path = prefix ? prefix + "/" + key : key;
        var freecad = ((node.$extensions || {}).freecad) || {};
        var collection = freecad.collection || inheritedCollection;
        var component  = freecad.component  || inheritedComponent;
        if ("$value" in node) {
            results.push({ path: path, token: node, collection: collection, component: component });
        } else {
            var sub = flattenTokens(node, path, collection, component);
            for (var i = 0; i < sub.length; i++) results.push(sub[i]);
        }
    }
    return results;
}

function figmaVariableType(tokenType) {
    var map = { color: "COLOR", dimension: "FLOAT", number: "FLOAT", string: "STRING", boolean: "BOOLEAN" };
    return map[tokenType] || null;
}

// ── Gradient helpers ──────────────────────────────────────────────────────────

function isLinearGradient(node) {
    return node && typeof node === "object" && !("$value" in node) &&
        "x1" in node && "y1" in node && "x2" in node && "y2" in node && "stops" in node;
}

function isRadialGradient(node) {
    return node && typeof node === "object" && !("$value" in node) &&
        "cx" in node && "cy" in node && "radius" in node && "stops" in node;
}

// Returns {colorKey, colorNode, posNode} from a stop tuple, finding the color element
// by $type rather than fixed index so stop element ordering is not load-bearing.
function parseStopElements(stop) {
    var colorKey = null, colorNode = null, posNode = null;
    for (var key in stop) {
        if (!stop.hasOwnProperty(key)) continue;
        var child = stop[key];
        if (child && child.$type === "color")     { colorKey = key; colorNode = child; }
        else if (child && child.$type === "number") { posNode = child; }
    }
    return { colorKey: colorKey, colorNode: colorNode, posNode: posNode };
}

function parseGradientStops(stopsNode) {
    var stops = [];
    for (var i = 0; ; i++) {
        var stop = stopsNode[String(i)];
        if (!stop) break;
        var elems = parseStopElements(stop);
        if (!elems.posNode || !elems.colorNode) break;
        var col = parseColor(elems.colorNode.$value);
        if (col) {
            stops.push({ position: parseFloat(elems.posNode.$value), color: col, colorKey: elems.colorKey });
        }
    }
    return stops;
}

// Maps bounding-box coords to gradient UV space where u=0 at start, u=1 at end.
function linearGradientTransform(x1, y1, x2, y2) {
    var dx = x2 - x1, dy = y2 - y1;
    var lenSq = dx * dx + dy * dy;
    if (lenSq < 1e-20) return [[1, 0, 0], [0, 1, 0]];
    return [
        [ dx / lenSq,  dy / lenSq, -(x1 * dx + y1 * dy) / lenSq],
        [-dy / lenSq,  dx / lenSq,  (x1 * dy - y1 * dx) / lenSq],
    ];
}

// Maps bounding-box coords to gradient UV space; circle of radius r around (cx,cy)
// maps to the unit circle centred at (0.5, 0.5).
function radialGradientTransform(cx, cy, radius) {
    if (radius < 1e-10) return [[1, 0, 0], [0, 1, 0]];
    var s = 1 / (2 * radius);
    return [
        [s, 0, -cx * s + 0.5],
        [0, s, -cy * s + 0.5],
    ];
}

function numericValue(node) {
    return node && "$value" in node ? parseFloat(node.$value) : 0;
}

function buildGradientPaint(type, transform, stopsNode) {
    var stops = parseGradientStops(stopsNode);
    if (stops.length < 2) return null;
    return {
        type: type,
        gradientTransform: transform,
        gradientStops: stops.map(function(s) { return { color: s.color, position: s.position }; }),
    };
}

function buildLinearGradientPaint(node) {
    return buildGradientPaint("GRADIENT_LINEAR",
        linearGradientTransform(numericValue(node.x1), numericValue(node.y1), numericValue(node.x2), numericValue(node.y2)),
        node.stops);
}

function buildRadialGradientPaint(node) {
    return buildGradientPaint("GRADIENT_RADIAL",
        radialGradientTransform(numericValue(node.cx), numericValue(node.cy), numericValue(node.radius)),
        node.stops);
}

// ── Color styles ──────────────────────────────────────────────────────────────

// Build the Figma variable name from the W3C path and the component group exported
// by TokenExporter ($extensions.freecad.component). Component tokens get an extra
// leading group: "Button/ButtonPadding/top". Non-component tokens (palettes, spacing)
// keep their existing path: "Neutral/050", "Spacing/100".
function figmaVariableName(path, component) {
    if (!component) return path;
    return component + "/" + path;
}

// Zero-pad numeric shade keys to 3 digits so alphabetical sort = numeric sort.
function normalizeShade(shade) {
    var n = parseInt(shade, 10);
    if (isNaN(n)) return shade;
    if (n < 10)  return "00" + n;
    if (n < 100) return "0"  + n;
    return String(n);
}

// A palette group has only color leaf children (no alias values).
function isPaletteGroup(node) {
    if (!node || typeof node !== "object" || "$value" in node) return false;
    var keys = Object.keys(node).filter(function(k) { return !RESERVED_KEYS[k]; });
    if (keys.length === 0) return false;
    return keys.every(function(k) {
        var child = node[k];
        return child && typeof child === "object" &&
            "$value" in child &&
            child.$type === "color" &&
            !isAliasValue(child.$value);
    });
}

// Binds gradient stop colors to Theme variables. Per the Figma API spec, boundVariables
// lives inside each ColorStop (ColorStop.boundVariables.color), not on the paint.
// Must be called AFTER style.paints is set so style.paints[0] is a Figma-native object.
function bindGradientStops(style, stopsNode, tokenKey, component, themeVarByName) {
    if (!style.paints || style.paints.length === 0) return;
    var paint = style.paints[0];
    if (!paint.gradientStops || paint.gradientStops.length === 0) return;

    var newStops = [];
    var bound = 0;
    for (var i = 0; i < paint.gradientStops.length; i++) {
        var stop = paint.gradientStops[i];
        // Find the color element key by $type so we're not coupled to tuple index order.
        var stopNode = stopsNode[String(i)];
        var colorKey = stopNode ? parseStopElements(stopNode).colorKey : null;
        if (!colorKey) { newStops.push(stop); continue; }
        var variable = themeVarByName[figmaVariableName(tokenKey + "/stops/" + i + "/" + colorKey, component)];
        if (variable) {
            newStops.push(Object.assign({}, stop, {
                boundVariables: { color: { type: "VARIABLE_ALIAS", id: variable.id } },
            }));
            bound++;
        } else {
            newStops.push(stop);
        }
    }

    if (bound === 0) return;

    var newPaint = Object.assign({}, paint, { gradientStops: newStops });
    console.log("[bindGradientStops] " + tokenKey + " — newPaint before assign:", JSON.stringify(newPaint));
    style.paints = [newPaint];
    console.log("[bindGradientStops] " + tokenKey + " — style.paints[0] after assign:", JSON.stringify(style.paints[0]));
}

async function syncColorStyles(themes) {
    var tree = themes[0].tokens;

    // Build variable name→id lookup from existing Theme collection variables.
    var themeVarByName = {};
    var allCollections = await figma.variables.getLocalVariableCollectionsAsync();
    for (var ci = 0; ci < allCollections.length; ci++) {
        if (allCollections[ci].name !== COLLECTION_THEME) continue;
        var colId = allCollections[ci].id;
        var allVars = await figma.variables.getLocalVariablesAsync();
        for (var vi = 0; vi < allVars.length; vi++) {
            if (allVars[vi].variableCollectionId === colId) {
                themeVarByName[allVars[vi].name] = allVars[vi];
            }
        }
    }

    var existing = await figma.getLocalPaintStylesAsync();
    var styleByName = {};
    for (var i = 0; i < existing.length; i++) {
        styleByName[existing[i].name] = existing[i];
    }

    var created = 0, updated = 0;

    function upsertStyle(name, paints) {
        var style = styleByName[name];
        if (style) { updated++; } else { style = figma.createPaintStyle(); style.name = name; styleByName[name] = style; created++; }
        style.paints = paints;
        return style;
    }

    for (var key in tree) {
        if (!tree.hasOwnProperty(key) || RESERVED_KEYS[key]) continue;
        var node = tree[key];
        if (typeof node !== "object" || node === null) continue;

        var gradPaint = isLinearGradient(node) ? buildLinearGradientPaint(node)
                      : isRadialGradient(node)  ? buildRadialGradientPaint(node)
                      : null;
        if (gradPaint) {
            var gradComponent = ((node.$extensions || {}).freecad || {}).component || null;
            var gradStyle = upsertStyle("Gradients/" + key, [gradPaint]);
            bindGradientStops(gradStyle, node.stops, key, gradComponent, themeVarByName);
        } else if (isPaletteGroup(node)) {
            for (var shade in node) {
                if (!node.hasOwnProperty(shade) || RESERVED_KEYS[shade]) continue;
                var shadeNode = node[shade];
                var color = parseColor(shadeNode.$value);
                if (!color) continue;

                var styleName = key + "/" + key + "." + normalizeShade(shade);

                // Find the corresponding Theme variable to bind the style to.
                // Palette tokens have no component, so figmaVariableName returns the
                // plain path: "Neutral/050" — matching the variable name directly.
                var tokenPath = key + "/" + shade;
                var varName = figmaVariableName(tokenPath, null);
                var boundVar = themeVarByName[varName];

                var paint;
                if (boundVar) {
                    // Bind the style fill to the variable so it tracks mode changes.
                    paint = figma.variables.setBoundVariableForPaint(
                        { type: "SOLID", color: { r: color.r, g: color.g, b: color.b }, opacity: color.a },
                        "color",
                        boundVar
                    );
                } else {
                    paint = { type: "SOLID", color: { r: color.r, g: color.g, b: color.b }, opacity: color.a };
                }

                upsertStyle(styleName, [paint]);
            }
        }
    }

    return { created: created, updated: updated };
}

// ── Variables ─────────────────────────────────────────────────────────────────

async function syncVariables(themes) {
    figma.ui.postMessage({ type: "progress", message: "Fetching existing variables…" });

    var existingCollections = await figma.variables.getLocalVariableCollectionsAsync();
    var collectionByName = {};
    for (var i = 0; i < existingCollections.length; i++) {
        collectionByName[existingCollections[i].name] = existingCollections[i];
    }

    // Flatten all themes' tokens
    var flatByTheme = themes.map(function(theme) {
        return flattenTokens(theme.tokens, "", null);
    });

    // ── Collections + modes ───────────────────────────────────────────────────

    // Theme collection: one mode per theme
    var themeCol = collectionByName[COLLECTION_THEME];
    if (!themeCol) {
        themeCol = figma.variables.createVariableCollection(COLLECTION_THEME);
        themeCol.renameMode(themeCol.defaultModeId, themes[0].name);
    }
    var themeModeByName = {};
    for (var m = 0; m < themeCol.modes.length; m++) {
        themeModeByName[themeCol.modes[m].name] = themeCol.modes[m].modeId;
    }
    for (var t = 0; t < themes.length; t++) {
        var modeName = themes[t].name;
        if (!themeModeByName[modeName]) {
            var newId = themeCol.addMode(modeName);
            themeModeByName[modeName] = newId;
        }
    }

    // Density collection: single "Default" mode, read from first theme
    var densityCol = collectionByName[COLLECTION_DENSITY];
    if (!densityCol) {
        densityCol = figma.variables.createVariableCollection(COLLECTION_DENSITY);
        densityCol.renameMode(densityCol.defaultModeId, DENSITY_MODE);
    }
    var densityModeId = densityCol.modes[0].modeId;

    // ── Existing variables lookup ─────────────────────────────────────────────

    var existingVars = await figma.variables.getLocalVariablesAsync();
    var varByKey = {};  // "collectionId::name" → variable
    for (var v = 0; v < existingVars.length; v++) {
        var ev = existingVars[v];
        varByKey[ev.variableCollectionId + "::" + ev.name] = ev;
    }

    // ── Collect all unique (collection, path, type) pairs ────────────────────

    var allThemePaths = {};    // path → figmaType
    var allDensityPaths = {};  // path → figmaType

    for (var ti = 0; ti < flatByTheme.length; ti++) {
        var tokens = flatByTheme[ti];
        for (var j = 0; j < tokens.length; j++) {
            var tok = tokens[j];
            var ftype = figmaVariableType(tok.token.$type || "");
            if (!ftype) continue;
            if (tok.collection === "Theme" && !allThemePaths[tok.path]) {
                allThemePaths[tok.path] = { ftype: ftype, component: tok.component };
            }
            if (tok.collection === "Density" && !allDensityPaths[tok.path]) {
                allDensityPaths[tok.path] = { ftype: ftype, component: tok.component };
            }
        }
    }

    // ── Create/find variables ─────────────────────────────────────────────────

    function findOrCreate(path, component, collection, ftype) {
        var varName = figmaVariableName(path, component);
        var found = varByKey[collection.id + "::" + varName] ||
                    (varName !== path && varByKey[collection.id + "::" + path]);
        if (found) {
            if (found.name !== varName) { try { found.name = varName; } catch (_) {} }
            return found;
        }
        try {
            return figma.variables.createVariable(varName, collection, ftype);
        } catch (err) { return null; }
    }

    var themeVarByPath = {};
    for (var path in allThemePaths) {
        if (!allThemePaths.hasOwnProperty(path)) continue;
        var tEntry = allThemePaths[path];
        var variable = findOrCreate(path, tEntry.component, themeCol, tEntry.ftype);
        if (variable) themeVarByPath[path] = variable;
    }

    var densityVarByPath = {};
    for (var dpath in allDensityPaths) {
        if (!allDensityPaths.hasOwnProperty(dpath)) continue;
        var dEntry = allDensityPaths[dpath];
        var dvar = findOrCreate(dpath, dEntry.component, densityCol, dEntry.ftype);
        if (dvar) densityVarByPath[dpath] = dvar;
    }

    // ── Set mode values ───────────────────────────────────────────────────────

    figma.ui.postMessage({ type: "progress", message: "Setting values…" });

    function resolveValue(rawValue, collection, varByPath) {
        if (isAliasValue(rawValue)) {
            var targetName = String(rawValue).trim().slice(1, -1);
            var targetVar = varByPath[targetName];
            if (targetVar) return { type: "VARIABLE_ALIAS", id: targetVar.id };
        }
        return null;  // caller handles literal parse
    }

    function parseRawValue(token) {
        var type = token.$type || "";
        var raw  = token.$value;
        if (type === "color") return parseColor(raw);
        if (type === "dimension" || type === "number") {
            var num = parseFloat(String(raw).replace(/px$|%$/, ""));
            return isNaN(num) ? null : num;
        }
        return null;
    }

    for (var ti2 = 0; ti2 < themes.length; ti2++) {
        var theme = themes[ti2];
        var modeId = themeModeByName[theme.name];
        var flat = flatByTheme[ti2];

        for (var fi = 0; fi < flat.length; fi++) {
            var item = flat[fi];
            if (item.collection === "Theme" && themeVarByPath[item.path] && modeId) {
                var val = resolveValue(item.token.$value, "Theme", themeVarByPath) ||
                          parseRawValue(item.token);
                if (val !== null && val !== undefined) {
                    themeVarByPath[item.path].setValueForMode(modeId, val);
                }
            }
            // Density: only first theme, single mode
            if (ti2 === 0 && item.collection === "Density" && densityVarByPath[item.path]) {
                var dval = resolveValue(item.token.$value, "Density", densityVarByPath) ||
                           parseRawValue(item.token);
                if (dval !== null && dval !== undefined) {
                    densityVarByPath[item.path].setValueForMode(densityModeId, dval);
                }
            }
        }
    }

    var themeCount  = Object.keys(allThemePaths).length;
    var densityCount = Object.keys(allDensityPaths).length;
    return { themeCount: themeCount, densityCount: densityCount };
}

// ── Message handler ───────────────────────────────────────────────────────────

figma.ui.onmessage = async function(msg) {
    if (msg.type !== "sync") return;

    var themes   = msg.themes;
    var doVars   = msg.operations.variables;
    var doStyles = msg.operations.styles;

    try {
        var parts = [];

        if (doVars) {
            figma.ui.postMessage({ type: "progress", message: "Syncing variables…" });
            var vr = await syncVariables(themes);
            parts.push("Variables: " + vr.themeCount + " theme, " + vr.densityCount + " density");
        }

        if (doStyles) {
            figma.ui.postMessage({ type: "progress", message: "Syncing color styles…" });
            var sr = await syncColorStyles(themes);
            parts.push("Styles: " + sr.created + " created, " + sr.updated + " updated");
        }

        figma.ui.postMessage({ type: "done", message: parts.join(" • ") || "Nothing selected." });
    } catch (err) {
        figma.ui.postMessage({ type: "error", message: String(err) });
    }
};
