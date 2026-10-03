#!/usr/bin/env python3
"""Regenerate the mirror's generated pointer-width rules to a fixpoint.

  analyze (infer_ptr_ints.py --hold HELD) -> generate mirror/src (--gen-only)
  -> check_signedness.py: comparisons whose meaning the widening changed
  -> hold those slots back (not widened, don't propagate) -> repeat
until no new holds appear. Each analysis also RELEASES holds whose slot no
longer carries (a later hold cut the path into it), so the set doesn't only grow. Then the full 64-bit build + difftest.

Held slots accumulate in mirror/rules/generated/HELD.txt: each is either an
analyzer false positive (not a pointer) or a pointer compared by sign (a tag
test), and both need a hand decision rather than an automatic widen.

  python3 tools/mirror/regen.py            # fixpoint + build + difftest
  python3 tools/mirror/regen.py --max 4
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
HELD = REPO / "mirror" / "rules" / "generated" / "HELD.txt"
PY = sys.executable


def run(*args, check=True):
    print("$", " ".join(str(a) for a in args), file=sys.stderr)
    r = subprocess.run([PY, *map(str, args)], cwd=REPO)
    if check and r.returncode != 0:
        raise SystemExit(f"step failed: {args}")
    return r.returncode


def read_holds(p: Path) -> set[str]:
    if not p.exists():
        return set()
    return {l.strip() for l in p.read_text().splitlines() if l.strip() and not l.startswith("#")}


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--max", type=int, default=5)
    args = ap.parse_args(argv)

    holds = read_holds(HELD)
    tmp_holds = REPO / "mirror" / "holds.in"          # mirror/* is gitignored scratch
    for it in range(1, args.max + 1):
        tmp_holds.write_text("\n".join(sorted(holds)) + "\n")
        run(HERE / "infer_ptr_ints.py", "--tus", "build", "--hold", tmp_holds)
        holds = read_holds(HELD)        # the analyzer released any stale holds
        run(HERE / "build_mirror.py", "--gen-only")
        new_file = REPO / "mirror" / "holds.new"
        run(HERE / "check_signedness.py", f"--emit-holds={new_file}", check=False)
        new = read_holds(new_file) - holds
        print(f"[regen] iteration {it}: {len(holds)} held, {len(new)} new", file=sys.stderr)
        if not new:
            break
        holds |= new
    else:
        print("[regen] WARNING: hold set still growing at --max", file=sys.stderr)

    rc = run(HERE / "build_mirror.py", check=False)
    rc |= run(HERE / "check_signedness.py", check=False)
    rc |= run(HERE / "check_outparams.py", check=False)
    rc |= run(HERE / "difftest" / "run.py", check=False)
    return rc


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
