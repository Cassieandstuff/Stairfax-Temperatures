#!/usr/bin/env python3
"""Mirror build target — generate the portable mirror and compile it 64-bit.

First, every decomp header a rule names (file="decomp/include/...") is run through
the same rewriter into mirror/include/<path>. mirror/include comes FIRST on the
include path, so a transformed header (e.g. a widened prototype) shadows the
decomp original for every TU — otherwise a retyped definition would conflict
with its untransformed declaration.

Then, for every TU in tools/mirror/build_manifest.txt this:
  1. runs the P2 rewriter (passes.apply_pointer_rules) on the decomp source,
  2. writes the transformed TU to mirror/src/<path>,
  3. compiles it 64-bit against mirror/include + the decomp include tree.

A listed TU that fails to compile fails the build (exit 1). This is the seed of
the native-64-bit ship target from ADR 0001: it grows one TU at a time as more of
the engine becomes mirror-compilable.

  python3 tools/mirror/build_mirror.py            # generate + compile all
  python3 tools/mirror/build_mirror.py --gen-only # just regenerate mirror/src
"""
from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
sys.path.insert(0, str(HERE))
import passes  # noqa: E402

DECOMP = REPO / "decomp"
MIRROR = REPO / "mirror"
MANIFEST = HERE / "build_manifest.txt"
CC = "clang"
# mirror/include first: transformed headers must shadow their decomp originals.
# mirror/include is a COMPLETE generated header tree (see gen_headers): the decomp's
# decomp/include, overlaid with the port's SDK shadow headers (repo-root include/,
# the same shadows the 32-bit build puts first), with mirror/rules applied. It has
# to be complete: a quoted #include searches the INCLUDER's directory first, so
# with a partial overlay a decomp header next to the original would silently pick
# the untransformed copy (that is how u32 stayed 8 bytes). TARGET_PC is NOT
# defined: the decomp's TARGET_PC path is MSVC-specific (it breaks bool off
# Windows).
INCLUDES = ["-Imirror/include", "-Imirror/runtime", "-Idecomp", "-Idecomp/src"]
# -fdeclspec: accept MWCC __declspec(weak) / __declspec(section ...).
# Implicit function declarations are allowed (MWCC accepted them) but every one is
# checked by implicit_decl_hazards(): an implicitly declared callee returns int, so
# one that really returns a pointer would truncate on 64-bit and fails the build.
# -include mirror_prelude.h (mirror/runtime): stdint + runtime accessor decls,
# injected on the command line so generated files stay LINE-PRESERVING (line N of a
# mirror file == line N of its decomp source; rules and diagnostics line up).
CFLAGS = ["-m64", "-c", "-O1", "-include", "mirror_prelude.h", "-fdeclspec", "-Wno-everything",
          "-Wimplicit-function-declaration", "-Wno-error=implicit-function-declaration"]


def read_manifest() -> list[str]:
    out = []
    for ln in MANIFEST.read_text().splitlines():
        ln = ln.strip()
        if ln and not ln.startswith("#"):
            out.append(ln)
    return out


def gen_one(rel: str, rules: dict) -> tuple[Path, int, int]:
    """Emit the mirror TU. Returns (mirror_path, crit_before, crit_after)."""
    src = DECOMP / rel
    text = src.read_text(errors="replace")
    tu_rel = f"decomp/{rel}"
    before = sum(1 for f in passes.scan_text_pointer_width(text, tu_rel)
                 if f.severity == "critical")
    new_text, applied, _ = passes.apply_pointer_rules(text, rules, tu_rel)

    after = sum(1 for f in passes.scan_text_pointer_width(new_text, tu_rel)
                if f.severity == "critical")
    dst = MIRROR / "src" / rel
    dst.parent.mkdir(parents=True, exist_ok=True)
    # line-preserving (see gen_headers)
    dst.write_text(f"/* GENERATED from decomp/{rel} by tools/mirror/build_mirror.py; "
                   f"DO NOT EDIT */ " + new_text)
    return dst, before, after


def gen_headers(rules: dict) -> list[str]:
    """Build mirror/include: decomp/include + port shadow overlay + rule rewrites.
    Returns the transformed headers (include-relative)."""
    dst_root = MIRROR / "include"
    shutil.rmtree(dst_root, ignore_errors=True)
    shutil.copytree(DECOMP / "include", dst_root)
    shutil.copytree(REPO / "include", dst_root, dirs_exist_ok=True)   # port shadows win
    out = []
    for f in sorted(passes.rule_files(rules)):
        if not f.endswith(".h"):
            continue
        if not f.startswith("decomp/include/"):
            raise SystemExit(f"error: header rule target {f!r} is not under decomp/include/")
        rel = f[len("decomp/include/"):]
        dst = dst_root / rel
        if not dst.exists():
            raise SystemExit(f"error: header rule target {f!r} does not exist")
        if (REPO / "include" / rel).exists():
            raise SystemExit(f"error: header rule target {f!r} is shadowed by the port's "
                             f"include/{rel}; a rule on the decomp copy would be dead")
        text = dst.read_text(errors="replace")
        new_text, applied, unmatched = passes.apply_pointer_rules(text, rules, f)
        if unmatched:
            raise SystemExit(f"error: header rules for {f} did not match: {unmatched}")
        # Line-preserving: provenance comment shares line 1; stdint.h comes from
        # -include on the command line (CFLAGS).
        dst.write_text(f"/* GENERATED from {f} by tools/mirror/build_mirror.py; DO NOT EDIT */ "
                       + new_text)
        out.append(f"{rel} ({len(applied)} rule(s))")
    return out


_RE_IMPLICIT = re.compile(r"call to undeclared (?:library )?function '(\w+)'")
_PTR_RET_CACHE: dict[str, list[str]] = {}


def _pointer_returning_decls(name: str) -> list[str]:
    """Declarations anywhere in the decomp/port headers+sources that give `name` a
    pointer return type, e.g. `void* name(` / `GameObject *name(`."""
    if name not in _PTR_RET_CACHE:
        r = subprocess.run(["grep", "-rnE", "--include=*.h", "--include=*.c",
                            rf"^[A-Za-z_][\w ]*\*+\s*{name}\s*\(",
                            "decomp/include", "decomp/src", "include"],
                           capture_output=True, text=True, cwd=REPO)
        _PTR_RET_CACHE[name] = [l for l in r.stdout.splitlines() if l][:3]
    return _PTR_RET_CACHE[name]


def implicit_decl_hazards(stderr: str) -> tuple[list[str], list[str]]:
    """Split implicitly-declared callees into (pointer-returning hazards, benign)."""
    hazards, benign = [], []
    for name in sorted(set(_RE_IMPLICIT.findall(stderr))):
        decls = _pointer_returning_decls(name)
        (hazards if decls else benign).append(
            f"{name}  <- {decls[0]}" if decls else name)
    return hazards, benign


def compile_one(mirror_path: Path) -> tuple[bool, str]:
    r = subprocess.run([CC, *CFLAGS, *INCLUDES, str(mirror_path),
                        "-o", str(mirror_path.with_suffix(".o"))],
                       capture_output=True, text=True, cwd=REPO)
    hazards, _ = implicit_decl_hazards(r.stderr)
    if r.returncode == 0 and hazards:
        return False, ("implicitly declared function(s) that return a pointer "
                       "(would truncate to int on 64-bit):\n  " + "\n  ".join(hazards))
    return r.returncode == 0, r.stderr


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Generate + compile the mirror target")
    ap.add_argument("--gen-only", action="store_true", help="regenerate mirror/src, skip compile")
    args = ap.parse_args(argv)

    tus = read_manifest()
    rules = passes.load_pointer_rules(MIRROR / "rules")
    # The generator owns mirror/src exclusively, so it always equals the manifest
    # (no strays from ad-hoc --emit runs leak into the CMake glob).
    shutil.rmtree(MIRROR / "src", ignore_errors=True)
    headers = gen_headers(rules)
    print(f"mirror target: {len(tus)} TU(s), {len(headers)} transformed header(s)")
    for h in headers:
        print(f"  hdr  {h}")

    n_fail = 0
    tot_before = tot_after = 0
    for rel in tus:
        mpath, cb, ca = gen_one(rel, rules)
        tot_before += cb
        tot_after += ca
        if args.gen_only:
            print(f"  gen  {rel}  crit {cb}->{ca}")
            continue
        ok, err = compile_one(mpath)
        status = "ok " if ok else "FAIL"
        note = "" if cb == ca == 0 else f"  crit {cb}->{ca}"
        print(f"  [{status}] {rel}{note}")
        if not ok:
            n_fail += 1
            first = err.strip().splitlines()[:4]
            for l in first:
                print(f"        {l}")

    verb = "generated" if args.gen_only else "compiled"
    print(f"\n{len(tus) - n_fail}/{len(tus)} {verb}; "
          f"pointer-width criticals {tot_before} -> {tot_after}")
    return 1 if n_fail else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
