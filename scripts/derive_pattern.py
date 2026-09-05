#!/usr/bin/env python3
"""Turn a known-good absolute address into a byte signature that finds it again.

Vanta's old client addressed GTA:SA 1.0 US with a table of ~207 literal
addresses. Those are correct, and they are also the only build they are correct
for - the Rockstar Games Launcher build shares none of them. Supporting both
means the client has to *find* its addresses rather than be told them, which is
what the framework's hook::get_pattern already does everywhere else.

So the old table is not thrown away: each verified 1.0 US address is the ground
truth this script derives a signature from. It reads the bytes at the address,
grows a candidate mask until exactly one place in the image matches, and then
proves the match is the address it started from.

    python scripts/derive_pattern.py <gta_sa.exe> 0x53E4FF
    python scripts/derive_pattern.py <gta_sa.exe> --batch addresses.txt

A signature that cannot be made unique is reported rather than papered over: it
means the site needs a different anchor (a nearby call, a string reference),
which is a judgement call and not something to guess at.
"""

import argparse
import struct
import sys
from pathlib import Path

# Bytes that commonly encode an absolute address or a relative displacement, and
# therefore differ between builds. Wildcarding them is what makes a signature
# portable; keeping them is what makes it unique. The tension is the whole job.
WILDCARD = "?"


def map_image(exe: Path):
    """Lay the PE out the way the loader does, so offsets match runtime RVAs.

    Same approach as scripts/build_pattern_table.py in the framework - the
    addresses this produces have to agree with the ones that table resolves.
    """
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

    return bytes(image), image_base, size_of_image


def count_matches(image: bytes, tokens, limit=8):
    """Where does this token list match? Stops early - only 'one' vs 'many' matters."""
    first = tokens[0]
    if first == WILDCARD:
        # A leading wildcard makes the search degenerate. Callers never produce
        # one, but a hand-edited signature might.
        raise ValueError("signature must not start with a wildcard")

    lead = int(first, 16)
    hits = []
    start = 0
    while True:
        i = image.find(bytes([lead]), start)
        if i < 0 or len(hits) >= limit:
            break
        start = i + 1
        if i + len(tokens) > len(image):
            continue
        for j, tok in enumerate(tokens):
            if tok == WILDCARD:
                continue
            if image[i + j] != int(tok, 16):
                break
        else:
            hits.append(i)
    return hits


def tokens_for(image: bytes, rva: int, length: int):
    """The literal bytes at rva, with the operands of common instructions masked.

    Only the displacement/immediate of an instruction is wildcarded, never its
    opcode: the opcode is what makes the signature mean something, and the
    operand is what makes it build-specific.
    """
    out = []
    i = 0
    while i < length:
        b = image[rva + i]
        out.append(f"{b:02X}")

        # E8/E9 rel32 - the target moves with the build, the opcode does not.
        if b in (0xE8, 0xE9) and i + 5 <= length:
            out.extend([WILDCARD] * 4)
            i += 5
            continue

        i += 1

    return out[:length]


def derive(image: bytes, image_base: int, address: int, max_len: 'int' = 48):
    """Grow a signature at `address` until exactly one site in the image matches."""
    rva = address - image_base
    if rva < 0 or rva >= len(image):
        return None, "address is outside the image"

    for length in range(6, max_len + 1):
        tokens = tokens_for(image, rva, length)

        # A signature ending in a wildcard is longer than it needs to be.
        while tokens and tokens[-1] == WILDCARD:
            tokens.pop()
        if not tokens:
            continue

        hits = count_matches(image, tokens)
        if len(hits) == 1:
            if hits[0] != rva:
                return None, f"unique match at 0x{hits[0] + image_base:08X}, not the address given"
            return " ".join(tokens), None

    return None, f"no unique signature within {max_len} bytes"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("exe", type=Path, help="the game executable to derive against")
    ap.add_argument("addresses", nargs="*", help="absolute addresses, eg 0x53E4FF")
    ap.add_argument("--batch", type=Path,
                    help="file of '0xADDRESS name' lines, # comments allowed")
    ap.add_argument("--max-len", type=int, default=48,
                    help="longest signature to try before giving up (default 48)")
    args = ap.parse_args()

    if not args.exe.exists():
        print(f"error: {args.exe} does not exist", file=sys.stderr)
        return 2

    wanted = []
    for a in args.addresses:
        wanted.append((int(a, 16), ""))

    if args.batch:
        for line in args.batch.read_text(encoding="utf-8").splitlines():
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            parts = line.split(None, 1)
            wanted.append((int(parts[0], 16), parts[1].strip() if len(parts) > 1 else ""))

    if not wanted:
        print("error: give at least one address, or --batch", file=sys.stderr)
        return 2

    image, image_base, size = map_image(args.exe)
    print(f"# {args.exe.name}: base 0x{image_base:08X}, {size:,} bytes mapped", file=sys.stderr)

    failures = 0
    for address, name in wanted:
        sig, err = derive(image, image_base, address, args.max_len)
        label = name or f"0x{address:08X}"
        if err:
            print(f"FAIL  {label:<40} 0x{address:08X}  {err}", file=sys.stderr)
            failures += 1
        else:
            print(f'"{sig}"  // {label} @ 0x{address:08X}')

    if failures:
        print(f"\n# {failures} of {len(wanted)} could not be made unique", file=sys.stderr)

    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
