#!/usr/bin/env python3
"""
convert_for_figma.py — Convert FreeCAD W3C token exports to the JSON format
used by the "Export/Import Variables" Figma plugin.

Produces one JSON file per Figma collection, each containing all modes:
  tokens.out/FreeCAD_Theme.json    — Theme collection  (Light + Dark modes)
  tokens.out/FreeCAD_Density.json  — Density collection (Default mode)

Usage:
    python tools/convert_for_figma.py \\
        --light tokens.light.json \\
        --dark  tokens.dark.json \\
        --out   tokens.out/

Export the JSON files first:
    FreeCAD → Edit → Preferences → Theme Editor → Export Tokens…
"""

from __future__ import annotations

import argparse
import json
import os
import re
from typing import Any


RESERVED_KEYS = {"$schema", "$type", "$value", "$description", "$extensions"}

COLLECTION_DEFS: dict[str, dict] = {
    "Theme":   {"figma_name": "FreeCAD / Theme",   "cli_modes": ["light", "dark"]},
    "Density": {"figma_name": "FreeCAD / Density", "cli_modes": ["light"]},
}

CLI_MODE_NAMES: dict[str, str] = {
    "light":   "Light",
    "dark":    "Dark",
    "compact": "Compact",
}

DENSITY_MODE_NAME = "Default"


# ── Value parsing ─────────────────────────────────────────────────────────────

def parse_alias(raw: Any) -> str | None:
    """Return the alias target name if raw is a W3C alias reference {TokenName}, else None."""
    match = re.match(r'^\{([^}]+)\}$', str(raw).strip())
    return match.group(1) if match else None


def parse_color(raw: str) -> dict | None:
    """Parse a CSS color string to Figma's {r, g, b, a} float format."""
    raw = str(raw).strip()
    if raw.startswith("#"):
        hex_str = raw.lstrip("#")
        if len(hex_str) == 6:
            red, green, blue = (int(hex_str[i:i + 2], 16) / 255.0 for i in (0, 2, 4))
            return {"r": red, "g": green, "b": blue, "a": 1.0}
        if len(hex_str) == 8:
            red, green, blue, alpha = (int(hex_str[i:i + 2], 16) / 255.0 for i in (0, 2, 4, 6))
            return {"r": red, "g": green, "b": blue, "a": alpha}
    match = re.match(r"^rgba\(\s*(\d+),\s*(\d+),\s*(\d+),\s*([\d.]+)\s*\)$", raw)
    if match:
        red, green, blue = (int(match.group(i)) / 255.0 for i in (1, 2, 3))
        return {"r": red, "g": green, "b": blue, "a": float(match.group(4))}
    return None


def parse_number(raw: Any) -> float | None:
    if isinstance(raw, (int, float)):
        return float(raw)
    text = str(raw).strip().removesuffix("px").removesuffix("%")
    try:
        return float(text)
    except ValueError:
        return None


def figma_value(token: dict) -> Any | None:
    """Convert a W3C token to a Figma variable value, or None to skip."""
    token_type = token.get("$type", "")
    raw = token.get("$value")
    if token_type == "color":
        return parse_color(raw)
    if token_type in ("dimension", "number"):
        return parse_number(raw)
    return None


def figma_type(token_type: str) -> str | None:
    return {"color": "COLOR", "dimension": "FLOAT", "number": "FLOAT"}.get(token_type)


# ── W3C token flattening ──────────────────────────────────────────────────────

def flatten_tokens(
    tree: dict,
    prefix: str = "",
    inherited_collection: str | None = None,
) -> list[tuple[str, dict, str | None]]:
    results: list[tuple[str, dict, str | None]] = []
    for key, node in tree.items():
        if key in RESERVED_KEYS or not isinstance(node, dict):
            continue
        path = f"{prefix}/{key}" if prefix else key
        collection = (
            node.get("$extensions", {}).get("freecad", {}).get("collection")
            or inherited_collection
        )
        if "$value" in node:
            results.append((path, node, collection))
        else:
            results.extend(flatten_tokens(node, path, collection))
    return results


# ── Collection builder ────────────────────────────────────────────────────────

def build_collection(
    logical_key: str,
    col_def: dict,
    tokens_by_cli_mode: dict[str, list[tuple[str, dict, str | None]]],
) -> dict | None:
    active_cli_modes = [m for m in col_def["cli_modes"] if m in tokens_by_cli_mode]
    if not active_cli_modes:
        return None

    # Assign stable fake IDs (the plugin ignores them on import and creates real ones)
    col_id = f"VariableCollectionId:freecad:{logical_key.lower()}"
    modes: dict[str, str] = {}  # mode_id → mode_name
    cli_key_to_mode_id: dict[str, str] = {}

    for index, cli_key in enumerate(active_cli_modes):
        mode_id = f"freecad:{logical_key.lower()}:mode:{index}"
        mode_name = DENSITY_MODE_NAME if logical_key == "Density" else CLI_MODE_NAMES[cli_key]
        modes[mode_id] = mode_name
        cli_key_to_mode_id[cli_key] = mode_id

    # Collect all unique (path, figma_type) pairs across all active modes
    all_paths: dict[str, str] = {}  # path → figma_type (first encountered wins)
    for cli_key in active_cli_modes:
        for path, token, token_collection in tokens_by_cli_mode[cli_key]:
            if token_collection != logical_key or path in all_paths:
                continue
            ftype = figma_type(token.get("$type", ""))
            if ftype:
                all_paths[path] = ftype

    if not all_paths:
        return None

    variables: list[dict] = []
    variable_ids: list[str] = []

    # Build name → var_id lookup for alias resolution within this collection
    var_id_by_name: dict[str, str] = {
        path: f"VariableID:freecad:{logical_key.lower()}:{index}"
        for index, path in enumerate(all_paths)
    }

    for var_index, (path, ftype) in enumerate(all_paths.items()):
        var_id = f"VariableID:freecad:{logical_key.lower()}:{var_index}"
        variable_ids.append(var_id)

        values_by_mode: dict[str, Any] = {}
        for cli_key in active_cli_modes:
            mode_id = cli_key_to_mode_id[cli_key]
            # Density always uses the same mode regardless of which CLI file contributes
            target_mode_id = next(iter(modes)) if logical_key == "Density" else mode_id

            token_map = {p: t for p, t, c in tokens_by_cli_mode[cli_key] if c == logical_key}
            token = token_map.get(path)
            if token is None:
                continue

            alias_target = parse_alias(token.get("$value"))
            if alias_target and alias_target in var_id_by_name:
                values_by_mode[target_mode_id] = {
                    "type": "VARIABLE_ALIAS",
                    "id": var_id_by_name[alias_target],
                }
            else:
                value = figma_value(token)
                if value is not None:
                    values_by_mode[target_mode_id] = value

        if values_by_mode:
            variables.append({
                "id":                 var_id,
                "name":               path,
                "description":        "",
                "type":               ftype,
                "valuesByMode":       values_by_mode,
                "scopes":             ["ALL_SCOPES"],
                "hiddenFromPublishing": False,
                "codeSyntax":         {},
            })

    return {
        "id":          col_id,
        "name":        col_def["figma_name"],
        "modes":       modes,
        "variableIds": variable_ids,
        "variables":   variables,
    }


# ── Main ──────────────────────────────────────────────────────────────────────

def convert(light_path: str, dark_path: str | None, out_dir: str) -> None:
    os.makedirs(out_dir, exist_ok=True)

    tokens_by_cli_mode: dict[str, list] = {}

    with open(light_path, encoding="utf-8") as fh:
        tokens_by_cli_mode["light"] = flatten_tokens(json.load(fh))
    print(f"  Light: {len(tokens_by_cli_mode['light'])} tokens")

    if dark_path:
        with open(dark_path, encoding="utf-8") as fh:
            tokens_by_cli_mode["dark"] = flatten_tokens(json.load(fh))
        print(f"  Dark:  {len(tokens_by_cli_mode['dark'])} tokens")

    for logical_key, col_def in COLLECTION_DEFS.items():
        collection = build_collection(logical_key, col_def, tokens_by_cli_mode)
        if not collection:
            print(f"  (skipping {logical_key} — no tokens)")
            continue

        file_name = col_def["figma_name"].replace(" / ", "_").replace(" ", "_") + ".json"
        out_path = os.path.join(out_dir, file_name)
        with open(out_path, "w", encoding="utf-8") as fh:
            json.dump(collection, fh, indent=2)
            fh.write("\n")

        mode_names = list(collection["modes"].values())
        print(f"  {out_path}  ({len(collection['variables'])} variables, modes: {', '.join(mode_names)})")


def main() -> None:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--light", required=True, metavar="JSON",
                        help="W3C tokens JSON exported from the Light theme")
    parser.add_argument("--dark",  metavar="JSON",
                        help="W3C tokens JSON exported from the Dark theme")
    parser.add_argument("--out",   default="tokens.out", metavar="DIR",
                        help="Output directory (default: tokens.out/)")
    args = parser.parse_args()

    print("Converting tokens for Figma import…")
    convert(args.light, args.dark, args.out)
    print()
    print("Import each file via Plugins → Export/Import Variables → Import.")


if __name__ == "__main__":
    main()
