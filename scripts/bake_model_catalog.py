#!/usr/bin/env python3
"""Write the retail GTA:SA model allow-list as a VNTC catalog.

The server loads this binary instead of trusting a script or a client
with a model id. Custom skins / cars later are the same file (or a
second file merged on top) with extra ids — never a hole in the list.

    python scripts/bake_model_catalog.py
    python scripts/bake_model_catalog.py --out data/catalog/gta_sa.models
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

MAGIC = b"VNTC"
VERSION = 1

PED_EXCLUDED = {74, 149, 208}


def table(ids: list[int]) -> bytes:
    ids = sorted(set(ids))
    return struct.pack("<H", len(ids)) + b"".join(struct.pack("<H", i) for i in ids)


def build() -> bytes:
    vehicles = list(range(400, 612))
    peds = [i for i in range(0, 313) if i not in PED_EXCLUDED]
    weapons = list(range(0, 0x2F))  # unarmed .. parachute
    upgrades = list(range(1000, 1194))

    return MAGIC + struct.pack("<HH", VERSION, 0) + table(vehicles) + table(peds) + table(weapons) + table(upgrades)


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", type=Path, default=root / "data" / "catalog" / "gta_sa.models")
    args = ap.parse_args()

    args.out.parent.mkdir(parents=True, exist_ok=True)
    payload = build()
    args.out.write_bytes(payload)
    print(f"wrote {args.out} ({len(payload)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
