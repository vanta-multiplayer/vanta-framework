#!/usr/bin/env python3
"""Find a code site that references a global, so the global can be scanned for.

A global lives in .data and has no bytes of its own worth matching - every
build lays it out differently and nothing about the storage identifies it. What
IS distinctive is the code that touches it: `mov eax, [0xB7CD98]` encodes the
address as an operand, and the instruction around it is stable across builds
even when the address is not.

So the signature anchors on the instruction and the client reads the operand
back out of it, which is exactly what the RGL scaffold already does for
CWorld::Players:

    const auto site = hook::get_pattern<uint8_t>("69 C0 90 01 00 00 8B 80 ? ? ? ?");
    gPatterns.CWorld__Players = *(const uint32_t *)(site + 8);

This script does the finding. Given a known address it locates every dword in
the image that equals it, then for each one grows a signature over the
surrounding code with the operand itself wildcarded, and reports those that
resolve uniquely - along with the operand offset the client needs.

    python scripts/derive_global.py <gta_sa.exe> 0x00B7CD98

Candidates are ranked by how much context sits BEFORE the operand: a site with
a distinctive lead-in is far likelier to survive a build change than one that
is unique only because of what follows it.
"""

import argparse
import struct
import sys
from pathlib import Path

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
    exec_ranges = []
    off = e_lfanew + 24 + opt_size
    for _ in range(sections):
        vsize, va, rsize, praw = struct.unpack_from("<IIII", raw, off + 8)
        chars = struct.unpack_from("<I", raw, off + 36)[0]
        image[va:va + rsize] = raw[praw:praw + rsize]
        if chars & 0x20000000:  # IMAGE_SCN_MEM_EXECUTE
            exec_ranges.append((va, va + min(vsize, rsize)))
        off += 40

    return bytes(image), image_base, exec_ranges


def in_exec(rva: int, exec_ranges) -> bool:
    return any(lo <= rva < hi for lo, hi in exec_ranges)


def matches(image: bytes, tokens, limit=4):
    """Count where a token list matches. Stops early - 'one' vs 'many' is all we need.

    Seeks on the longest run of literal bytes rather than on the first byte.
    These signatures are mostly wildcards around an operand, so a first-byte
    scan visits tens of thousands of false starts per candidate and the search
    never finishes; anchoring on the longest run makes it near-instant.
    """
    n = len(tokens)

    best_start, best_len = 0, 0
    run_start = None
    for idx in range(n + 1):
        literal = idx < n and tokens[idx] != WILDCARD
        if literal and run_start is None:
            run_start = idx
        elif not literal and run_start is not None:
            if idx - run_start > best_len:
                best_start, best_len = run_start, idx - run_start
            run_start = None

    if best_len == 0:
        return []

    seed = bytes(int(t, 16) for t in tokens[best_start:best_start + best_len])

    hits, start = [], 0
    while len(hits) < limit:
        i = image.find(seed, start)
        if i < 0:
            break
        start = i + 1
        begin = i - best_start
        if begin < 0 or begin + n > len(image):
            continue
        for j, tok in enumerate(tokens):
            if tok != WILDCARD and image[begin + j] != int(tok, 16):
                break
        else:
            hits.append(begin)
    return hits


# Opcodes that commonly open the instruction referencing a global. Not a
# disassembler - just enough to tell "this begins an instruction" from "this
# begins wherever the backwards walk happened to stop".
_PLAUSIBLE_LEAD = {
    0x8B,  # mov r32, r/m32
    0x89,  # mov r/m32, r32
    0xA1,  # mov eax, moffs32
    0xA3,  # mov moffs32, eax
    0x69,  # imul r32, r/m32, imm32
    0x6B,  # imul r32, r/m32, imm8
    0x8D,  # lea
    0xC7,  # mov r/m32, imm32
    0x83,  # grp1 r/m32, imm8
    0x81,  # grp1 r/m32, imm32
    0x39, 0x3B,  # cmp
    0x85,  # test
    0x80,  # grp1 r/m8, imm8
    0x0F,  # two-byte opcodes
    0x50, 0x51, 0x52, 0x53, 0x55, 0x56, 0x57,  # push r32
    0xE8, 0xE9,  # call/jmp rel32
    0xFF,  # grp5
    0x33,  # xor
}


def starts_plausibly(signature: str) -> bool:
    first = signature.split(None, 1)[0]
    return first != WILDCARD and int(first, 16) in _PLAUSIBLE_LEAD


def candidates(image, image_base, exec_ranges, target, max_before=12, max_after=10):
    """Every code site referencing `target`, with a unique signature if one exists."""
    needle = struct.pack("<I", target)
    out = []

    start = 0
    while True:
        i = image.find(needle, start)
        if i < 0:
            break
        start = i + 1

        if not in_exec(i, exec_ranges):
            continue

        # Grow backwards first: context before the operand is what makes a
        # signature meaningful rather than merely unique.
        for before in range(2, max_before + 1):
            begin = i - before
            if begin < 0:
                continue

            for after in range(0, max_after + 1):
                toks = [f"{b:02X}" for b in image[begin:i]]
                toks += [WILDCARD] * 4                       # the operand
                toks += [f"{b:02X}" for b in image[i + 4:i + 4 + after]]

                while toks and toks[-1] == WILDCARD:
                    toks.pop()
                if not toks or toks[0] == WILDCARD:
                    continue

                hits = matches(image, toks)
                if len(hits) == 1 and hits[0] == begin:
                    out.append({
                        "site": begin + image_base,
                        "operand_offset": before,
                        "signature": " ".join(toks),
                        "before": before,
                    })
                    break
            else:
                continue
            break

    # Rank by how plausibly the signature STARTS on an instruction, then by how
    # much context precedes the operand.
    #
    # Growing backwards byte by byte does not respect instruction boundaries, so
    # a candidate can be "unique" only because it begins in the middle of some
    # unrelated immediate - `92 8C 05 00 00 ...` is the tail of a float, not an
    # opcode. Such a signature is unique here and meaningless anywhere else: the
    # bytes it leans on are the ones most likely to change.
    #
    # A signature that opens on a recognisable instruction is describing what
    # the code DOES, which is the part that survives a rebuild.
    out.sort(key=lambda c: (0 if starts_plausibly(c["signature"]) else 1, -c["before"]))
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("exe", type=Path)
    ap.add_argument("addresses", nargs="*", help="globals to anchor, eg 0x00B7CD98")
    ap.add_argument("--batch", type=Path, help="file of '0xADDRESS name' lines")
    ap.add_argument("--top", type=int, default=1, help="candidates to print per global")
    args = ap.parse_args()

    wanted = [(int(a, 16), "") for a in args.addresses]
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

    image, image_base, exec_ranges = map_image(args.exe)

    failures = 0
    for target, name in wanted:
        label = name or f"0x{target:08X}"
        found = candidates(image, image_base, exec_ranges, target)
        if not found:
            print(f"FAIL  {label:<40} 0x{target:08X}  no uniquely anchorable reference",
                  file=sys.stderr)
            failures += 1
            continue

        for c in found[:args.top]:
            print(f'"{c["signature"]}"  // {label} @ 0x{target:08X} '
                  f'(site 0x{c["site"]:08X}, operand +{c["operand_offset"]})')

    if failures:
        print(f"\n# {failures} of {len(wanted)} could not be anchored", file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
