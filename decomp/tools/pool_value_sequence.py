"""Compare what our code ASKS the literal pool for against what retail's does.

For each function, walk `.text` in address order and record the VALUE of every
data-section word the function references. Do it for our object and for the
retail split object, and compare the two sequences.

Equal sequences mean the two objects request exactly the same constants in
exactly the same order -- the generated code is already saying what retail's
says, and any remaining section score is purely which slot each constant landed
in. That order is fixed by the front end from the SOURCE TEXT, so no register,
schedule or statement-level work reaches it (docs/priced_classes.md section 10).

A differing sequence is the opposite verdict: there is still a real value or a
real reference to recover, and that is where the work belongs.

Usage:
  python3 tools/pool_value_sequence.py <src-path> [section]
  python3 tools/pool_value_sequence.py --all [section]

Exit status: 0 if every function's sequence matches, 1 otherwise.
"""
from __future__ import annotations

import json
import re
import struct
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
VERSION = "GSAE01"
OBJDUMP = REPO / "build" / "binutils" / "powerpc-eabi-objdump"

sys.path.insert(0, str(REPO / "tools"))
from score_delta_gate import elf_progbits  # noqa: E402

SYM_RE = re.compile(r"^([0-9a-f]{8})\s+.{7}\s+(\S+)\s+([0-9a-f]{8})\s+(\S+)$")
FUNC_RE = re.compile(r"^[0-9a-f]+ <(\S+)>:")
RELOC_RE = re.compile(r"R_PPC_EMB_SDA21\s+(\S+)")


def sequences(obj: Path, section: str) -> tuple[dict[str, list[str]], list[str]]:
    """{function: [word value, ...]} in .text address order, plus the function order."""
    dump = subprocess.run([str(OBJDUMP), "-t", str(obj)], capture_output=True, text=True).stdout
    offsets = {}
    for line in dump.splitlines():
        m = SYM_RE.match(line)
        if m and m.group(2) == section:
            offsets[m.group(4)] = int(m.group(1), 16)
    blob = elf_progbits(str(obj)).get(section, b"")

    def word(off: int) -> str:
        if off + 4 <= len(blob):
            return "%08x" % struct.unpack(">I", blob[off:off + 4])[0]
        return "----"

    text = subprocess.run([str(OBJDUMP), "-M", "gekko", "-drz", "-j", ".text", str(obj)],
                          capture_output=True, text=True).stdout
    current = None
    seqs: dict[str, list[str]] = {}
    order: list[str] = []
    for line in text.splitlines():
        m = FUNC_RE.match(line)
        if m:
            current = m.group(1)
            seqs.setdefault(current, [])
            order.append(current)
            continue
        m = RELOC_RE.search(line)
        if m and current is not None and m.group(1) in offsets:
            value = word(offsets[m.group(1)])
            if not seqs[current] or seqs[current][-1] != value:
                seqs[current].append(value)
    return seqs, order


class MissingObject(Exception):
    """A row's object pair is absent -- the row is unscannable, not clean."""


def compare(src_rel: str, section: str, quiet: bool = False) -> tuple[int, int]:
    stem = src_rel[:-2] if src_rel.endswith(".c") else src_rel
    ours = REPO / "build" / VERSION / (stem + ".o")
    retail = REPO / "build" / VERSION / ("obj" + stem[3:] + ".o")
    if not (ours.is_file() and retail.is_file()):
        raise MissingObject(f"missing object for {src_rel}")
    ours_seqs, _ = sequences(ours, section)
    retail_seqs, retail_order = sequences(retail, section)
    same = diff = 0
    for fn in retail_order:
        a = ours_seqs.get(fn, [])
        b = retail_seqs.get(fn, [])
        if not a and not b:
            continue
        if a == b:
            same += 1
            continue
        diff += 1
        if not quiet:
            print("DIFF %-40s n=%d/%d" % (fn, len(a), len(b)))
            print("      ours  ", " ".join(a))
            print("      retail", " ".join(b))
    return same, diff


def sub100_sections() -> list[tuple[str, str, float, int]]:
    report = json.loads((REPO / "build" / VERSION / "report.json").read_text())
    rows = []
    for unit in report["units"]:
        src = unit.get("metadata", {}).get("source_path")
        if not src or not src.startswith("src/"):
            continue
        for sec in unit.get("sections", []):
            if sec["name"] == ".text":
                continue
            score = sec.get("fuzzy_match_percent", 0.0)
            size = int(sec.get("size", 0))
            if score < 100.0 and size:
                rows.append((src, sec["name"], score, size))
    return rows


def main() -> int:
    args = sys.argv[1:]
    if not args:
        raise SystemExit(__doc__)
    if args[0] == "--all":
        section = args[1] if len(args) > 1 else None
        bad = 0
        scanned = 0
        errors = []
        rows = [row for row in sub100_sections() if not section or row[1] == section]
        for src, sec, score, size in rows:
            try:
                same, diff = compare(src, sec, quiet=True)
            except MissingObject as exc:
                errors.append(str(exc))
                print("%-50s %-11s %7.3f %6dB  UNSCANNED  %s" % (src, sec, score, size, exc))
                continue
            scanned += 1
            bad += 1 if diff else 0
            print("%-50s %-11s %7.3f %6dB  SAME %2d  DIFF %2d" % (src, sec, score, size, same, diff))
        print("population %d  scanned %d  differing %d  unscanned %d"
              % (len(rows), scanned, bad, len(errors)))
        return 1 if bad or errors else 0
    section = args[1] if len(args) > 1 else ".sdata2"
    try:
        same, diff = compare(args[0], section)
    except MissingObject as exc:
        raise SystemExit(str(exc))
    print("value-sequence SAME %d  DIFF %d" % (same, diff))
    return 1 if diff else 0


if __name__ == "__main__":
    sys.exit(main())
