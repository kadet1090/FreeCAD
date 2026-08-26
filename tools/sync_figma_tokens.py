#!/usr/bin/env python3
"""
sync_figma_tokens.py — Push FreeCAD W3C design tokens to Figma variables.

FreeCAD tokens are split into two independent Figma collections so that
theme (light/dark) and UI density (default/compact) can be switched
independently in Figma:

  FreeCAD / Theme   — modes: Light, Dark
  FreeCAD / Density — modes: Default  (Compact added in the future)

Usage:
    FIGMA_TOKEN=<personal-access-token> python tools/sync_figma_tokens.py \\
        --file  e9Q5Khd0pHr8loEgkp4y3W \\
        --light tokens.light.json \\
        --dark  tokens.dark.json

To add a future Compact density mode, re-run with:
    ... --compact tokens.compact.json

Export the JSON files first via the Theme Editor "Export Tokens..." button:
  1. Open FreeCAD → Edit → Preferences → Theme Editor
  2. Select "FreeCAD Light" theme, click "Export Tokens...", save as tokens.light.json
  3. Select "FreeCAD Dark"  theme, click "Export Tokens...", save as tokens.dark.json

Requirements: requests  (pip install requests)
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from typing import Any

import requests

FIGMA_API = "https://api.figma.com/v1"
RESERVED_KEYS = {"$schema", "$type", "$value", "$description", "$extensions"}

# Each collection maps a logical key to its Figma name.
# "modes_from" lists which CLI mode args contribute a mode to this collection.
# Density values are the same in all themes, so only one source is needed.
COLLECTION_DEFS: dict[str, dict] = {
    "Theme":   {"figma_name": "FreeCAD / Theme",   "cli_modes": ["light", "dark"]},
    "Density": {"figma_name": "FreeCAD / Density", "cli_modes": ["light"]},
}

# Human-readable mode names for each CLI flag.
CLI_MODE_NAMES: dict[str, str] = {
    "light":   "Light",
    "dark":    "Dark",
    "compact": "Compact",
}


# ── W3C token tree helpers ────────────────────────────────────────────────────

def flatten_tokens(
    tree: dict,
    prefix: str = "",
    inherited_collection: str | None = None,
) -> list[tuple[str, dict, str | None]]:
    """Recursively flatten a W3C token tree into (path, leaf, collection) triples.

    The $extensions.freecad.collection key at each root token node sets the
    collection for that token and all its sub-tokens. Sub-tokens inherit the
    collection from their nearest ancestor that has the annotation.
    """
    results: list[tuple[str, dict, str | None]] = []
    for key, node in tree.items():
        if key in RESERVED_KEYS:
            continue
        if not isinstance(node, dict):
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


def figma_variable_type(token_type: str) -> str | None:
    """Map a W3C token $type to a Figma resolvedType string, or None to skip."""
    return {
        "color":     "COLOR",
        "dimension": "FLOAT",
        "number":    "FLOAT",
        "string":    "STRING",
        "boolean":   "BOOLEAN",
    }.get(token_type)


def parse_value(token: dict) -> Any:
    """Convert a W3C token $value to the value format expected by Figma."""
    raw = token["$value"]
    token_type = token.get("$type", "")
    if token_type == "color":
        return _parse_color(raw)
    if token_type in ("dimension", "number"):
        return _parse_number(raw)
    return str(raw)


def _parse_color(value: str) -> dict | None:
    value = str(value).strip()
    if value.startswith("#"):
        hex_str = value.lstrip("#")
        if len(hex_str) == 6:
            red, green, blue = (int(hex_str[i:i + 2], 16) / 255.0 for i in (0, 2, 4))
            return {"r": red, "g": green, "b": blue, "a": 1.0}
        if len(hex_str) == 8:
            red, green, blue, alpha = (int(hex_str[i:i + 2], 16) / 255.0 for i in (0, 2, 4, 6))
            return {"r": red, "g": green, "b": blue, "a": alpha}
    if value.startswith("rgba("):
        parts = value[5:].rstrip(")").split(",")
        if len(parts) == 4:
            red, green, blue = (int(p.strip()) / 255.0 for p in parts[:3])
            alpha = float(parts[3].strip())
            return {"r": red, "g": green, "b": blue, "a": alpha}
    return None


def _parse_number(value: Any) -> float | None:
    if isinstance(value, (int, float)):
        return float(value)
    text = str(value).strip().removesuffix("px").removesuffix("%")
    try:
        return float(text)
    except ValueError:
        return None


# ── Figma API helpers ─────────────────────────────────────────────────────────

class FigmaClient:
    def __init__(self, token: str, file_key: str) -> None:
        self.session = requests.Session()
        self.session.headers["X-Figma-Token"] = token
        self.file_key = file_key

    def get_local_variables(self) -> dict:
        response = self.session.get(f"{FIGMA_API}/files/{self.file_key}/variables/local")
        response.raise_for_status()
        return response.json()

    def post_variables(self, payload: dict) -> dict:
        response = self.session.post(
            f"{FIGMA_API}/files/{self.file_key}/variables",
            json=payload,
        )
        if not response.ok:
            print(f"Figma API error {response.status_code}: {response.text}", file=sys.stderr)
            response.raise_for_status()
        return response.json()


# ── Sync logic ────────────────────────────────────────────────────────────────

def build_payload(
    tokens_by_cli_mode: dict[str, list[tuple[str, dict, str | None]]],
    existing: dict,
) -> dict:
    """Build the Figma batch variables payload for both collections."""

    payload: dict[str, list] = {
        "variableCollections": [],
        "variableModes": [],
        "variables": [],
        "variableModeValues": [],
    }

    existing_collections: dict[str, dict] = existing.get("meta", {}).get("variableCollections", {})
    existing_variables:   dict[str, dict] = existing.get("meta", {}).get("variables", {})

    # Lookup by Figma name → existing collection object
    collection_by_name = {col["name"]: col for col in existing_collections.values()}

    # Track per-collection state we build up.
    collection_id_map: dict[str, str] = {}  # logical key → Figma collection id
    mode_id_map: dict[str, dict[str, str]] = {}  # logical key → {mode_name → Figma mode id}

    # ── Ensure each collection and its modes exist ────────────────────────────
    for logical_key, col_def in COLLECTION_DEFS.items():
        figma_name = col_def["figma_name"]
        cli_modes  = col_def["cli_modes"]

        # Which CLI modes are present in this run?
        active_cli_modes = [m for m in cli_modes if m in tokens_by_cli_mode]
        if not active_cli_modes:
            continue  # No data for this collection in this run

        existing_col = collection_by_name.get(figma_name)
        if existing_col:
            col_id = existing_col["id"]
        else:
            col_id = f"new_col_{logical_key.lower()}"
            first_mode_id = f"new_mode_{logical_key.lower()}_{active_cli_modes[0]}"
            payload["variableCollections"].append({
                "action": "CREATE",
                "id": col_id,
                "name": figma_name,
                "initialModeId": first_mode_id,
            })

        collection_id_map[logical_key] = col_id
        mode_id_map[logical_key] = {}

        existing_modes_by_name = {
            m["name"]: m["id"]
            for m in (existing_col.get("modes", []) if existing_col else [])
        }

        for cli_key in active_cli_modes:
            mode_name = CLI_MODE_NAMES[cli_key]
            if mode_name in existing_modes_by_name:
                mode_id_map[logical_key][mode_name] = existing_modes_by_name[mode_name]
            else:
                temp_id = f"new_mode_{logical_key.lower()}_{cli_key}"
                mode_id_map[logical_key][mode_name] = temp_id
                # Skip adding CREATE for the initial mode (already in variableCollections)
                already_in_initial = (
                    not existing_col
                    and cli_key == active_cli_modes[0]
                )
                if not already_in_initial:
                    payload["variableModes"].append({
                        "action": "CREATE",
                        "id": temp_id,
                        "name": mode_name,
                        "variableCollectionId": col_id,
                    })

        # Default mode for Density is "Default" — rename the auto-created initial mode if needed
        if logical_key == "Density" and not existing_col and active_cli_modes:
            first_mode_id = f"new_mode_{logical_key.lower()}_{active_cli_modes[0]}"
            payload["variableModes"].append({
                "action": "UPDATE",
                "id": first_mode_id,
                "name": "Default",
                "variableCollectionId": col_id,
            })
            mode_id_map[logical_key]["Default"] = first_mode_id
            # Remove what we stored under the CLI name
            cli_name = CLI_MODE_NAMES[active_cli_modes[0]]
            if cli_name != "Default":
                mode_id_map[logical_key].pop(cli_name, None)

    # ── Map existing variables by name within their collection ────────────────
    # name_to_var_id: (collection_id, variable_name) → variable_id
    name_to_var_id: dict[tuple[str, str], str] = {
        (var["variableCollectionId"], var["name"]): var["id"]
        for var in existing_variables.values()
    }

    # ── Collect all unique (path, collection, figma_type) across all modes ────
    all_vars: dict[tuple[str, str], str] = {}  # (col_id, path) → figma_type

    for cli_key, token_list in tokens_by_cli_mode.items():
        for path, token, logical_collection in token_list:
            if not logical_collection:
                continue
            col_id = collection_id_map.get(logical_collection)
            if not col_id:
                continue
            figma_type = figma_variable_type(token.get("$type", ""))
            if figma_type:
                all_vars[(col_id, path)] = figma_type

    var_id_map: dict[tuple[str, str], str] = {}  # (col_id, path) → var_id

    for (col_id, path), figma_type in all_vars.items():
        existing_id = name_to_var_id.get((col_id, path))
        if existing_id:
            var_id_map[(col_id, path)] = existing_id
        else:
            temp_id = f"new_var_{col_id}_{path.replace('/', '_')}"
            var_id_map[(col_id, path)] = temp_id
            payload["variables"].append({
                "action": "CREATE",
                "id": temp_id,
                "name": path,
                "variableCollectionId": col_id,
                "resolvedType": figma_type,
            })

    # ── Build mode values ─────────────────────────────────────────────────────
    for cli_key, token_list in tokens_by_cli_mode.items():
        cli_mode_name = CLI_MODE_NAMES[cli_key]

        for path, token, logical_collection in token_list:
            if not logical_collection:
                continue
            col_id = collection_id_map.get(logical_collection)
            if not col_id:
                continue

            # Density collection: always write to "Default" mode regardless of CLI mode name
            if logical_collection == "Density":
                mode_name = "Default"
            else:
                mode_name = cli_mode_name

            mode_id = mode_id_map.get(logical_collection, {}).get(mode_name)
            if not mode_id:
                continue

            var_id = var_id_map.get((col_id, path))
            if not var_id:
                continue

            parsed = parse_value(token)
            if parsed is None:
                continue

            payload["variableModeValues"].append({
                "variableId": var_id,
                "modeId": mode_id,
                "value": parsed,
            })

    return payload


def sync(file_key: str, token: str, mode_files: dict[str, str]) -> None:
    client = FigmaClient(token, file_key)

    print("Fetching existing Figma variables…")
    existing = client.get_local_variables()

    tokens_by_cli_mode: dict[str, list[tuple[str, dict, str | None]]] = {}
    for cli_key, json_path in mode_files.items():
        with open(json_path, encoding="utf-8") as fh:
            tree = json.load(fh)
        tokens_by_cli_mode[cli_key] = flatten_tokens(tree)
        print(f"  {CLI_MODE_NAMES[cli_key]}: {len(tokens_by_cli_mode[cli_key])} tokens from {json_path}")

    payload = build_payload(tokens_by_cli_mode, existing)

    totals = {k: len(v) for k, v in payload.items() if v}
    if not totals:
        print("Nothing to update.")
        return

    print(
        f"Pushing: "
        f"{len(payload['variableCollections'])} collections, "
        f"{len(payload['variableModes'])} modes, "
        f"{len(payload['variables'])} new variables, "
        f"{len(payload['variableModeValues'])} mode values…"
    )
    client.post_variables(payload)
    print("Done.")


# ── Entry point ───────────────────────────────────────────────────────────────

def main() -> None:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--file", required=True, metavar="FILE_KEY",
                        help="Figma file key (from the URL)")
    parser.add_argument("--light",   metavar="JSON",
                        help="W3C tokens JSON exported from the Light theme")
    parser.add_argument("--dark",    metavar="JSON",
                        help="W3C tokens JSON exported from the Dark theme")
    parser.add_argument("--compact", metavar="JSON",
                        help="W3C tokens JSON for Compact density (future use)")
    args = parser.parse_args()

    figma_token = os.environ.get("FIGMA_TOKEN")
    if not figma_token:
        print("Error: set FIGMA_TOKEN environment variable to your Figma personal access token.",
              file=sys.stderr)
        sys.exit(1)

    mode_files: dict[str, str] = {}
    for cli_key in ("light", "dark", "compact"):
        path = getattr(args, cli_key)
        if path:
            mode_files[cli_key] = path

    if not mode_files:
        print("Error: provide at least one of --light, --dark, or --compact.", file=sys.stderr)
        sys.exit(1)

    sync(args.file, figma_token, mode_files)


if __name__ == "__main__":
    main()
