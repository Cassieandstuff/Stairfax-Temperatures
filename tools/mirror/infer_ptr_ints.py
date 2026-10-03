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


# Rounding helpers used on both pointers and sizes (mm.h). Treated
# context-sensitively: one pointer caller (model.c `roundUpTo32((int)out + 0x64)`)
# must not mark every size rounded through them -- that flooded mm.c's allocator
# sizes (HeapItem.size, bestSize, largest...) as pointer-carrying.
POLYMORPHIC = {"alignUp2", "roundUpTo4", "roundUpTo8", "roundUpTo16", "roundUpTo32"}


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
        self.copy_pred = defaultdict(set) # dst -> {src} for DIRECT value copies only
        self.arg_pred = defaultdict(set)  # param slot -> {src}: direct copies at CALL sites
        self.recon = set()                # slots cast straight back to a pointer
        self.dispatch = defaultdict(set)  # (fn-ptr field name, arity) -> {function USR}
        self.icalls = []                  # ((field name, arity), arg index, tokens, direct)
        self.macro_skips = 0

    def flow(self, src, dst, copy=False):
        if src and dst and src != dst:
            self.edges[src].add(dst)
            if copy:
                self.copy_pred[dst].add(src)
                if dst.startswith("param:"):
                    self.arg_pred[dst].add(src)


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
        self.zero_tests = set()           # extents of `(T*)x` operands of `== NULL` tests
        self.index = ci.Index.create()
        self.subs = {}                    # id -> (left cursor, right cursor) of `a - b`
        self.tu_tag = "0"                 # makes SUB node names unique across worker TUs
        self.varfield = {}                # var USR -> fn-ptr field name it was loaded from

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
            if d is not None and d.kind == K.FUNCTION_DECL and d.spelling in POLYMORPHIC:
                # value-polymorphic helper: this call's result carries a pointer
                # iff this call's argument does (the helper itself still widens
                # through its param/ret slots, but its ret doesn't flow back out)
                args = list(e.get_arguments())
                return self.sources(args[0]) if args else set()
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
                return {x if x.startswith(("SEED", "SUB:", "~")) else "~" + x for x in a | b}
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
                rsrc = {r.lstrip("~") for r in self.sources(right)}
                self.g.subnodes[node] = ({r for r in rsrc if not r.startswith(("SEED", "SUB:"))},
                                         "SEED" in rsrc)
                for s2 in self.sources(left):
                    if s2 == "SEED":
                        self.g.seeds.add(node)
                    elif not s2.startswith("SUB:"):
                        self.g.flow(s2.lstrip("~"), node)
                self.g.flow(node, dst_slot)
            elif s.startswith("~"):
                self.g.flow(s[1:], dst_slot)              # arithmetic: forward only
            else:
                self.g.flow(s, dst_slot, copy=True)       # direct copy: both ways

    @staticmethod
    def strip(e):
        """Peel parens / implicit conversions / casts / derefs."""
        while e is not None and e.kind in (K.PAREN_EXPR, K.UNEXPOSED_EXPR, K.CSTYLE_CAST_EXPR,
                                           K.UNARY_OPERATOR):
            kids = list(e.get_children())
            if not kids:
                return e
            e = kids[-1]
        return e

    def func_ref(self, e):
        """(USR, arity) of a function named directly (through casts), else None."""
        e = self.strip(e)
        if e is not None and e.kind == K.DECL_REF_EXPR and e.referenced is not None \
                and e.referenced.kind == K.FUNCTION_DECL:
            fn = e.referenced
            return (fn.get_usr(), len(list(fn.get_arguments())))
        return None

    def fnptr_field_name(self, e):
        """Field name if e (through casts/parens/derefs) reads a function-pointer
        struct field, or a variable previously loaded from one."""
        e = self.strip(e)
        if e is None:
            return None
        if e.kind == K.MEMBER_REF_EXPR and e.referenced is not None and \
                e.referenced.kind == K.FIELD_DECL:
            return e.referenced.spelling
        if e.kind == K.DECL_REF_EXPR and e.referenced is not None:
            return self.varfield.get(e.referenced.get_usr())
        return None

    def direct_slot(self, e):
        """The slot an expression reads *directly* (through parens / implicit
        conversions / int->int casts), or None if it's arithmetic or a constant."""
        while e is not None and e.kind in (K.PAREN_EXPR, K.UNEXPOSED_EXPR, K.CSTYLE_CAST_EXPR):
            kids = list(e.get_children())
            if not kids:
                return None
            if e.kind == K.CSTYLE_CAST_EXPR and not is_narrow_int(kids[-1].type):
                return None
            e = kids[-1]
        if e is None:
            return None
        srcs = self.sources(e)
        return next(iter(srcs)) if len(srcs) == 1 and not next(iter(srcs)).startswith(
            ("SEED", "SUB:", "~")) else None

    @staticmethod
    def _ext(e):
        x = e.extent
        return (x.start.file.name if x.start.file else None, x.start.offset, x.end.offset)

    @staticmethod
    def _is_null(e):
        """A null-pointer / zero operand: 0, NULL, (void*)0, __null."""
        while e is not None and e.kind in (K.PAREN_EXPR, K.UNEXPOSED_EXPR, K.CSTYLE_CAST_EXPR,
                                           K.GNU_NULL_EXPR):
            if e.kind == K.GNU_NULL_EXPR:
                return True
            kids = list(e.get_children())
            e = kids[-1] if kids else None
        if e is None:
            return False
        if e.kind == K.INTEGER_LITERAL:
            toks = [t.spelling for t in e.get_tokens()]
            return bool(toks) and toks[0].rstrip("uUlL") in ("0", "0x0")
        return False

    def visit(self, c, fn=None):
        f = rel(c.location.file.name if c.location.file else None)
        k = c.kind
        if k == K.BINARY_OPERATOR and self.binop(c) in ("==", "!=", "BinaryOperator.EQ",
                                                          "BinaryOperator.NE"):
            # `(void*)x == NULL` is a zero test spelled as a pointer compare (the
            # decomp does this for game bits); the cast is not pointer evidence
            ops = list(c.get_children())
            if len(ops) == 2:
                for a, b in ((ops[0], ops[1]), (ops[1], ops[0])):
                    if self._is_null(b):
                        e = a
                        while e is not None and e.kind in (K.PAREN_EXPR, K.UNEXPOSED_EXPR,
                                                           K.CSTYLE_CAST_EXPR):
                            self.zero_tests.add(self._ext(e))
                            kids = list(e.get_children())
                            e = kids[-1] if kids else None
        if k == K.FUNCTION_DECL:
            fn = c
            if f and is_narrow_int(c.result_type):
                self.g.decls[f"ret:{c.get_usr()}"].add((f, c.location.line, c.spelling, "", "ret"))
            for i, a in enumerate(c.get_arguments()):
                if is_narrow_int(a.type):
                    af = rel(a.location.file.name if a.location.file else None)
                    if af:
                        self.g.decls[f"param:{c.get_usr()}#{i}"].add(
                            (af, a.location.line, a.spelling, a.type.spelling,
                             f"param:{c.spelling}:{i}"))
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
        # --- function-pointer dispatch (DLL interface tables) ---------------
        if k == K.INIT_LIST_EXPR:
            rt = c.type.get_canonical()
            if rt.kind == T.RECORD:
                for fld, el in zip(rt.get_fields(), c.get_children()):
                    fu = self.func_ref(el)
                    if fu:
                        self.g.dispatch[(fld.spelling, fu[1])].add(fu[0])
        if k == K.BINARY_OPERATOR and self.binop(c) == "=":
            kids = list(c.get_children())
            if len(kids) == 2:
                lhs = self.strip(kids[0])
                fu = self.func_ref(kids[1])
                if fu and lhs is not None and lhs.kind == K.MEMBER_REF_EXPR and lhs.referenced is not None:
                    self.g.dispatch[(lhs.referenced.spelling, fu[1])].add(fu[0])
                fname = self.fnptr_field_name(kids[1])
                if fname and lhs is not None and lhs.kind == K.DECL_REF_EXPR and lhs.referenced is not None:
                    self.varfield[lhs.referenced.get_usr()] = fname
        if k == K.VAR_DECL:
            kids = [ch for ch in c.get_children() if ch.kind != K.TYPE_REF]
            if kids:
                fname = self.fnptr_field_name(kids[-1])
                if fname:
                    self.varfield[c.get_usr()] = fname
        if k == K.CALL_EXPR and (c.referenced is None or c.referenced.kind != K.FUNCTION_DECL):
            kids = list(c.get_children())
            fname = self.fnptr_field_name(kids[0]) if kids else None
            if fname:
                args = list(c.get_arguments())
                for i, a in enumerate(args):
                    toks = self.sources(a)
                    if toks:
                        # key (field name, arity): a call only reaches functions with the
                        # same parameter count; same-named fields in other interface
                        # structs with other signatures don't get its arguments.
                        self.g.icalls.append(((fname, len(args)), i, sorted(toks),
                                              self.direct_slot(a) is not None))
        if k in (K.CSTYLE_CAST_EXPR, K.UNEXPOSED_EXPR) and is_ptrish(c.type) and \
                c.type.get_canonical().kind == T.POINTER:
            kids = list(c.get_children())
            # not an array decaying to its element pointer (`int ids[4]` passed or
            # cast as int*): that names the array's storage, it doesn't rebuild a
            # pointer that was stored in an int
            if kids and is_narrow_int(kids[-1].type) and self._ext(c) not in self.zero_tests and \
                    canon(kids[-1].type).kind not in (T.CONSTANTARRAY, T.INCOMPLETEARRAY):
                d = self.direct_slot(kids[-1])
                if d:
                    self.g.recon.add(d)       # an int rebuilt as a pointer held one
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
    def resolve_indirect(self):
        """Indirect call through fn-ptr field NAME -> arg i flows into param i of
        every function placed in a field of that name (DLL interface tables:
        ObjectDescriptorNN.render <-> ObjectInterface.render)."""
        n = 0
        for (fname, i, toks, direct) in self.g.icalls:
            for fu in self.g.dispatch.get(fname, ()):
                dst = f"param:{fu}#{i}"
                for t in toks:
                    if t == "SEED":
                        self.g.seeds.add(dst)
                    elif t.startswith("SUB:"):
                        continue
                    else:
                        self.g.flow(t.lstrip("~"), dst, copy=direct and not t.startswith("~"))
                        n += 1
        self.indirect_edges = n

    def solve(self) -> set[str]:
        """Two facts, kept apart so they can't alternate through hub slots:
          MUST: a slot cast straight back to a pointer ((T*)x) must hold one. This
                propagates BACKWARD over direct copies (a variable passed straight
                into it must carry too, or it truncates first).
          MAY:  anything pointer-valued flows FORWARD (assignment, args, returns,
                pointer-preserving arithmetic) from the seeds ((int)ptr casts)
                and from the MUST set.
        Backward never starts from a slot that's carrying only because something
        flowed in. Otherwise one quirk (a gamebit value cast to a pointer, a pointer
        logged through logPrintf) floods every caller of a hub param (mmAlloc's
        size, logPrintf's args) via forward-then-backward alternation.
        SUB nodes (`a - b`) whose right operand carries are differences, so they're
        disabled and we re-solve; that only removes flow, so it terminates."""
        # MUST seeds: reconstruction evidence on variables/params/returns. Not on
        # struct FIELDS: a field is one slot shared program-wide, so one overloaded
        # use (rebuilt as a pointer here, passed as an id there) would poison every
        # reader. Pointers STORED into fields still flow (MAY) normally.
        must = {x for x in self.g.recon if not x.startswith("field:")}
        work = list(must)
        while work:
            # backward ONLY from a param to the direct args at its call sites. One
            # value at one point, so it's sound. Not through `x = e` assignments:
            # locals get reused (shader_dolphin's v1 holds a texel address, then a
            # random number), which would wrongly make randomGetRange's result a
            # pointer.
            s = work.pop()
            for src in self.g.arg_pred.get(s, ()):
                if src not in must and not src.startswith("SUB:"):
                    must.add(src)
                    work.append(src)
        self.field_recon = {x for x in self.g.recon if x.startswith("field:")}
        disabled: set[str] = set(getattr(self, "held", ()))
        while True:
            carrying = {x for x in (self.g.seeds | must) if x not in disabled}
            work = list(carrying)
            while work:                               # forward over all flow
                s = work.pop()
                for d in self.g.edges.get(s, ()):
                    if d not in carrying and d not in disabled:
                        carrying.add(d)
                        work.append(d)
            newly = {n for n, (rights, rseed) in self.g.subnodes.items()
                     if n not in disabled and (rseed or rights & carrying)}
            if not newly:
                self.disabled = disabled
                self.must = must
                return {x for x in carrying if not x.startswith("SUB:")}
            disabled |= newly


def live_holds(an, carrying: set[str], held_keys: set[str]) -> tuple[set[str], set[str]]:
    """Split holds into (live, stale). The regen fixpoint only ever ADDS holds, so a
    hold added early (when the hold set was smaller and more paths were open) can
    outlive its reason once a later hold cuts the path into it. A held slot is
    live iff it would carry if released: it's a seed / MUST slot itself, or a
    carrying slot flows straight into it. A cast-line hold ("file:LINE") is live
    iff a cast on that line still has a pointer or carrying operand. Releasing
    stale holds can't change the carrying set (nothing flows through them, even
    when released together), so the generated rules are unchanged."""
    must = getattr(an, "must", set())
    fed = {d for src in carrying for d in an.g.edges.get(src, ())}
    def slot_live(sl):
        return sl in an.g.seeds or sl in must or sl in fed
    by_key = defaultdict(set)
    for sl, ds in an.g.decls.items():
        for (f, ln, n, _, _) in ds:
            by_key[f"{f}:{n}@{ln}"].add(sl)
            by_key[f"{f}:{n}"].add(sl)
    cast_live = {(f, line) for (f, line, col, frm, ptr_inner, srcs) in an.g.casts
                 if ptr_inner or any(x in carrying for x in srcs)}
    live, stale = set(), set()
    for k in held_keys:
        f, tail = k.rsplit(":", 1)
        if tail.isdigit():
            ok = (f, int(tail)) in cast_live
        else:
            ok = any(slot_live(sl) for sl in by_key.get(k, ()))
        (live if ok else stale).add(k)
    return live, stale


def explain(an, carrying, name, limit=4):
    """Print, for carrying slots declared as `name`, the shortest chain from a
    seed ((int)ptr cast) or reconstruction ((T*)x) to it."""
    from collections import deque
    dis = getattr(an, "disabled", set())
    label = lambda sl: ", ".join(sorted({f"{n}@{f}:{l}" for (f, l, n, t, k) in
                                         an.g.decls.get(sl, ())})[:1]) or sl[:60]
    copy_succ = defaultdict(set)                      # reverse of arg_pred
    for d, srcs in an.g.arg_pred.items():
        for s0 in srcs:
            copy_succ[d].add(s0)
    starts = [(x, "seed") for x in an.g.seeds] + [(x, "recon") for x in an.g.recon]
    parent, q = {}, deque()
    for x, why in starts:
        if x not in parent and x not in dis:
            parent[x] = (None, why)
            q.append(x)
    while q:
        u = q.popleft()
        for v in an.g.edges.get(u, ()):
            if v not in parent and v not in dis:
                kind = "copy" if u in an.g.copy_pred.get(v, ()) else "arith/flow"
                parent[v] = (u, kind)
                q.append(v)
        if u in getattr(an, "must", set()):           # backward only from MUST slots
            for v in copy_succ.get(u, ()):
                if v not in parent and v not in dis and v in an.must:
                    parent[v] = (u, "BACKWARD copy")
                    q.append(v)
    targets = [sl for sl in carrying if any(n == name for (_, _, n, _, _) in an.g.decls.get(sl, ()))]
    for sl in targets[:limit]:
        chain, cur = [], sl
        while cur is not None:
            pu, why = parent.get(cur, (None, "?"))
            chain.append(f"{label(cur)}  [{why}]")
            cur = pu
        print(f"--- {label(sl)}")
        for step in reversed(chain):
            print(f"      {step}")


def _parse_one(args):
    """Worker: parse one TU, return its graph fragment (plain, picklable data)."""
    i, path = args
    an = Analyzer()
    an.tu_tag = str(i)
    an.parse(Path(path))
    g = an.g
    return ({k: set(v) for k, v in g.edges.items()}, set(g.seeds),
            {k: set(v) for k, v in g.decls.items()}, list(g.casts), dict(g.fields),
            dict(g.subnodes), {k: set(v) for k, v in g.copy_pred.items()}, set(g.recon),
            {k: set(v) for k, v in g.dispatch.items()}, list(g.icalls),
            {k: set(v) for k, v in g.arg_pred.items()})


def parse_all(tus, jobs: int) -> "Analyzer":
    """Parse TUs in parallel and merge fragments into one Analyzer graph."""
    import multiprocessing as mp
    an = Analyzer()
    with mp.Pool(jobs) as pool:
        for n, (edges, seeds, decls, casts, fields, subs, cpred, recon, disp, icalls, apred) in enumerate(
                pool.imap_unordered(_parse_one, [(i, str(t)) for i, t in enumerate(tus)]), 1):
            for k, v in edges.items():
                an.g.edges[k] |= v
            an.g.seeds |= seeds
            for k, v in decls.items():
                an.g.decls[k] |= v
            an.g.casts += casts
            an.g.fields.update(fields)
            an.g.subnodes.update(subs)
            for k, v in cpred.items():
                an.g.copy_pred[k] |= v
            an.g.recon |= recon
            for k, v in disp.items():
                an.g.dispatch[k] |= v
            an.g.icalls += icalls
            for k, v in apred.items():
                an.g.arg_pred[k] |= v
            if n % 100 == 0:
                print(f"  parsed {n}/{len(tus)}", file=sys.stderr)
    an.resolve_indirect()
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
    ap.add_argument("--hold", metavar="FILE", help="slots (decomp/path:symbol@declline per line) to "
                    "hold back: not widened, and they don't propagate")
    ap.add_argument("--explain", metavar="NAME", action="append", default=[],
                    help="print the seed->slot chain for carrying slots named NAME")
    args = ap.parse_args(argv)

    # Parse against a header tree built from HAND rules only (u32 pinned etc.), never
    # from previously generated rules: those would already have widened the
    # declarations, so the analyzer would stop seeing them and drop their rules.
    bm.gen_headers(passes.load_pointer_rules(REPO / "mirror" / "rules", generated=False))
    tus = tu_list(args.tus)
    an = parse_all(tus, args.jobs)
    held_keys = set()
    if args.hold and Path(args.hold).exists():
        held_keys = {l.strip() for l in Path(args.hold).read_text().splitlines() if l.strip()}
    # "file:name@line" holds one declaration; legacy "file:name" holds every
    # same-named slot in the file
    an.held = {sl for sl, ds in an.g.decls.items()
               if any(f"{f}:{n}@{ln}" in held_keys or f"{f}:{n}" in held_keys
                      for (f, ln, n, _, _) in ds)}
    carrying = an.solve()
    held_keys, released = live_holds(an, carrying, held_keys)
    if released:
        print(f"released {len(released)} stale hold(s): their slots no longer carry",
              file=sys.stderr)
        for k in sorted(released):
            print(f"  - {k}", file=sys.stderr)
    for nm in args.explain:
        explain(an, carrying, nm)
    if args.explain:
        return 0

    # decl sites to retype (vars/params) and functions to ret-widen
    retypes, rets, fields, port_sites, pretypes = [], [], [], [], []
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
            elif kind.startswith("param:") and not name:
                _, fn, idx = kind.split(":")
                pretypes.append((f, line, fn, int(idx), ty))
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
        "tus": len(tus), "seeds": len(an.g.seeds), "recon_evidence": len(an.g.recon),
        "must_slots": len(getattr(an, "must", ())),
        "dispatch_fields": len(an.g.dispatch), "indirect_calls": len(an.g.icalls),
        "indirect_edges": getattr(an, "indirect_edges", 0),
        "carrying_slots": len(carrying),
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
    # cast-line holds ("decomp/path:LINE"): drop generated widen_casts on that line
    cast_holds = {(k.rsplit(":", 1)[0], int(k.rsplit(":", 1)[1])) for k in held_keys
                  if k.rsplit(":", 1)[1].isdigit()}
    widen = [w for w in widen if (w[0], w[1]) not in cast_holds]
    rc = write_rules(retypes, rets, widen, pretypes)
    if args.hold:                     # always rewrite: released holds must disappear
        (REPO / "mirror" / "rules" / "generated" / "HELD.txt").write_text(
            "# Slots the analyzer would widen but the signedness check held back: widening\n"
            "# them changes a comparison's meaning (x < 0, signed vs unsigned). Either the\n"
            "# analyzer is wrong (not a pointer) or it's a sign/tag test needing a hand\n"
            "# decision. Produced by tools/mirror/check_signedness.py --emit-holds;\n"
            "# holds whose slot no longer carries are released by infer_ptr_ints.py.\n"
            + "\n".join(sorted(held_keys)) + "\n")
    return rc


def write_rules(retypes, rets, widen, pretypes=()) -> int:
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
    for (f, line, fn, idx, ty) in sorted(set(pretypes)):        # unnamed params
        ty = ty.split("[")[0].strip()
        if not passes._retype_param(text(f)[line - 1], fn, idx, ty, "uintptr_t"):
            dropped.append(f"retype_param {f}:{line} {fn}#{idx} {ty} (no textual match)")
            continue
        out[f].append(f'[[pointer.retype_param]]\nfile = "{f}"\nline = {line}\nfunc = "{fn}"\n'
                      f'index = {idx}\nfrom = "{ty}"\nto = "uintptr_t"\n')
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
