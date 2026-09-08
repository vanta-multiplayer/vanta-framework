#!/usr/bin/env python3
"""Emit @vantamp/types ambient .d.ts from v8pp scripting catalog dumps.

Reads types/catalog/{server,client}.json (schemaVersion 2), syncs polished
Framework vendor files from the Framework tree's types/, and writes:

  types/shared/globals.d.ts   — Entity, Color, vectors, …
  types/{server,client}/globals.d.ts — side-specific globals

Entry points live in separate folders so the IDE never loads both sides into
one program:

  types/server/index.d.ts
  types/client/index.d.ts

    python scripts/generate_scripting_types.py
    python scripts/generate_scripting_types.py --framework-types ../../../../types
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TYPES = ROOT / "types"
CATALOG = TYPES / "catalog"
VENDOR = TYPES / "vendor" / "mafiahub"
SHARED_DIR = TYPES / "shared"
CLIENT_DIR = TYPES / "client"
SERVER_DIR = TYPES / "server"

# Catalog symbol name -> TypeScript global name.
RENAME = {
    "VantaPlayer": "Player",
}

# When VantaPlayer exists, the Framework Player base is emitted under this name.
FRAMEWORK_PLAYER_ALIAS = "FrameworkPlayer"

# Client-only / server-only filters (catalog symbol names before rename).
CLIENT_ONLY = {"Key", "Discord", "Web", "Nametags"}
SERVER_ONLY_EXTRA = {"Vehicles", "World"}
# Identical on both sides — emitted once into generated/shared.d.ts.
SHARED_SYMBOLS = {
    "Entity",
    "Color",
    "Vector2",
    "Vector3",
    "Vector4",
    "Quaternion",
    "ExecutionEnvironment",
}
# Hand-written under vendor/mafiahub/ (Framework) — do not re-emit.
VENDOR_SYMBOLS = {"Messages", "Exports", "Imports"}
# Familiar Node-like globals — keep out of ambient d.ts noise.
OMIT_FROM_TYPES = {"console"} | VENDOR_SYMBOLS

# Methods that exist on the server dump but are not registered on the client.
CLIENT_STRIP_FUNCTIONS = {
    "Player": {
        "kick",
        "emit",
        "getIP",
        "setNametagVisible",
        "setNametagHealthVisible",
        "setNametagText",
        "setNametagColor",
    },
    "VantaPlayer": {
        "spawn",
        "giveWeapon",
        "takeWeapon",
        "takeAllWeapons",
        "setWeaponAmmo",
        "kill",
        "kick",
        "emit",
        "getIP",
        "setNametagVisible",
        "setNametagHealthVisible",
        "setNametagText",
        "setNametagColor",
    },
    "Vehicle": {
        "destroy",
        "addUpgrade",
        "removeUpgrade",
        "removeAllUpgrades",
        "fix",
        "blow",
        "getUpgradeOnSlot",
        "getDoorState",
        "setDoorState",
        "getWheelState",
        "setWheelState",
        "getLightState",
        "setLightState",
        "getPanelState",
        "setPanelState",
        "getDoorOpenRatio",
        "setDoorOpenRatio",
        "setColor",
        "setRotation",
        "getUpgrades",
        "getCompatibleUpgrades",
        "getRotation",
        "setVirtualWorld",
        "setVisibleTo",
    },
    "Entity": {
        "setVirtualWorld",
        "setVisibleTo",
    },
    # Framework registers a different Voice global on the client; strip the
    # server-only proximity-admin surface so the client supplement can replace it.
    "Voice": {
        "setRange",
        "setPlayerRange",
        "getPlayerRange",
        "setPlayerMuted",
        "isPlayerMuted",
        "setPlayerDeaf",
        "isPlayerDeaf",
        "setLocalMute",
        "isLocallyMuted",
        "isPlayerVoiceEnabled",
        "isPlayerTalking",
    },
}

CLIENT_STRIP_PROPERTIES = {
    "Player": {"steamId", "discordId", "hardwareId", "ping", "ip"},
    "VantaPlayer": {"steamId", "discordId", "hardwareId", "ping", "ip"},
}

VENDOR_FILES = (
    "messages.d.ts",
    "imports.d.ts",
    "exports.d.ts",
    "key.d.ts",
    "discord.d.ts",
)


def find_framework_types(explicit: Path | None) -> Path | None:
    if explicit and explicit.is_dir():
        return explicit
    candidates = [
        ROOT.parents[2] / "types",  # new/types when vanta is new/code/projects/vanta
        ROOT.parent.parent.parent / "types",
    ]
    for path in candidates:
        if (path / "framework").is_dir():
            return path
    return None


def sync_vendor(framework_types: Path | None) -> None:
    VENDOR.mkdir(parents=True, exist_ok=True)
    if not framework_types:
        return
    src = framework_types / "framework"
    for name in VENDOR_FILES:
        path = src / name
        if path.is_file():
            shutil.copy2(path, VENDOR / name)


def map_type(name: str) -> str:
    if not name:
        return "any"
    cleaned = name.strip()
    if "v8::" in cleaned or "FunctionCallback" in cleaned:
        return "any"
    # C++ qualified names from type_info → bare JS globals.
    cleaned = cleaned.replace("Framework::Scripting::Builtins::", "")
    cleaned = re.sub(r"\b(?:class|struct|enum)\s+", "", cleaned)
    if "::" in cleaned:
        cleaned = cleaned.rsplit("::", 1)[-1]
    cleaned = cleaned.replace("Core.", "")
    cleaned = re.sub(r"\bUnsubscribe\b", "() => void", cleaned)
    cleaned = re.sub(r"\bEventHandler\b", "(...args: any[]) => any", cleaned)
    cleaned = re.sub(r"\bMessageHandler\b", "MessageHandler", cleaned)
    cleaned = re.sub(r"\bunknown\b", "any", cleaned)
    cleaned = re.sub(r"\bthis\b", "this", cleaned)
    for src, dst in RENAME.items():
        cleaned = re.sub(rf"\b{src}\b", dst, cleaned)
    return cleaned or "any"


def is_callback_info_type(type_name: str) -> bool:
    return "FunctionCallback" in (type_name or "") or "v8::" in (type_name or "")


def normalize_params(fn: dict) -> list[dict]:
    """Drop V8 callback-info ghosts and turn args/values arrays into rest params."""
    raw = list(fn.get("parameters") or [])
    cleaned: list[dict] = []
    for p in raw:
        tname = (p.get("type") or {}).get("name") or ""
        if is_callback_info_type(tname):
            continue
        cleaned.append(p)

    # Infer names for arg0/arg1 from a small method table when docs are missing.
    hints = METHOD_PARAM_HINTS.get(fn.get("name") or "", [])
    for i, p in enumerate(cleaned):
        pname = p.get("name") or ""
        if re.match(r"^arg\d*$", pname) and i < len(hints):
            p = dict(p)
            p["name"] = hints[i]
            cleaned[i] = p
        elif pname in ("arg0", "arg") and fn.get("name") == "constructor":
            p = dict(p)
            p["name"] = "id"
            cleaned[i] = p

    # metadata often records variadic as a single optional any[] named args/values.
    if len(cleaned) >= 1:
        last = cleaned[-1]
        lname = last.get("name") or ""
        ltype = map_type((last.get("type") or {}).get("name") or "")
        if lname in ("args", "values") and ltype in ("any[]", "unknown[]", "any"):
            last = dict(last)
            last["name"] = lname
            last["rest"] = True
            last["type"] = {
                "name": "any",
                "cppName": "",
                "optional": False,
            }
            cleaned[-1] = last
    return cleaned


# When dump only has arg0/arg1, name them for freeroam-facing APIs.
METHOD_PARAM_HINTS: dict[str, list[str]] = {
    "giveWeapon": ["weapon", "ammo"],
    "takeWeapon": ["weapon"],
    "setWeaponAmmo": ["weapon", "ammo"],
    "addUpgrade": ["upgradeId"],
    "removeUpgrade": ["upgradeId"],
    "getUpgradeOnSlot": ["slot"],
    "getDoorState": ["door"],
    "setDoorState": ["door", "state"],
    "getWheelState": ["wheel"],
    "setWheelState": ["wheel", "state"],
    "getLightState": ["light"],
    "setLightState": ["light", "state"],
    "getPanelState": ["panel"],
    "setPanelState": ["panel", "state"],
    "getDoorOpenRatio": ["door"],
    "setDoorOpenRatio": ["door", "ratio"],
    "kick": ["reason"],
    "emit": ["eventName", "payloadJson"],
}


def merge_by_name(existing: list[dict], extra: list[dict], key: str = "name") -> list[dict]:
    by_name = {item.get(key): dict(item) for item in existing if item.get(key)}
    for item in extra:
        name = item.get(key)
        if not name:
            continue
        if name in by_name:
            # Supplement wins field-by-field, including empty parameter lists.
            merged = dict(by_name[name])
            for field, value in item.items():
                if value is None:
                    continue
                if field == "name":
                    continue
                merged[field] = value
            by_name[name] = merged
        else:
            by_name[name] = dict(item)
    ordered = []
    seen = set()
    for item in existing:
        name = item.get(key)
        if name in by_name and name not in seen:
            ordered.append(by_name[name])
            seen.add(name)
    for name, item in by_name.items():
        if name not in seen:
            ordered.append(item)
    return ordered


def apply_supplement(catalog: dict, supplement: dict) -> dict:
    if not supplement:
        return catalog
    by_name = {s.get("name"): s for s in catalog.get("symbols") or []}
    for extra in supplement.get("symbols") or []:
        name = extra.get("name")
        if not name:
            continue
        if name not in by_name:
            by_name[name] = dict(extra)
            catalog.setdefault("symbols", []).append(by_name[name])
            continue
        target = by_name[name]
        if extra.get("bases") is not None:
            target["bases"] = list(extra["bases"])
        if extra.get("description"):
            target["description"] = extra["description"]
        if "properties" in extra:
            target["properties"] = merge_by_name(target.get("properties") or [], extra["properties"])
        if "functions" in extra:
            if extra.get("replaceFunctions"):
                target["functions"] = list(extra["functions"])
            else:
                target["functions"] = merge_by_name(target.get("functions") or [], extra["functions"])
        if "constructor" in extra and extra["constructor"] is not None:
            target["constructor"] = extra["constructor"]
    if "variables" in supplement:
        catalog["variables"] = merge_by_name(catalog.get("variables") or [], supplement["variables"])
    return catalog


def fold_vanta_player(catalog: dict) -> dict:
    """Merge Framework Player into VantaPlayer and drop the Framework-only symbol.

    Scripts only ever see `Player` (Vanta's ctor). Emitting FrameworkPlayer is noise.
    """
    symbols = catalog.get("symbols") or []
    framework = next((s for s in symbols if s.get("name") == "Player"), None)
    vanta = next((s for s in symbols if s.get("name") == "VantaPlayer"), None)
    if not framework or not vanta:
        return catalog

    merged = dict(vanta)
    merged["bases"] = ["Entity"]
    merged["properties"] = merge_by_name(framework.get("properties") or [], vanta.get("properties") or [])
    merged["functions"] = merge_by_name(framework.get("functions") or [], vanta.get("functions") or [])
    # Prefer Framework ctor docs (named `id`) when Vanta only has arg0.
    fw_ctor = framework.get("constructor")
    vt_ctor = vanta.get("constructor")
    if fw_ctor and (not vt_ctor or all(re.match(r"^arg", (p.get("name") or "")) for p in (vt_ctor.get("parameters") or []))):
        merged["constructor"] = fw_ctor
    elif vt_ctor:
        merged["constructor"] = vt_ctor

    catalog["symbols"] = [merged if s.get("name") == "VantaPlayer" else s for s in symbols if s.get("name") != "Player"]
    return catalog


def apply_supplement_properties_only(catalog: dict, supplement: dict) -> dict:
    """Copy property metadata from another supplement without replacing functions."""
    if not supplement:
        return catalog
    by_name = {s.get("name"): s for s in catalog.get("symbols") or []}
    for extra in supplement.get("symbols") or []:
        name = extra.get("name")
        if not name or name not in by_name or "properties" not in extra:
            continue
        target = by_name[name]
        target["properties"] = merge_by_name(target.get("properties") or [], extra["properties"])
        if extra.get("bases") is not None and not target.get("bases"):
            target["bases"] = list(extra["bases"])
    return catalog


def load_supplement(side: str) -> dict:
    path = TYPES / "supplements" / f"{side}.json"
    if not path.is_file():
        return {}
    return json.loads(path.read_text(encoding="utf-8"))


def jsdoc(description: str, indent: str = "    ") -> str:
    text = (description or "").strip()
    if not text:
        return ""
    text = text.replace("Core.", "")
    lines = text.splitlines() or [text]
    if len(lines) == 1:
        return f"{indent}/** {lines[0]} */\n"
    body = "\n".join(f"{indent} * {line}" for line in lines)
    return f"{indent}/**\n{body}\n{indent} */\n"


def emit_params(params: list[dict]) -> str:
    parts = []
    for p in params:
        pname = p.get("name") or "arg"
        if not re.match(r"^[A-Za-z_$][\w$]*$", pname):
            pname = "arg"
        optional = bool((p.get("type") or {}).get("optional"))
        ptype = map_type((p.get("type") or {}).get("name") or "any")
        if p.get("rest"):
            parts.append(f"...{pname}: {ptype}[]")
        else:
            parts.append(f"{pname}{'?' if optional else ''}: {ptype}")
    return ", ".join(parts)


def emit_function(fn: dict, indent: str = "    ", static: bool = False) -> str:
    name = fn.get("name") or "anonymous"
    params = normalize_params(fn)
    ret = map_type(((fn.get("returns") or {}).get("type") or {}).get("name") or "void")
    if ret in ("", "void"):
        ret = "void"
    prefix = "static " if static or fn.get("static") else ""
    out = jsdoc(fn.get("description") or "", indent)
    out += f"{indent}{prefix}{name}({emit_params(params)}): {ret};\n"
    return out


def emit_property(prop: dict, indent: str = "    ") -> str:
    name = prop.get("name") or "value"
    readonly = "readonly " if prop.get("readonly") else ""
    static = "static " if prop.get("static") else ""
    ptype = map_type((prop.get("type") or {}).get("name") or "any")
    out = jsdoc(prop.get("description") or "", indent)
    out += f"{indent}{static}{readonly}{name}: {ptype};\n"
    return out


def resolve_emit_name(symbol: dict, has_vanta_player: bool) -> str | None:
    name = symbol.get("name") or ""
    return RENAME.get(name, name)


def resolve_bases(bases: list[str], has_vanta_player: bool) -> list[str]:
    out = []
    for base in bases or []:
        mapped = RENAME.get(base, base)
        if mapped == FRAMEWORK_PLAYER_ALIAS:
            mapped = "Entity"
        out.append(mapped)
    return out


def emit_symbol(symbol: dict, has_vanta_player: bool) -> str:
    kind = symbol.get("kind")
    emit_name = resolve_emit_name(symbol, has_vanta_player)
    if not emit_name:
        return ""

    desc = symbol.get("description") or ""
    parts: list[str] = []

    if kind in ("constructor", "dataType"):
        bases = resolve_bases(symbol.get("bases") or [], has_vanta_player)
        extends = f" extends {', '.join(bases)}" if bases else ""
        parts.append(jsdoc(desc, "").rstrip() + "\n" if desc else "")
        parts.append(f"declare class {emit_name}{extends} {{\n")
        ctor = symbol.get("constructor")
        if ctor:
            ctor_fn = dict(ctor)
            ctor_fn["name"] = "constructor"
            params = normalize_params(ctor_fn)
            # constructors use `id` not arg0
            for p in params:
                if (p.get("name") or "").startswith("arg"):
                    p["name"] = "id"
            parts.append(jsdoc(ctor.get("description") or "", "    "))
            parts.append(f"    constructor({emit_params(params)});\n")
        for prop in symbol.get("properties") or []:
            parts.append(emit_property(prop))
        for fn in symbol.get("functions") or []:
            parts.append(emit_function(fn, static=bool(fn.get("static"))))
        parts.append("}\n")
        return "".join(parts)

    if kind == "globalObject":
        parts.append(jsdoc(desc, "").rstrip() + "\n" if desc else "")
        parts.append(f"declare const {emit_name}: {{\n")
        for prop in symbol.get("properties") or []:
            parts.append(emit_property(prop))
        for fn in symbol.get("functions") or []:
            fn = dict(fn)
            fn["static"] = False
            parts.append(emit_function(fn))
        parts.append("};\n")
        return "".join(parts)

    return ""


def emit_variable(var: dict) -> str:
    name = var.get("name") or "value"
    vtype = map_type((var.get("type") or {}).get("name") or "any")
    out = jsdoc(var.get("description") or "", "")
    keyword = "const" if var.get("readonly") else "var"
    out += f"declare {keyword} {name}: {vtype};\n"
    return out


def emit_ambient_file(body: str, label: str) -> str:
    return (
        "// Generated by scripts/generate_scripting_types.py - do not edit.\n"
        f"// {label}\n\n"
        + body.strip()
        + "\n"
    )


def _symbol_sort_key(sym: dict, has_vanta: bool):
    name = resolve_emit_name(sym, has_vanta) or ""
    kind = sym.get("kind") or ""
    kind_order = 0 if kind in ("constructor", "dataType") else 1
    priority = {
        "Entity": 0,
        "Color": 1,
        "Vector2": 2,
        "Vector3": 3,
        "Vector4": 4,
        "Quaternion": 5,
        "Player": 6,
        "Vehicle": 7,
    }.get(name, 50)
    return (kind_order, priority, name)


def generate_shared(catalog: dict) -> str:
    """Emit value types + Entity + ExecutionEnvironment once for both sides."""
    symbols = catalog.get("symbols") or []
    chunks: list[str] = []
    for sym in sorted(symbols, key=lambda s: _symbol_sort_key(s, False)):
        raw_name = sym.get("name") or ""
        if raw_name not in SHARED_SYMBOLS:
            continue
        text = emit_symbol(sym, False)
        if text.strip():
            chunks.append(text)
            chunks.append("\n")
    return emit_ambient_file("".join(chunks), "Shared (client + server)")


def generate_side(side: str, catalog: dict) -> str:
    catalog = fold_vanta_player(dict(catalog, symbols=list(catalog.get("symbols") or [])))
    if side == "client":
        catalog = sanitize_client_catalog(catalog)
    symbols = catalog.get("symbols") or []
    has_vanta = any(s.get("name") == "VantaPlayer" for s in symbols)

    chunks: list[str] = []
    for sym in sorted(symbols, key=lambda s: _symbol_sort_key(s, has_vanta)):
        raw_name = sym.get("name") or ""
        if raw_name in OMIT_FROM_TYPES or raw_name in SHARED_SYMBOLS:
            continue
        if side == "server" and raw_name in CLIENT_ONLY:
            continue
        if side == "client" and raw_name in SERVER_ONLY_EXTRA:
            continue
        text = emit_symbol(sym, has_vanta and side == "server")
        if text.strip():
            chunks.append(text)
            chunks.append("\n")

    for var in catalog.get("variables") or []:
        chunks.append(emit_variable(var))
        chunks.append("\n")

    return emit_ambient_file("".join(chunks), f"Side: {side}")


SIDE_TSCONFIG = """\
{
  "compilerOptions": {
    "composite": true,
    "noEmit": true,
    "strict": true,
    "skipLibCheck": true
  },
  "files": ["index.d.ts"]
}
"""

SERVER_ENTRY = """\
/// <reference path="../shared/globals.d.ts" />
/// <reference path="./globals.d.ts" />
/// <reference path="../vendor/mafiahub/messages.d.ts" />
/// <reference path="../vendor/mafiahub/exports.d.ts" />
/// <reference path="../vendor/mafiahub/imports.d.ts" />
"""

CLIENT_ENTRY = """\
/// <reference path="../shared/globals.d.ts" />
/// <reference path="./globals.d.ts" />
/// <reference path="../vendor/mafiahub/messages.d.ts" />
/// <reference path="../vendor/mafiahub/exports.d.ts" />
/// <reference path="../vendor/mafiahub/imports.d.ts" />
/// <reference path="../vendor/mafiahub/key.d.ts" />
/// <reference path="../vendor/mafiahub/discord.d.ts" />
"""

ROOT_TSCONFIG = """\
{
  "files": [],
  "references": [
    { "path": "./client" },
    { "path": "./server" }
  ]
}
"""

SHARED_TSCONFIG = """\
{
  "compilerOptions": {
    "composite": true,
    "noEmit": true,
    "strict": true,
    "skipLibCheck": true
  },
  "files": ["globals.d.ts"]
}
"""


def bootstrap_client_from_server(server: dict) -> dict:
    """Build a usable client catalog when a live client dump is unavailable.

    Keeps shared value types, Entity/Player/Vehicle handles, and client Events;
    drops server-only factory/World/Chat. Strips server-only methods from
    VantaPlayer/Vehicle so bootstrapped d.ts matches the client Register surface.
    Key / Discord / Web stay in vendor hand-written files.
    """
    skip = set(SERVER_ONLY_EXTRA) | {"Chat"}
    symbols = []
    for sym in server.get("symbols") or []:
        name = sym.get("name") or ""
        if name in skip or name in CLIENT_ONLY:
            continue
        sym = json.loads(json.dumps(sym))  # deep copy
        if name == "Events":
            fns = []
            for fn in sym.get("functions") or []:
                fname = fn.get("name") or ""
                if fname in ("onClient", "onceClient", "offClient", "emitAllClients"):
                    continue
                fns.append(fn)
            fns.append(
                {
                    "name": "emitServer",
                    "description": "Sends a named event from this client to the server's isolated onClient handlers.",
                    "static": False,
                    "parameters": [
                        {
                            "name": "eventName",
                            "description": "Server-side client-event name.",
                            "type": {"name": "string", "cppName": "", "optional": False},
                        },
                        {
                            "name": "payload",
                            "description": "Optional string payload sent verbatim; other values are JSON-serialized.",
                            "type": {"name": "any", "cppName": "", "optional": True},
                        },
                    ],
                    "returns": {
                        "description": "",
                        "type": {"name": "void", "cppName": "", "optional": False},
                    },
                    "since": "",
                    "deprecated": "",
                    "examples": [],
                }
            )
            sym["functions"] = fns
            sym["description"] = "Asynchronous resource event bus (client)."
        strip = CLIENT_STRIP_FUNCTIONS.get(name)
        if strip:
            sym["functions"] = [fn for fn in (sym.get("functions") or []) if (fn.get("name") or "") not in strip]
        strip_props = CLIENT_STRIP_PROPERTIES.get(name)
        if strip_props:
            sym["properties"] = [p for p in (sym.get("properties") or []) if (p.get("name") or "") not in strip_props]
        symbols.append(sym)
    return {"schemaVersion": 2, "symbols": symbols, "variables": list(server.get("variables") or [])}


def sanitize_client_catalog(catalog: dict) -> dict:
    """Drop server-only members after folding VantaPlayer into Player."""
    for sym in catalog.get("symbols") or []:
        name = sym.get("name") or ""
        emit = RENAME.get(name, name)
        strip = CLIENT_STRIP_FUNCTIONS.get(name) or CLIENT_STRIP_FUNCTIONS.get(emit)
        if strip:
            sym["functions"] = [fn for fn in (sym.get("functions") or []) if (fn.get("name") or "") not in strip]
        strip_props = CLIENT_STRIP_PROPERTIES.get(name) or CLIENT_STRIP_PROPERTIES.get(emit)
        if strip_props:
            sym["properties"] = [p for p in (sym.get("properties") or []) if (p.get("name") or "") not in strip_props]
    return catalog


def load_catalog(side: str, allow_bootstrap: bool = True) -> dict:
    path = CATALOG / f"{side}.json"
    if path.is_file():
        return json.loads(path.read_text(encoding="utf-8"))
    if side == "client" and allow_bootstrap:
        server_path = CATALOG / "server.json"
        if server_path.is_file():
            print("note: types/catalog/client.json missing — bootstrapping from server.json")
            return bootstrap_client_from_server(json.loads(server_path.read_text(encoding="utf-8")))
    return {"schemaVersion": 2, "symbols": [], "variables": []}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--framework-types",
        type=Path,
        default=None,
        help="Path to Framework types/ (default: auto-detect new/types)",
    )
    parser.add_argument(
        "--write-client-bootstrap",
        action="store_true",
        help="Write types/catalog/client.json from server.json when client dump is missing",
    )
    args = parser.parse_args()

    framework_types = find_framework_types(args.framework_types)
    sync_vendor(framework_types)

    for path in (SHARED_DIR, CLIENT_DIR, SERVER_DIR, CATALOG):
        path.mkdir(parents=True, exist_ok=True)

    if args.write_client_bootstrap:
        server = load_catalog("server", allow_bootstrap=False)
        if not server.get("symbols"):
            print("error: server catalog empty; dump server first")
            return 1
        client = bootstrap_client_from_server(server)
        out = CATALOG / "client.json"
        out.write_text(json.dumps(client, indent=2) + "\n", encoding="utf-8", newline="\n")
        print(f"wrote {out.relative_to(ROOT)}")

    catalogs: dict[str, dict] = {}
    for side in ("server", "client"):
        catalog = load_catalog(side)
        catalog = apply_supplement(catalog, load_supplement(side))
        if side == "client":
            # Replica field docs live in the server supplement; reuse them readonly.
            catalog = apply_supplement_properties_only(catalog, load_supplement("server"))
        catalogs[side] = catalog

    shared_out = SHARED_DIR / "globals.d.ts"
    shared_out.write_text(generate_shared(catalogs["server"]), encoding="utf-8", newline="\n")
    print(f"wrote {shared_out.relative_to(ROOT)}")

    for side, directory, entry in (
        ("server", SERVER_DIR, SERVER_ENTRY),
        ("client", CLIENT_DIR, CLIENT_ENTRY),
    ):
        globals_out = directory / "globals.d.ts"
        globals_out.write_text(generate_side(side, catalogs[side]), encoding="utf-8", newline="\n")
        print(f"wrote {globals_out.relative_to(ROOT)}")

        entry_out = directory / "index.d.ts"
        entry_out.write_text(entry, encoding="utf-8", newline="\n")
        print(f"wrote {entry_out.relative_to(ROOT)}")

        tsconfig_out = directory / "tsconfig.json"
        tsconfig_out.write_text(SIDE_TSCONFIG, encoding="utf-8", newline="\n")

    (SHARED_DIR / "tsconfig.json").write_text(SHARED_TSCONFIG, encoding="utf-8", newline="\n")
    (TYPES / "tsconfig.json").write_text(ROOT_TSCONFIG, encoding="utf-8", newline="\n")

    # Remove legacy layouts that put both sides in one inferred TS project.
    legacy_paths = [
        TYPES / "client.d.ts",
        TYPES / "server.d.ts",
        TYPES / "generated",
    ]
    for legacy in legacy_paths:
        if legacy.is_file():
            legacy.unlink()
            print(f"removed {legacy.relative_to(ROOT)}")
        elif legacy.is_dir():
            shutil.rmtree(legacy)
            print(f"removed {legacy.relative_to(ROOT)}/")

    if framework_types:
        print(f"vendor synced from {framework_types}")
    else:
        print("warning: Framework types/ not found; left existing vendor/ as-is")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
