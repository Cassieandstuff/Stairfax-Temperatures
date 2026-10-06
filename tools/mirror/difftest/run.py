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
import tomllib
from pathlib import Path

HERE = Path(__file__).resolve().parent
MIRROR_TOOLS = HERE.parent
REPO = MIRROR_TOOLS.parent.parent
sys.path.insert(0, str(MIRROR_TOOLS))
import passes  # noqa: E402

CC = "clang"
# -fno-strict-aliasing: the decomp type-puns (MWCC semantics); keep UB-driven
# optimizer differences between oracle and mirror from posing as a behavior diff.
CFLAGS = ["-m64", "-O1", "-w", "-fno-strict-aliasing", "-DDIFFTEST_MAIN", f"-I{HERE}"]
LDFLAGS = ["-lm"]
# Mirror builds get the same force-included prelude as the real mirror build
# (stdint + runtime accessor decls); the oracle is untransformed decomp code.
MIRROR_PRELUDE = ["-DDIFFTEST_MIRROR", "-include", "mirror_prelude.h", f"-I{REPO / 'mirror' / 'runtime'}"]
CASES = HERE / "cases"


def _run(cmd: list[str], **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def _compile(src: Path, out: Path, extra: list[str],
             extra_srcs: list[str] | None = None) -> tuple[bool, str]:
    srcs = [str(src)] + [str(s) for s in (extra_srcs or [])]
    r = _run([CC, *CFLAGS, *extra, *srcs, "-o", str(out), *LDFLAGS])
    return r.returncode == 0, r.stderr


def _exec(bin_: Path) -> tuple[int, str]:
    r = _run([str(bin_)])
    return r.returncode, r.stdout


def _extract_ranges(text: str, ranges: list) -> str:
    lines = text.splitlines(keepends=True)
    out = []
    for r in ranges:
        a, b = int(r[0]), int(r[1])
        out.append("".join(lines[a - 1:b]))
    return "\n".join(out)


def run_extract_case(case: Path, workdir: Path) -> bool:
    """Real-TU case: extract line ranges from an actual decomp source and from the
    rewriter's transform of that same source, compile each against the case shim +
    driver, and diff."""
    name = case.name
    spec = tomllib.loads((case / "extract.toml").read_text())
    rules_dir = REPO / spec.get("rules", "mirror/rules")
    shim = case / "shim.h"
    driver = case / "driver.c"
    # one source (`source` + `ranges`) or several (`[[part]]` tables, concatenated
    # in order — e.g. a callee from one TU followed by its caller from another)
    parts = spec.get("part") or [{"source": spec["source"], "ranges": spec["ranges"]}]
    rules = passes.load_pointer_rules(rules_dir)
    oracle_snips, mirror_snips, applied = [], [], []
    for part in parts:
        source = REPO / part["source"]
        if not source.exists():
            print(f"[{name}] FAIL — source not found: {part['source']}")
            return False
        original = source.read_text(errors="replace")
        mirror_full, app, _ = passes.apply_pointer_rules(original, rules, part["source"])
        o, m = _extract_ranges(original, part["ranges"]), _extract_ranges(mirror_full, part["ranges"])
        # may_be_unchanged: supporting definitions the rules don't touch.
        # mirror_only: code the oracle can't compile on a 64-bit host (e.g. a
        # static initializer truncating an address); the oracle's shim/driver
        # supplies the equivalent 32-bit data instead.
        if o == m and not part.get("may_be_unchanged"):
            print(f"[{name}] FAIL — extracted mirror == oracle for {part['source']}; rules "
                  "did not touch the extracted ranges (nothing to validate)")
            return False
        if not part.get("mirror_only"):
            oracle_snips.append(o)
        mirror_snips.append(m); applied += app
    oracle_snip, mirror_snip = "\n".join(oracle_snips), "\n".join(mirror_snips)
    src_label = " + ".join(p["source"] for p in parts)

    wd = workdir / name
    for build, snip, extra in (("oracle", oracle_snip, ["-DDIFFTEST_ORACLE_LOWMEM"]),
                               ("mirror", mirror_snip, [])):
        bd = wd / build
        bd.mkdir(parents=True, exist_ok=True)
        (bd / "snippet.c").write_text(snip + "\n")
        (bd / "shim.h").write_text(shim.read_text())
        (bd / "driver.c").write_text(driver.read_text())

    # oracle_cflags: extra flags for the oracle builds only (e.g. -fms-extensions
    # for `T* __ptr32 __uptr`, a 4-byte pointer that gives a struct holding one its
    # 32-bit layout on this 64-bit host)
    o_flags = list(spec.get("oracle_cflags", []))
    # oracle_include / oracle_preinclude: compile the oracle against a real header
    # tree too (e.g. the decomp's own object.h). Pre-including mirror/include's
    # dolphin/types.h gives that tree the 4-byte s32/u32 of the 32-bit build;
    # the raw decomp typedefs make them `long`, 8 bytes on this host.
    o_flags += [a for i in spec.get("oracle_preinclude", []) for a in ("-include", str(REPO / i))]
    o_flags += [a for i in spec.get("oracle_include", []) for a in ("-idirafter", str(REPO / i))]
    ok, err = _compile(wd / "oracle" / "driver.c", wd / "oracle_bin",
                       ["-DDIFFTEST_ORACLE_LOWMEM", f"-I{wd/'oracle'}", *o_flags])
    if not ok:
        print(f"[{name}] FAIL — oracle did not compile:\n{err}")
        return False
    # optional: the mirror side compiled against the real mirror header tree and
    # linked with runtime sources (cases that exercise mirror/runtime code)
    # -idirafter: the decomp's own libc headers (stdio.h, ...) live in the mirror
    # tree; searched after the system dirs, the driver still gets the real libc
    m_inc = [a for i in spec.get("mirror_include", []) for a in ("-idirafter", str(REPO / i))]
    m_link = [REPO / l for l in spec.get("mirror_link", [])]
    ok, err = _compile(wd / "mirror" / "driver.c", wd / "mirror_bin",
                       [*MIRROR_PRELUDE, f"-I{wd/'mirror'}", *m_inc], m_link)
    if not ok:
        print(f"[{name}] FAIL — mirror did not compile:\n{err}")
        return False

    orc, oout = _exec(wd / "oracle_bin")
    mrc, mout = _exec(wd / "mirror_bin")
    passed = (orc == mrc == 0) and (oout == mout)

    # negative control: oracle snippet built without the low-mem crutch
    ctrl_note = ""
    ok, _ = _compile(wd / "oracle" / "driver.c", wd / "oracle_hi_bin", [f"-I{wd/'oracle'}", *o_flags])
    if ok:
        crc, cout = _exec(wd / "oracle_hi_bin")
        ctrl_note = ("  (hazard confirmed: untransformed code breaks on a high heap)"
                     if (crc != 0 or cout != oout)
                     else "  (control inconclusive: OS gave a low address anyway)")

    tag = "PASS" if passed else "FAIL"
    print(f"[{name}] {tag}  real TU {src_label}  rules_applied={len(applied)}  "
          f"oracle={oout.strip()!r}  mirror={mout.strip()!r}{ctrl_note}")
    if not passed:
        print(f"    oracle rc={orc} mirror rc={mrc}")
    return passed


def run_case(case: Path, workdir: Path) -> bool:
    if (case / "extract.toml").exists():
        return run_extract_case(case, workdir)
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

    # optional case.toml: extra link sources / include dirs / oracle-only defines
    cfg = {}
    cfg_path = case / "case.toml"
    if cfg_path.exists():
        cfg = tomllib.loads(cfg_path.read_text())
    links = [REPO / p for p in cfg.get("link", [])]
    incs = [f"-I{REPO / i}" for i in cfg.get("include", [])]
    oracle_defs = [f"-D{d}" for d in cfg.get("oracle_defs", [])]

    wd = workdir / name
    wd.mkdir(parents=True, exist_ok=True)
    oracle_src = wd / "oracle.c"
    mirror_src = wd / "mirror.c"
    oracle_src.write_text(original)
    mirror_src.write_text(mirror_text)

    # --- build ---
    ok, err = _compile(oracle_src, wd / "oracle",
                       ["-DDIFFTEST_ORACLE_LOWMEM", *oracle_defs, *incs], links)
    if not ok:
        print(f"[{name}] FAIL — oracle did not compile:\n{err}")
        return False
    ok, err = _compile(mirror_src, wd / "mirror", [*MIRROR_PRELUDE, *incs], links)
    if not ok:
        print(f"[{name}] FAIL — mirror did not compile:\n{err}")
        return False

    # --- run + diff ---
    orc, oout = _exec(wd / "oracle")
    mrc, mout = _exec(wd / "mirror")
    passed = (orc == mrc == 0) and (oout == mout)

    # --- negative control: oracle without the low-mem / MEM1-map crutch ---
    ctrl_note = ""
    ok, _ = _compile(oracle_src, wd / "oracle_hi", incs, links)
    if ok:
        crc, cout = _exec(wd / "oracle_hi")
        if crc != 0 or cout != oout:
            ctrl_note = "  (hazard confirmed: untransformed code breaks without the guest map)"
        else:
            ctrl_note = "  (control inconclusive: ran without the crutch anyway)"

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
