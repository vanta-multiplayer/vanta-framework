#!/usr/bin/env python3
"""Build the Solid interface and embed it for the client.

    python scripts/build_ui.py

Runs `npm install` (if needed) and `npm run build` in ui/, then
scripts/embed_ui.py so the next client compile serves http://vanta/ui.html
from the DLL. No files are copied next to the binary.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
UI = REPO / "ui"


def npm() -> str:
    # CreateProcess does not apply PATHEXT. A shell finds npm.cmd; this does not.
    for name in ("npm.cmd", "npm"):
        found = shutil.which(name)
        if found:
            return found

    raise FileNotFoundError("npm is not on PATH")


def run(args: list[str], cwd: Path) -> None:
    print("+", " ".join(args), f"(in {cwd})")
    subprocess.run(args, cwd=cwd, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--skip-install", action="store_true", help="do not run npm install")
    parser.add_argument("-o", "--output", type=Path, help="generated header path")
    args = parser.parse_args()

    if not UI.exists():
        print(f"error: {UI} is missing", file=sys.stderr)
        return 2

    npm_exe = npm()

    if not args.skip_install and not (UI / "node_modules").exists():
        run([npm_exe, "install"], UI)

    run([npm_exe, "run", "build"], UI)

    built = UI / "dist" / "index.html"
    fallback = REPO / "data" / "ui" / "index.html"
    fallback.parent.mkdir(parents=True, exist_ok=True)
    fallback.write_bytes(built.read_bytes())
    print(f"updated {fallback} so the client embed sees this build")

    embed = [sys.executable, str(REPO / "scripts" / "embed_ui.py"), str(built)]
    if args.output:
        embed.extend(["-o", str(args.output)])
    run(embed, REPO)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
