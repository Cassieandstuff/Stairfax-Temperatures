#!/usr/bin/env python3
"""Find comparisons whose meaning changed because a rule widened an int to
uintptr_t (signed -> unsigned).

Widening is behavior-preserving for pointer values, but a widened slot compared
as signed changes meaning: `x < 0` becomes always false; `x < n` with a signed
`n` becomes an unsigned comparison. Clang diagnoses both
(-Wtautological-unsigned-zero-compare, -Wsign-compare, plus the -Wtype-limits
family). So compile each TU twice with those warnings:
  baseline = the decomp source with only HAND rules applied (u32 pinned etc.)
  mirror   = the generated mirror TU (all rules, incl. generated)
and report warnings that appear only in the mirror: the widening introduced them.

  python3 tools/mirror/check_signedness.py            # all build-set TUs
  python3 tools/mirror/check_signedness.py src/main/x.c ...
Exit 1 if any newly-introduced warnings are found.
"""
from __future__ import annotations

import concurrent.futures as cf
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
sys.path.insert(0, str(HERE))
import build_mirror as bm  # noqa: E402
import passes  # noqa: E402

WARN = ["-Wtautological-unsigned-zero-compare", "-Wsign-compare",
        "-Wtautological-constant-out-of-range-compare", "-Wtype-limits"]
FLAGS = [f for f in bm.CFLAGS if f not in ("-c", "-Wno-everything")] + ["-fsyntax-only"]
RE_W = re.compile(r"^(?P<f>[^:]+):(?P<l>\d+):\d+: warning: (?P<msg>.*?) \[(?P<w>-W[^\]]+)\]")
KEEP = {"-Wtautological-unsigned-zero-compare", "-Wsign-compare",
        "-Wtautological-constant-out-of-range-compare", "-Wtype-limits",
        "-Wtautological-unsigned-enum-zero-compare"}


def warnings(path: str, includes: list[str]) -> set[tuple[int, str]]:
    r = subprocess.run([bm.CC, *FLAGS, *WARN, *includes, path],
                       capture_output=True, text=True, cwd=REPO)
    out = set()
    for ln in r.stderr.splitlines():
        m = RE_W.match(ln)
        if m and m["w"] in KEEP and Path(m["f"]).name == Path(path).name:
            out.add((int(m["l"]), re.sub(r"'[^']*'", "X", m["msg"])))
    return out


def check(rel: str, hand: dict, tmp: Path, base_inc: list[str]) -> list[str]:
    src = REPO / "decomp" / rel
    base_text, _, _ = passes.apply_pointer_rules(src.read_text(errors="replace"), hand,
                                                 f"decomp/{rel}")
    base = tmp / rel
    base.parent.mkdir(parents=True, exist_ok=True)
    base.write_text(base_text)                      # line-preserving, like the mirror
    mirror = REPO / "mirror" / "src" / rel
    if not mirror.exists():
        return []
    new = warnings(str(mirror), bm.INCLUDES) - warnings(str(base), base_inc)
    return [f"{rel}:{l}: {msg}" for (l, msg) in sorted(new)]


def retyped_symbols() -> dict:
    """{decomp file: {symbol: [decl lines]}} retyped by GENERATED rules"""
    gen = passes.load_pointer_rules(REPO / "mirror" / "rules")
    hand = passes.load_pointer_rules(REPO / "mirror" / "rules", generated=False)
    hand_keys = {(r["file"], r.get("line")) for k in ("retype", "promote") for r in hand[k]}
    out = {}
    for r in gen["retype"]:
        if (r["file"], r.get("line")) not in hand_keys:
            out.setdefault(r["file"], {}).setdefault(r["symbol"], []).append(r["line"])
    return out


def holds_for(found: list[str]) -> list[str]:
    """Map each flagged comparison to the generated-retyped declaration in scope on
    that line, as `file:symbol@declline`. "In scope" = the nearest declaration of
    that name at or before the comparison (C declares before use); a same-named
    slot elsewhere in the file (another function's `int obj`) is not held."""
    syms = retyped_symbols()
    holds = set()
    for f in found:
        rel, line = f.split(":")[0], int(f.split(":")[1])
        text = (REPO / "decomp" / rel).read_text(errors="replace").splitlines()[line - 1]
        hit = False
        for sym, decls in syms.get(f"decomp/{rel}", {}).items():
            before = [d for d in decls if d <= line]
            if before and re.search(rf"\b{re.escape(sym)}\b", text):
                holds.add(f"decomp/{rel}:{sym}@{max(before)}")
                hit = True
        if not hit:
            # no widened declaration in scope: a generated widen_cast did it
            # (e.g. `id == (int)ptr` vs an int id). Hold the casts on this line.
            holds.add(f"decomp/{rel}:{line}")
    return sorted(holds)


def main(argv):
    emit = None
    if argv and argv[0].startswith("--emit-holds="):
        emit, argv = argv[0].split("=", 1)[1], argv[1:]
    tus = argv or bm.read_manifest()
    hand = passes.load_pointer_rules(REPO / "mirror" / "rules", generated=False)
    found = []
    with tempfile.TemporaryDirectory() as td, cf.ThreadPoolExecutor() as ex:
        # baseline headers: HAND rules only (u32 pinned, hand widenings), no generated
        hdr = Path(td) / "include"
        bm.gen_headers(hand, hdr)
        base_inc = [f"-I{hdr}" if i == "-Imirror/include" else i for i in bm.INCLUDES]
        for res in ex.map(lambda t: check(t, hand, Path(td) / "src", base_inc), tus):
            found += res
    for f in found:
        print(f)
    if emit:
        hs = holds_for(found)
        Path(emit).write_text("\n".join(hs) + ("\n" if hs else ""))
        print(f"{len(hs)} slot(s) to hold -> {emit}", file=sys.stderr)
    print(f"\n{len(found)} comparison(s) changed meaning by widening "
          f"({len(tus)} TU(s) checked)", file=sys.stderr)
    return 1 if found else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
