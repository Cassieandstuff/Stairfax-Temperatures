#!/usr/bin/env python3
"""Behavioral diff harness for the P2 rewriter.

For each case under tools/mirror/difftest/cases/<name>/:
  - `case.c`      : decomp-style source with pointer-width hazards + a
                    `run_case()` and a `main()` behind DIFFTEST_MAIN.
  - `rules.toml`  : the P2 rules to transform it.

The harness:
  1. builds the ORACLE from case.c unchanged, with the heap confined to the low
     4 GB (DIFFTEST_ORACLE_LOWMEM) so its pointer-in-int truncation is lossless —
     the ground truth, behaving as the real 32-bit build would;
  2. produces the MIRROR by running the actual P2 rewriter (passes.apply_pointer_
     rules) on case.c, and builds it with a normal high-address heap;
  3. runs both and compares stdout + exit code. PASS iff identical.

It also runs a NEGATIVE CONTROL: the oracle source built WITHOUT the low-memory
confinement. If that diverges or crashes, the hazard the rewriter fixes is real
(so the test is meaningful, not vacuous). A control that happens to pass — e.g.
the OS handed out a low address anyway — is reported as inconclusive, not failed.

Exit code 0 iff every case passes.
"""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
MIRROR_TOOLS = HERE.parent
REPO = MIRROR_TOOLS.parent.parent
sys.path.insert(0, str(MIRROR_TOOLS))
import passes  # noqa: E402

CC = "clang"
CFLAGS = ["-m64", "-O1", "-w", "-DDIFFTEST_MAIN", f"-I{HERE}"]
CASES = HERE / "cases"


def _run(cmd: list[str], **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def _compile(src: Path, out: Path, extra: list[str]) -> tuple[bool, str]:
    r = _run([CC, *CFLAGS, *extra, str(src), "-o", str(out)])
    return r.returncode == 0, r.stderr


def _exec(bin_: Path) -> tuple[int, str]:
    r = _run([str(bin_)])
    return r.returncode, r.stdout


def run_case(case: Path, workdir: Path) -> bool:
    name = case.name
    src = case / "case.c"
    rules_path = case / "rules.toml"
    if not src.exists() or not rules_path.exists():
        print(f"[{name}] SKIP — missing case.c or rules.toml")
        return False

    rules = passes.load_pointer_rules(case)  # reads *.toml in the case dir
    rel = (src.relative_to(REPO)).as_posix()
    original = src.read_text()

    # --- mirror = P2 rewriter output on the case source ---
    mirror_text, applied, unmatched = passes.apply_pointer_rules(original, rules, rel)
    if applied and "uintptr_t" in mirror_text and "stdint.h" not in mirror_text:
        mirror_text = "#include <stdint.h>\n" + mirror_text
    if unmatched:
        print(f"[{name}] rule(s) did not match — the case/rules are out of sync:")
        for u in unmatched:
            print(f"    - {u}")
        return False

    wd = workdir / name
    wd.mkdir(parents=True, exist_ok=True)
    oracle_src = wd / "oracle.c"
    mirror_src = wd / "mirror.c"
    oracle_src.write_text(original)
    mirror_src.write_text(mirror_text)

    # --- build ---
    ok, err = _compile(oracle_src, wd / "oracle", ["-DDIFFTEST_ORACLE_LOWMEM"])
    if not ok:
        print(f"[{name}] FAIL — oracle did not compile:\n{err}")
        return False
    ok, err = _compile(mirror_src, wd / "mirror", [])
    if not ok:
        print(f"[{name}] FAIL — mirror did not compile:\n{err}")
        return False

    # --- run + diff ---
    orc, oout = _exec(wd / "oracle")
    mrc, mout = _exec(wd / "mirror")
    passed = (orc == mrc == 0) and (oout == mout)

    # --- negative control: oracle without the low-mem crutch ---
    ctrl_note = ""
    ok, _ = _compile(oracle_src, wd / "oracle_hi", [])
    if ok:
        crc, cout = _exec(wd / "oracle_hi")
        if crc != 0 or cout != oout:
            ctrl_note = "  (hazard confirmed: untransformed code breaks on a high heap)"
        else:
            ctrl_note = "  (control inconclusive: OS gave a low address anyway)"

    tag = "PASS" if passed else "FAIL"
    print(f"[{name}] {tag}  rules_applied={len(applied)}  "
          f"oracle={oout.strip()!r}  mirror={mout.strip()!r}{ctrl_note}")
    if not passed:
        print(f"    oracle rc={orc} mirror rc={mrc}")
    return passed


def main(argv: list[str]) -> int:
    only = set(argv)
    cases = sorted(p for p in CASES.iterdir() if p.is_dir()) if CASES.exists() else []
    if only:
        cases = [c for c in cases if c.name in only]
    if not cases:
        print("no cases found")
        return 1
    workdir = REPO / "mirror" / "difftest-build"
    results = [run_case(c, workdir) for c in cases]
    n_ok = sum(results)
    print(f"\n{n_ok}/{len(results)} case(s) passed")
    return 0 if n_ok == len(results) else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
