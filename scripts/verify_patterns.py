#!/usr/bin/env python3
"""Prove every signature in patterns.cpp resolves to the address it claims.

A signature is only as good as its uniqueness, and uniqueness is easy to lose by
accident: shortening a pattern while tidying, copying one by hand, or a game
update shifting the bytes it leaned on. When that happens the client does not
misbehave subtly - hook::get_pattern asserts on anything other than exactly one
match, so the mod dies at startup. Better to find out here.

Each `Resolve("...")` and `get_pattern("...")` call in patterns.cpp carries the
1.0 US address it was derived from in a trailing comment. That comment is the
expectation this checks:

    python scripts/verify_patterns.py <gta_sa.exe> [patterns.cpp]

Reports three distinct failures, because they need different fixes:

    AMBIGUOUS  the signature matches more than once - it needs more context
    MISSING    it matches nothing - the bytes moved, or the pattern is wrong
    WRONG      it matches once, somewhere else - it is describing other code

Exits non-zero if any signature fails, so it can gate a build.
"""

import re
import struct
import sys
from pathlib import Path

# Both spellings used in patterns.cpp, each followed by the address it came from.
#
# Two comment forms, because a signature can claim two different things:
#
#   Resolve("...");                 // 0xADDRESS        the match IS the address
#   OperandAt(get_pattern("..."), n); // site 0xADDRESS  the match is the code
#                                                       site holding the operand
#
# A global is read out of an instruction, so the pattern resolves to the code
# that touches it and not to the global itself. Checking the global's address
# against the match would fail every time, which is what "site" distinguishes.
CALL_RE = re.compile(r'(?:Resolve|get_pattern)\(\s*"([0-9A-Fa-f? ]+)"\s*\).*?//\s*(?:site\s+)?(0x[0-9A-Fa-f]+)')

WILDCARD = "?"


def map_image(exe: Path):
    """Lay the PE out the way the loader does, so offsets match runtime RVAs."""
    raw = exe.read_bytes()
    e_lfanew = struct.unpack_from("<I", raw, 0x3C)[0]
    sections = struct.unpack_from("<H", raw, e_lfanew + 6)[0]
    opt_size = struct.unpack_from("<H", raw, e_lfanew + 20)[0]
    image_base = struct.unpack_from("<I", raw, e_lfanew + 24 + 28)[0]
    size_of_image = struct.unpack_from("<I", raw, e_lfanew + 24 + 56)[0]

    image = bytearray(size_of_image)
    off = e_lfanew + 24 + opt_size
    for _ in range(sections):
        _vsize, va, rsize, praw = struct.unpack_from("<IIII", raw, off + 8)
        image[va:va + rsize] = raw[praw:praw + rsize]
        off += 40

    return bytes(image), image_base


def find_all(image: bytes, signature: str):
    """Every place the signature matches, sought on its longest literal run.

    Seeking on the first byte instead would be correct but far too slow for a
    pattern that opens with a common opcode.
    """
    tokens = signature.split()

    runs, current = [], []
    for i, tok in enumerate(tokens):
        if tok != WILDCARD:
            current.append((i, tok))
        elif current:
            runs.append(current)
            current = []
    if current:
        runs.append(current)

    if not runs:
        return []

    best = max(runs, key=len)
    seed = bytes(int(t, 16) for _, t in best)
    seed_at = best[0][0]

    hits, start = [], 0
    while True:
        i = image.find(seed, start)
        if i < 0:
            break
        start = i + 1
        begin = i - seed_at
        if begin < 0 or begin + len(tokens) > len(image):
            continue
        if all(t == WILDCARD or image[begin + j] == int(t, 16) for j, t in enumerate(tokens)):
            hits.append(begin)

    return hits


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        print("usage: verify_patterns.py <game.exe> [patterns.cpp]", file=sys.stderr)
        return 2

    exe = Path(sys.argv[1])
    src = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(__file__).resolve().parent.parent / "code" / "client" / "src" / "sdk" / "patterns.cpp"

    if not exe.exists():
        print(f"error: {exe} does not exist", file=sys.stderr)
        return 2
    if not src.exists():
        print(f"error: {src} does not exist", file=sys.stderr)
        return 2

    image, image_base = map_image(exe)
    entries = CALL_RE.findall(src.read_text(encoding="utf-8"))

    if not entries:
        print(f"error: no signatures found in {src.name}", file=sys.stderr)
        return 2

    failures = []
    for signature, expected in entries:
        want = int(expected, 16)
        hits = find_all(image, signature)

        if len(hits) > 1:
            verdict = f"AMBIGUOUS ({len(hits)} matches)"
        elif not hits:
            verdict = "MISSING"
        elif hits[0] + image_base != want:
            verdict = f"WRONG -> 0x{hits[0] + image_base:08X}"
        else:
            continue

        failures.append((signature, expected, verdict))

    checked = len(entries)
    if failures:
        print(f"{len(failures)} of {checked} signatures do not resolve as claimed:\n", file=sys.stderr)
        for signature, expected, verdict in failures:
            print(f"  {expected}  {verdict}\n    {signature}", file=sys.stderr)
        return 1

    print(f"{checked} signatures resolve to the addresses they claim, in {exe.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
