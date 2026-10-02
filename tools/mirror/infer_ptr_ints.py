#!/usr/bin/env python3
"""Whole-program inference of integers that carry pointers (P2, type-aware).

The line scanner (passes.py) can only see hazards on one line. The `int obj`
convention moves pointers through int variables, params, returns and globals
across functions and files, so converting it needs real dataflow. This tool parses
every TU with libclang (same flags as the mirror build) and computes which
integer-typed *slots* may hold a pointer value:

  slot  = a variable (global / local / param), a function's return value, or a
          struct field, identified by libclang USR (stable across TUs).
  seed  = a pointer -> integer cast `(int)ptr`: its value is a pointer.
  flow  = assignment / initialization / compound assignment, call argument ->
          callee param (by callee USR + index), return -> function, and the
          pointer-preserving arithmetic `p + n`, `p - n`, `p & mask`, `p | bits`,
          `c ? p : q`. NOT `p - q` (both pointers: a difference), `* / % >> <<`, or
          comparisons.

Propagation is forward-only (a slot needs widening iff a pointer can reach it), so
an int that's merely added to a pointer (an offset) is never widened.

Output: rule TOML per TU under mirror/rules/generated/ (retype for every carrying
int declaration incl. prototypes and headers, widen_cast for every narrowing cast
of a carrying value, ret for functions returning one), skipping sites already
covered by hand-written rules. Struct FIELDS that carry pointers are only
REPORTED: field layout is often fixed by disc formats, which is a design decision
(docs/mirror/pointer-slot-survey.md), not a mechanical rewrite.

  python3 tools/mirror/infer_ptr_ints.py --tus build      # mirror build set
  python3 tools/mirror/infer_ptr_ints.py --tus all        # + compilable dlls
  python3 tools/mirror/infer_ptr_ints.py --tus build --report-only
"""
from __future__ import annotations

import argparse
import json
import sys
from collections import defaultdict
from pathlib import Path

import clang.cindex as ci
from clang.cindex import CursorKind as K, TypeKind as T

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
sys.path.insert(0, str(HERE))
import build_mirror as bm  # noqa: E402
import passes  # noqa: E402

NARROW_INT = {T.INT, T.UINT, T.LONG, T.ULONG}          # 32-bit on the GameCube
PTRISH = {T.POINTER, T.INCOMPLETEARRAY, T.CONSTANTARRAY, T.FUNCTIONPROTO,
          T.FUNCTIONNOPROTO}
# Parse exactly as the mirror compiles: transformed headers first (u32/s32 pinned
# to 32 bits by mirror/rules/types.toml; run build_mirror.py --gen-only first).
INC = list(bm.INCLUDES)
ARGS = ["-m64", "-include", "mirror_prelude.h", "-fdeclspec", "-Wno-everything", "-x", "c"] + INC


def canon(t):
    return t.get_canonical()


def is_narrow_int(t) -> bool:
    c = canon(t)
    if c.kind in (T.CONSTANTARRAY, T.INCOMPLETEARRAY):     # u32 tbl[N]: element slot
        c = c.element_type.get_canonical()
    return c.kind in NARROW_INT and c.get_size() == 4


def is_ptrish(t) -> bool:
    return canon(t).kind in PTRISH


def rel(path: str | None) -> str | None:
    """Repo-relative path of a source location. We parse through the generated
    mirror/include tree, so header locations come back as mirror/include/<h>; map
    them to the decomp header rules target (decomp/include/<h>). A header that comes
    from the port's shadow overlay maps to include/<h>: port-owned code, which rules
    don't rewrite (reported instead)."""
    if not path:
        return None
    try:
        r = Path(path).resolve().relative_to(REPO).as_posix()
    except ValueError:
        return None
    if r.startswith("mirror/include/"):
        h = r[len("mirror/include/"):]
        if (REPO / "include" / h).exists():
            return f"include/{h}"
        return f"decomp/include/{h}"
    if r.startswith("mirror/src/"):
        return "decomp/" + r[len("mirror/src/"):]
    return r


class Graph:
    def __init__(self):
        self.edges = defaultdict(set)     # src slot -> {dst slots}
        self.seeds = set()                # slots that directly receive a pointer
        self.decls = defaultdict(set)     # slot -> {(file, line, name, typespell, kind)}
        self.casts = []                   # (file, line, col, from_spelling, operand_slots, is_ptr_to_int)
        self.fields = {}                  # field slot -> (struct.field, file, line)
        self.subnodes = {}                # SUB node -> (right-operand slots, right is SEED)
        self.macro_skips = 0

    def flow(self, src, dst):
        if src and dst and src != dst:
            self.edges[src].add(dst)


def slot_of_decl(c) -> str | None:
    if c.kind == K.PARM_DECL:
        fn = c.semantic_parent
        if fn is not None and fn.kind == K.FUNCTION_DECL:
            idx = [a.get_usr() for a in fn.get_arguments()].index(c.get_usr()) \
                if c.get_usr() in [a.get_usr() for a in fn.get_arguments()] else None
            if idx is not None:
                return f"param:{fn.get_usr()}#{idx}"
        return f"var:{c.get_usr()}"
    if c.kind == K.VAR_DECL:
        return f"var:{c.get_usr()}"
    if c.kind == K.FIELD_DECL:
        return f"field:{c.get_usr()}"
    return None


def in_macro(c) -> bool:
    """True if the cursor's text comes from a macro expansion (rules target source
    lines, so macro-generated code can't be rewritten at the expansion site)."""
    ext = c.extent
    return ext.start.file is None or (ext.start.line != c.location.line and
                                      c.kind != K.FUNCTION_DECL and False)


class Analyzer:
    def __init__(self):
        self.g = Graph()
        self.index = ci.Index.create()
        self.subs = {}                    # id -> (left cursor, right cursor) of `a - b`
        self.tu_tag = "0"                 # makes SUB node names unique across worker TUs

    # --- expression value sources -------------------------------------------
    def sources(self, e) -> set[str]:
        """Slots / markers whose value can flow out of expression e.
        'SEED' marks a pointer value converted to an integer."""
        k = e.kind
        if k in (K.PAREN_EXPR, K.UNEXPOSED_EXPR):
            kids = list(e.get_children())
            out = set()
            for ch in kids:
                if is_ptrish(ch.type) and is_narrow_int(e.type):
                    out.add("SEED")               # implicit pointer -> int
                else:
                    out |= self.sources(ch)
            return out
        if k == K.DECL_REF_EXPR:
            d = e.referenced
            if d is not None and d.kind in (K.VAR_DECL, K.PARM_DECL) and is_narrow_int(d.type):
                s = slot_of_decl(d)
                return {s} if s else set()
            return set()
        if k == K.MEMBER_REF_EXPR:
            d = e.referenced
            if d is not None and d.kind == K.FIELD_DECL and is_narrow_int(d.type):
                return {f"field:{d.get_usr()}"}
            return set()
        if k == K.CALL_EXPR:
            d = e.referenced
            if d is not None and d.kind == K.FUNCTION_DECL and is_narrow_int(d.result_type):
                return {f"ret:{d.get_usr()}"}
            return set()
        if k == K.CSTYLE_CAST_EXPR:
            kids = list(e.get_children())
            inner = kids[-1] if kids else None
            if inner is None or not is_narrow_int(e.type):
                return set()
            if is_ptrish(inner.type):
                return {"SEED"}
            return self.sources(inner)
        if k == K.BINARY_OPERATOR:
            kids = list(e.get_children())
            if len(kids) != 2:
                return set()
            op = self.binop(e)
            a, b = self.sources(kids[0]), self.sources(kids[1])
            if op in ("+", "&", "|", "^"):
                return a | b
            if op == "-":
                # p - n stays a pointer; p - q is a difference. Decided at solve
                # time: record as a guarded flow.
                if a and b:
                    self.subs[id(e)] = (kids[0], kids[1])
                    return {f"SUB:{self.tu_tag}:{id(e)}"}
                return a
            if op == ",":
                return b
            return set()
        if k == K.CONDITIONAL_OPERATOR:
            kids = list(e.get_children())
            out = set()
            for ch in kids[1:]:
                out |= self.sources(ch)
            return out
        if k == K.UNARY_OPERATOR:
            kids = list(e.get_children())
            tok = [t.spelling for t in e.get_tokens()][:1]
            if kids and tok and tok[0] in ("+", "~"):
                return self.sources(kids[0])
            return set()
        return set()

    def binop(self, e) -> str:
        try:
            return {"BinaryOperator.Add": "+", "BinaryOperator.Sub": "-",
                    "BinaryOperator.And": "&", "BinaryOperator.Or": "|",
                    "BinaryOperator.Xor": "^", "BinaryOperator.Comma": ","}.get(
                str(e.binary_operator), str(e.binary_operator))
        except Exception:
            kids = list(e.get_children())
            toks = [t.spelling for t in e.get_tokens()]
            n = len(list(kids[0].get_tokens())) if kids else 0
            return toks[n] if n < len(toks) else "?"

    # --- statement / declaration walk ---------------------------------------
    def sink(self, dst_slot, expr):
        for s in self.sources(expr):
            if s == "SEED":
                self.g.seeds.add(dst_slot)
            elif s.startswith("SUB:"):
                # `p - n` stays a pointer, `p - q` is a size. Route through a SUB node
                # the solver disables once its right operand is known to carry.
                node = s
                left, right = self.subs[int(s.rsplit(":", 1)[1])]
                rsrc = self.sources(right)
                self.g.subnodes[node] = ({r for r in rsrc if not r.startswith(("SEED", "SUB:"))},
                                         "SEED" in rsrc)
                for s2 in self.sources(left):
                    if s2 == "SEED":
                        self.g.seeds.add(node)
                    elif not s2.startswith("SUB:"):
                        self.g.flow(s2, node)
                self.g.flow(node, dst_slot)
            else:
                self.g.flow(s, dst_slot)

    def visit(self, c, fn=None):
        f = rel(c.location.file.name if c.location.file else None)
        k = c.kind
        if k == K.FUNCTION_DECL:
            fn = c
            if f and is_narrow_int(c.result_type):
                self.g.decls[f"ret:{c.get_usr()}"].add((f, c.location.line, c.spelling, "", "ret"))
            for i, a in enumerate(c.get_arguments()):
                if is_narrow_int(a.type):
                    af = rel(a.location.file.name if a.location.file else None)
                    if af:
                        self.g.decls[f"param:{c.get_usr()}#{i}"].add(
                            (af, a.location.line, a.spelling, a.type.spelling, "param"))
                    self.g.flow(f"param:{c.get_usr()}#{i}", f"var:{a.get_usr()}")
                    self.g.flow(f"var:{a.get_usr()}", f"param:{c.get_usr()}#{i}")
        elif k == K.VAR_DECL and is_narrow_int(c.type):
            s = slot_of_decl(c)
            if f:
                self.g.decls[s].add((f, c.location.line, c.spelling, c.type.spelling, "var"))
            kids = [ch for ch in c.get_children() if ch.kind not in (K.TYPE_REF,)]
            if kids:
                init = kids[-1]
                if init.kind == K.INIT_LIST_EXPR:      # u32 tbl[] = {(u32)p, ...}
                    for el in init.get_children():
                        self.sink(s, el)
                else:
                    self.sink(s, init)
        elif k == K.FIELD_DECL and is_narrow_int(c.type):
            par = c.semantic_parent
            self.g.fields[f"field:{c.get_usr()}"] = (
                f"{par.spelling or par.type.spelling}.{c.spelling}", f, c.location.line)
        elif k == K.BINARY_OPERATOR and self.binop(c) in ("=",) or \
                (k == K.BINARY_OPERATOR and str(getattr(c, "binary_operator", "")) == "BinaryOperator.Assign"):
            kids = list(c.get_children())
            if len(kids) == 2:
                for d in self.sources(kids[0]):
                    if not d.startswith(("SEED", "SUB:")):
                        self.sink(d, kids[1])
        elif k == K.COMPOUND_ASSIGNMENT_OPERATOR:
            pass                                   # x += n keeps x's own class
        elif k == K.CALL_EXPR:
            d = c.referenced
            if d is not None and d.kind == K.FUNCTION_DECL:
                args = list(c.get_arguments())
                for i, a in enumerate(args):
                    self.sink(f"param:{d.get_usr()}#{i}", a)
        elif k == K.RETURN_STMT and fn is not None:
            kids = list(c.get_children())
            if kids and is_narrow_int(fn.result_type):
                self.sink(f"ret:{fn.get_usr()}", kids[0])
        if k == K.CSTYLE_CAST_EXPR and f and is_narrow_int(c.type):
            kids = list(c.get_children())
            inner = kids[-1] if kids else None
            if inner is not None:
                toks = [t.spelling for t in c.get_tokens()]
                frm = self.cast_spelling(toks)
                if frm:
                    self.g.casts.append((f, c.extent.start.line, c.extent.start.column, frm,
                                         is_ptrish(inner.type), sorted(
                                             s for s in self.sources(inner)
                                             if not s.startswith(("SEED", "SUB:")))))
        for ch in c.get_children():
            self.visit(ch, fn)

    @staticmethod
    def cast_spelling(toks):
        if len(toks) >= 3 and toks[0] == "(":
            try:
                j = toks.index(")")
            except ValueError:
                return None
            ty = " ".join(toks[1:j])
            return ty if "*" not in ty else None
        return None

    def parse(self, path: Path):
        tu = self.index.parse(str(path), args=ARGS)
        self.visit(tu.cursor)

    # --- solve -------------------------------------------------------------
    def solve(self) -> set[str]:
        """Forward closure from the seeds; SUB nodes (`a - b`) whose right operand
        carries a pointer are differences, so they're disabled and we re-solve.
        Disabling only removes flow, so this terminates."""
        disabled: set[str] = set()
        while True:
            carrying = {s for s in self.g.seeds if s not in disabled}
            work = list(carrying)
            while work:
                s = work.pop()
                for d in self.g.edges.get(s, ()):
                    if d not in carrying and d not in disabled:
                        carrying.add(d)
                        work.append(d)
            newly = {n for n, (rights, rseed) in self.g.subnodes.items()
                     if n not in disabled and (rseed or rights & carrying)}
            if not newly:
                return {s for s in carrying if not s.startswith("SUB:")}
            disabled |= newly


def _parse_one(args):
    """Worker: parse one TU, return its graph fragment (plain, picklable data)."""
    i, path = args
    an = Analyzer()
    an.tu_tag = str(i)
    an.parse(Path(path))
    g = an.g
    return ({k: set(v) for k, v in g.edges.items()}, set(g.seeds),
            {k: set(v) for k, v in g.decls.items()}, list(g.casts), dict(g.fields),
            dict(g.subnodes))


def parse_all(tus, jobs: int) -> "Analyzer":
    """Parse TUs in parallel and merge fragments into one Analyzer graph."""
    import multiprocessing as mp
    an = Analyzer()
    with mp.Pool(jobs) as pool:
        for n, (edges, seeds, decls, casts, fields, subs) in enumerate(
                pool.imap_unordered(_parse_one, [(i, str(t)) for i, t in enumerate(tus)]), 1):
            for k, v in edges.items():
                an.g.edges[k] |= v
            an.g.seeds |= seeds
            for k, v in decls.items():
                an.g.decls[k] |= v
            an.g.casts += casts
            an.g.fields.update(fields)
            an.g.subnodes.update(subs)
            if n % 100 == 0:
                print(f"  parsed {n}/{len(tus)}", file=sys.stderr)
    return an


def tu_list(which: str) -> list[Path]:
    build = [REPO / "decomp" / t for t in bm.read_manifest()]
    if which == "build":
        return build
    dlls = [Path(l) for l in (REPO / "tools/mirror/dll_manifest.txt").read_text().split()] \
        if (REPO / "tools/mirror/dll_manifest.txt").exists() else []
    return build + [REPO / d for d in dlls]


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--tus", choices=["build", "all"], default="build")
    ap.add_argument("--report-only", action="store_true")
    ap.add_argument("--json", metavar="PATH")
    import os
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    args = ap.parse_args(argv)

    # Parse against a header tree built from HAND rules only (u32 pinned etc.), never
    # from previously generated rules: those would already have widened the
    # declarations, so the analyzer would stop seeing them and drop their rules.
    bm.gen_headers(passes.load_pointer_rules(REPO / "mirror" / "rules", generated=False))
    tus = tu_list(args.tus)
    an = parse_all(tus, args.jobs)
    carrying = an.solve()

    # decl sites to retype (vars/params) and functions to ret-widen
    retypes, rets, fields, port_sites = [], [], [], []
    for s in sorted(carrying):
        if s.startswith("field:"):
            if s in an.g.fields:
                fields.append(an.g.fields[s])
            continue
        for (f, line, name, ty, kind) in sorted(an.g.decls.get(s, ())):
            if f.startswith("include/"):
                port_sites.append(f"{kind} {f}:{line} {name}")
                continue
            if not f.startswith(("decomp/src/", "decomp/include/")):
                continue
            if kind == "ret":
                rets.append((f, name))
            else:
                retypes.append((f, line, name, ty))
    # narrowing casts whose operand is a pointer or a carrying slot
    widen = []
    for (f, line, col, frm, ptr_inner, srcs) in an.g.casts:
        if not f.startswith(("decomp/src/", "decomp/include/")):
            continue
        if ptr_inner or any(s in carrying for s in srcs):
            widen.append((f, line, col, frm))

    summary = {
        "tus": len(tus), "seeds": len(an.g.seeds), "carrying_slots": len(carrying),
        "retype_sites": len(set(retypes)), "ret_funcs": len(set(rets)),
        "widen_casts": len(set(widen)), "carrying_fields": len(set(fields)),
    }
    print(json.dumps(summary, indent=2))
    if args.json:
        Path(args.json).write_text(json.dumps({
            "summary": summary,
            "retypes": sorted(set(retypes)), "rets": sorted(set(rets)),
            "widen": sorted(set(widen)), "fields": sorted(set(fields), key=str)}, indent=1))
    if port_sites:
        print(f"note: {len(set(port_sites))} carrying decl(s) live in the port's shadow "
              "headers (include/): fix by hand there:", file=sys.stderr)
        for x in sorted(set(port_sites))[:20]:
            print(f"  {x}", file=sys.stderr)
    if args.report_only:
        return 0
    return write_rules(retypes, rets, widen)


def write_rules(retypes, rets, widen) -> int:
    """Emit generated rules, each pre-validated against the source text: a rule
    whose site doesn't match textually (macro-expanded code, `int a, b;`
    multi-declarators, spelling mismatches) is DROPPED and reported, never
    emitted as a rule that silently wouldn't apply."""
    import re
    import shutil
    hand = passes.load_pointer_rules(REPO / "mirror" / "rules", generated=False)
    covered = {(passes._norm(r.get("file", "")), r.get("line")) for k in
               ("retype", "promote", "widen", "widen_cast") for r in hand[k]}
    covered_ret = {(passes._norm(r.get("file", "")), r.get("func")) for r in hand["ret"]}
    out, dropped = defaultdict(list), []
    src = {}

    def text(f):
        if f not in src:
            src[f] = (REPO / f).read_text(errors="replace").splitlines()
        return src[f]

    for (f, line, name, ty) in sorted(set(retypes)):
        if (f, line) in covered:
            continue
        ty = ty.split("[")[0].strip()                     # u32 tbl[N] -> u32
        nl, cnt = passes._retype(text(f)[line - 1], name, ty, "uintptr_t", None)
        if not nl:
            dropped.append(f"retype {f}:{line} {ty} {name} ({cnt} textual matches)")
            continue
        out[f].append(f'[[pointer.retype]]\nfile = "{f}"\nline = {line}\nsymbol = "{name}"\n'
                      f'from = "{ty}"\nto = "uintptr_t"\n')
    for (f, fn) in sorted(set(rets)):
        if (f, fn) in covered_ret:
            continue
        if not any(passes._promote_return(l, fn, "uintptr_t") for l in text(f)):
            dropped.append(f"ret {f} {fn} (no textual narrow return)")
            continue
        out[f].append(f'[[pointer.ret]]\nfile = "{f}"\nfunc = "{fn}"\nto = "uintptr_t"\n')
    by_line = defaultdict(list)
    for (f, line, col, frm) in sorted(set(widen)):
        by_line[(f, line, frm)].append(col)
    for (f, line, frm), cols in sorted(by_line.items()):
        if (f, line) in covered:
            continue
        pat = re.compile(rf"\(\s*{passes._type_pat(frm)}\s*\)")
        all_cols = [m.start() + 1 for m in pat.finditer(text(f)[line - 1])]
        for col in cols:
            if col not in all_cols:
                dropped.append(f"widen_cast {f}:{line} ({frm}) col {col} (macro or spelling)")
                continue
            occ = all_cols.index(col) + 1
            out[f].append(f'[[pointer.widen_cast]]\nfile = "{f}"\nline = {line}\nfrom = "{frm}"\n'
                          f'to = "uintptr_t"\n' + (f"occurrence = {occ}\n" if len(all_cols) > 1 else ""))

    gen = REPO / "mirror" / "rules" / "generated"
    shutil.rmtree(gen, ignore_errors=True)
    gen.mkdir(parents=True)
    for f, blocks in out.items():
        name = f.replace("decomp/", "").replace("/", "__").replace(".", "_") + ".toml"
        (gen / name).write_text(f"# GENERATED by tools/mirror/infer_ptr_ints.py for {f}. "
                                "DO NOT EDIT; hand rules in mirror/rules/*.toml take precedence.\n\n"
                                + "\n".join(blocks))
    (gen / "DROPPED.txt").write_text(
        "# Sites the analyzer found but could not express as a text rule.\n"
        "# Each needs a hand rule or a different fix.\n" + "\n".join(dropped) + "\n")
    n = sum(len(b) for b in out.values())
    print(f"wrote {n} generated rule(s) in {len(out)} file(s) to {gen.relative_to(REPO)}; "
          f"{len(dropped)} dropped (see DROPPED.txt)", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
