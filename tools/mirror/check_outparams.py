#!/usr/bin/env python3
"""Check that pointer-width out-parameters get pointer-width storage.

Some mirror APIs return pointers through integer out-parameters that the rules
widened to uintptr_t* (ObjMsg_Pop / ObjMsg_Peek sender and param,
newshadows_get*Texture). A caller that still passes the address of 4-byte
storage compiles (at most a pointer-type warning) but the callee writes 8 bytes
into it: silent memory corruption. This compiles nothing. It parses every built
mirror TU with libclang and checks, at each call, that those arguments point at
8-byte objects (or are NULL).

  python3 tools/mirror/check_outparams.py      # exit 1 on any violation
"""
from __future__ import annotations

import concurrent.futures as cf
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import build_mirror as bm  # noqa: E402
import clang.cindex as ci  # noqa: E402

K, T = ci.CursorKind, ci.TypeKind
ARGS = ["-xc", "-std=gnu99", "-fdeclspec", "-fms-extensions", "-w",
        "-include", "mirror_prelude.h", *bm.INCLUDES]
# function -> argument indices that are pointer-width out-parameters
CHECKED = {
    "ObjMsg_Pop": (2, 3), "ObjMsg_Peek": (2, 3),
    "newshadows_getCausticTexture": (0,), "newshadows_getRampTexture": (0,),
    "newshadows_getDiskTexture": (0,), "newshadows_getReflectionDepthTexture": (0,),
}


def _strip(e):
    while e is not None and e.kind in (K.PAREN_EXPR, K.UNEXPOSED_EXPR, K.CSTYLE_CAST_EXPR):
        k = list(e.get_children())
        e = k[-1] if k else None
    return e


def _target_size(arg) -> int | None:
    """Size of the object the argument points at (before any cast), or None for
    a null constant."""
    e = _strip(arg)
    if e is None or e.kind in (K.INTEGER_LITERAL, K.GNU_NULL_EXPR):
        return None
    t = e.type.get_canonical()
    if t.kind == T.POINTER:
        return t.get_pointee().get_canonical().get_size()
    if t.kind in (T.CONSTANTARRAY, T.INCOMPLETEARRAY):
        return t.element_type.get_canonical().get_size()
    return -1


def check(rel: str) -> list[str]:
    path = bm.MIRROR / "src" / rel
    text = path.read_text(errors="replace")
    if not any(f in text for f in CHECKED):
        return []
    tu = ci.Index.create().parse(str(path), args=ARGS)
    bad = []
    for c in tu.cursor.walk_preorder():
        if c.kind != K.CALL_EXPR or c.spelling not in CHECKED:
            continue
        if not c.location.file or Path(c.location.file.name).resolve() != path.resolve():
            continue
        args = list(c.get_arguments())
        for i in CHECKED[c.spelling]:
            if i >= len(args):
                continue
            sz = _target_size(args[i])
            if sz is not None and sz != 8:
                src = " ".join(t.spelling for t in args[i].get_tokens())
                bad.append(f"{rel}:{c.location.line}: {c.spelling} arg {i} `{src}` "
                           f"points at {sz}-byte storage (callee writes 8)")
    return bad


def main() -> int:
    bad = []
    with cf.ProcessPoolExecutor() as ex:
        for res in ex.map(check, bm.read_manifest()):
            bad += res
    for b in bad:
        print(b)
    print(f"{len(bad)} out-parameter width violation(s)", file=sys.stderr)
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
