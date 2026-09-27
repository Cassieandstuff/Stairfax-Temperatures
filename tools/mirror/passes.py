"""Transform-pipeline passes (skeleton).

Each pass is deterministic and idempotent. For the skeleton, P0/P1/P6 are the
identity transform (copy / parse-or-noop / emit-unchanged) and P2 is a working
pointer-width *worklist generator* — it does not rewrite code, it reports every
site a human rule must resolve before the mirror can be trusted on a 64-bit host.

P1 uses libclang when the `clang.cindex` bindings are importable; otherwise it
falls back to the heuristic line scanner below. The heuristic is intentionally
conservative about rewriting (it rewrites nothing) and liberal about flagging.

See docs/adr/0001-native-64bit-portable-mirror.md for the pass contract.
"""
from __future__ import annotations

import re
from dataclasses import dataclass, asdict
from pathlib import Path


# --- P1: parse ---------------------------------------------------------------

def have_libclang() -> bool:
    try:
        import clang.cindex  # noqa: F401
        return True
    except Exception:
        return False


# --- P2: pointer-width worklist ---------------------------------------------

@dataclass
class Finding:
    category: str
    severity: str          # "critical" (truncates at 64-bit) | "review"
    file: str
    line: int
    snippet: str
    note: str
    rule_hint: str         # suggested rules-manifest entry id/shape

    def key(self):
        return (self.file, self.line, self.category)


# int-ish types that are only 32 bits wide and therefore cannot hold a 64-bit
# host pointer without truncation.
_NARROW = r"(?:int|u32|s32|unsigned int|signed int|long)"

# Global/file-scope declaration of a narrow-typed variable (candidate pointer
# storage). We confirm it holds a pointer in a second scan.
_RE_NARROW_DECL = re.compile(
    rf"^\s*(?:static\s+)?{_NARROW}\s+(?P<name>[A-Za-z_]\w*)\s*(?:=|;)")

# A K&R-style function signature line: `<ret> name(arg, arg)` with no brace or
# semicolon, immediately preceding old-style parameter decls.
_RE_KNR_SIG = re.compile(r"\b[A-Za-z_]\w*\s*\([A-Za-z_][\w\s,]*\)\s*$")

# (int)/(u32)/(s32) applied to something that is (or yields) a pointer.
_RE_PTR_TO_NARROW = re.compile(
    rf"\((?P<ty>{_NARROW})\)\s*(?P<rhs>[A-Za-z_]\w*|\&|mmAlloc|g[A-Z]\w*)")

# (T*)(int)x  — an explicit round-trip through a narrow int back to a pointer.
_RE_ROUNDTRIP = re.compile(
    rf"\(\s*[A-Za-z_]\w*\s*\*\s*\)\s*\(\s*{_NARROW}\s*\)")

# (T*)<narrow-global-or-int-expr> — reconstructing a pointer from a narrow value.
_RE_NARROW_TO_PTR = re.compile(
    r"\(\s*(?P<ty>[A-Za-z_]\w*)\s*\*\s*\)\s*\(\s*(?:u32|int|s32)\s*\)")

# MMIO registers (WGPIPE etc.) — always an address, never a mask.
_RE_MMIO_ADDR = re.compile(r"0xCC0[0-9A-Fa-f]{4}\b")
# A MEM1-range constant (0x8xxxxxxx). Ambiguous: overwhelmingly a sign-bit mask
# or flag (0x80000000) unless used in a pointer context. We only treat it as an
# address when cast/deref'd as a pointer; a bitmask/#define use is skipped.
_RE_MEM1_CONST = re.compile(r"0x8[0-9A-Fa-f]{7}\b")
# 0x8xxxxxxx in pointer context: `(T*)0x8...` or `*(...)0x8...`.
_RE_MEM1_PTR = re.compile(r"\(\s*[A-Za-z_][\w ]*\*\s*\)\s*0x8[0-9A-Fa-f]{7}\b"
                          r"|\*\s*\(\s*[^)]*\)\s*0x8[0-9A-Fa-f]{7}\b")
# Bitmask / macro / flag uses that are NOT addresses.
_RE_MEM1_MASK = re.compile(r"[&|]\s*0x8[0-9A-Fa-f]{7}\b"
                           r"|0x8[0-9A-Fa-f]{7}\s*[&|]"
                           r"|#\s*define\b.*0x8[0-9A-Fa-f]{7}\b")


def _strip_noncode(line: str, in_block: bool) -> tuple[str, bool]:
    """Return (code-only line, still-in-block-comment) with // and /* */ and
    string/char literals blanked, so brace counting tracks real scope."""
    out = []
    i, n = 0, len(line)
    while i < n:
        two = line[i:i + 2]
        if in_block:
            if two == "*/":
                in_block = False
                i += 2
            else:
                i += 1
            continue
        if two == "/*":
            in_block = True
            i += 2
            continue
        if two == "//":
            break
        c = line[i]
        if c in "\"'":
            q = c
            i += 1
            while i < n and line[i] != q:
                if line[i] == "\\":
                    i += 1
                i += 1
            i += 1
            continue
        out.append(c)
        i += 1
    return "".join(out), in_block


def _mmalloc_returning_narrow(line: str) -> bool:
    # `g... = (int)mmAlloc(...)` — the canonical "pointer stored in an int" trap.
    return bool(re.search(rf"=\s*\(\s*{_NARROW}\s*\)\s*mmAlloc", line))


def scan_pointer_width(path: Path) -> list[Finding]:
    return scan_text_pointer_width(path.read_text(errors="replace"), path.as_posix())


def scan_text_pointer_width(text: str, rel: str) -> list[Finding]:
    lines = text.splitlines()
    findings: list[Finding] = []

    # Pass A: collect narrow-typed file-scope names, then decide if they ever
    # hold a pointer (assigned from a cast/mmAlloc, or later cast to a pointer).
    # Scope tracking counts braces on comment/string-stripped code so that braces
    # inside comments or literals don't desync depth (which would misflag locals
    # as globals).
    narrow_decls: dict[str, int] = {}   # name -> line
    knr_params: set[str] = set()        # subset of narrow_decls that are K&R params
    brace_depth = 0
    in_block_comment = False
    in_knr = False          # inside a K&R old-style parameter-decl block
    prev_code = ""
    for i, ln in enumerate(lines, 1):
        code, in_block_comment = _strip_noncode(ln, in_block_comment)
        # only treat depth-0 decls with an original (un-stripped) match as globals
        if brace_depth == 0 and "{" not in code:
            m = _RE_NARROW_DECL.match(ln)
            if m and _RE_NARROW_DECL.match(code):
                narrow_decls[m.group("name")] = i
                # K&R old-style param: decl sits between `name(args)` and body `{`
                if in_knr or _RE_KNR_SIG.search(prev_code):
                    knr_params.add(m.group("name"))
                    in_knr = True
        if "{" in code:
            in_knr = False
        brace_depth += code.count("{") - code.count("}")
        if brace_depth < 0:
            brace_depth = 0
        if code.strip():
            prev_code = code

    holds_ptr: set[str] = set()
    for ln in lines:
        for name in narrow_decls:
            if re.search(rf"\b{name}\b\s*=\s*\(\s*{_NARROW}\s*\)", ln) or \
               re.search(rf"\(\s*[A-Za-z_]\w*\s*\*\s*\)\s*(?:\(\s*u8\s*\*\s*\)\s*)?{name}\b", ln) or \
               re.search(rf"=\s*{name}\b", ln) and _looks_ptr_use(name, lines):
                holds_ptr.add(name)

    for name in sorted(holds_ptr):
        is_knr = name in knr_params
        findings.append(Finding(
            category="knr_param_holds_ptr" if is_knr else "int_global_holds_ptr",
            severity="critical",
            file=rel, line=narrow_decls[name],
            snippet=lines[narrow_decls[name] - 1].strip(),
            note=(f"K&R param '{name}' is declared narrow int but holds a pointer; "
                  "truncates on a 64-bit host."
                  if is_knr else
                  f"file-scope '{name}' is a narrow int but stores a pointer; "
                  "truncates on a 64-bit host."),
            rule_hint=f"[[pointer.promote]] symbol=\"{name}\" to=\"uintptr_t\"",
        ))

    # Pass B: per-line cast/address hazards.
    for i, ln in enumerate(lines, 1):
        s = ln.strip()
        if _RE_ROUNDTRIP.search(ln):
            findings.append(Finding(
                "ptr_int_roundtrip", "critical", rel, i, s,
                "pointer round-tripped through a 32-bit int; loses the high dword.",
                "[[pointer.widen]] file=\"%s\" line=%d  # (T*)(int)x -> (T*)(uintptr_t)x" % (rel, i)))
        if _mmalloc_returning_narrow(ln):
            findings.append(Finding(
                "alloc_stored_narrow", "critical", rel, i, s,
                "mmAlloc() result stored into a narrow int; the allocation "
                "pointer is truncated.",
                "[[pointer.widen]] file=\"%s\" line=%d  # (int)mmAlloc -> (uintptr_t)mmAlloc" % (rel, i)))
        if _RE_NARROW_TO_PTR.search(ln):
            findings.append(Finding(
                "narrow_to_ptr", "critical", rel, i, s,
                "pointer reconstructed from a narrow int value.",
                "[[pointer.widen]] file=\"%s\" line=%d" % (rel, i)))
        for m in _RE_PTR_TO_NARROW.finditer(ln):
            rhs = m.group("rhs")
            # skip obvious numeric/scalar casts (e.g. (int)floorf(...))
            if rhs in ("mmAlloc",) or rhs.startswith("g") or rhs == "&":
                findings.append(Finding(
                    "ptr_to_narrow", "review", rel, i, s,
                    f"cast of pointer-ish '{rhs}' to {m.group('ty')}; fine if only "
                    "compared/measured, truncating if reused as an address.",
                    "[[pointer.audit]] file=\"%s\" line=%d" % (rel, i)))
                break
        if _RE_MMIO_ADDR.search(ln):
            findings.append(Finding(
                "mmio_address", "critical", rel, i, s,
                "hard-coded MMIO register address; no memory-mapped IO on host.",
                "[[address.rewrite]] file=\"%s\" line=%d kind=\"mmio\"" % (rel, i)))
        elif _RE_MEM1_CONST.search(ln) and not _RE_MEM1_MASK.search(ln):
            if _RE_MEM1_PTR.search(ln):
                findings.append(Finding(
                    "mem1_address", "critical", rel, i, s,
                    "MEM1-range constant used as a pointer; no fixed guest map on host.",
                    "[[address.rewrite]] file=\"%s\" line=%d kind=\"mem1\"" % (rel, i)))
            else:
                findings.append(Finding(
                    "mem1_const", "review", rel, i, s,
                    "0x8xxxxxxx constant, not an obvious mask nor pointer; confirm it "
                    "is a flag/sentinel and not a guest address.",
                    "[[address.audit]] file=\"%s\" line=%d" % (rel, i)))

    # dedupe by (file,line,category)
    seen, out = set(), []
    for f in findings:
        if f.key() in seen:
            continue
        seen.add(f.key())
        out.append(f)
    out.sort(key=lambda f: (0 if f.severity == "critical" else 1, f.line))
    return out


def _looks_ptr_use(name: str, lines: list[str]) -> bool:
    # name is later dereferenced as an address: (T*)name, (u8*)name, name + n*0x..
    pat = re.compile(rf"\(\s*[A-Za-z_]\w*\s*\*\s*\)\s*(?:\(\s*u8\s*\*\s*\)\s*)?{name}\b"
                     rf"|\bname\b\s*\+\s*\w+\s*\*".replace("name", name))
    return any(pat.search(l) for l in lines)


def render_worklist_md(tu: str, findings: list[Finding], rev: str) -> str:
    crit = [f for f in findings if f.severity == "critical"]
    rev_n = [f for f in findings if f.severity == "review"]
    by_cat: dict[str, int] = {}
    for f in findings:
        by_cat[f.category] = by_cat.get(f.category, 0) + 1

    lines = [
        f"# P2 pointer-width worklist — `{tu}`",
        "",
        "> GENERATED by `tools/mirror/mirror_build.py` (pass P2). Do not hand-edit.",
        f"> decomp revision: `{rev}`",
        "",
        f"**{len(crit)} critical**, {len(rev_n)} to review, {len(findings)} total.",
        "",
        "Critical = truncates or corrupts on a 64-bit host and MUST be resolved by a",
        "rule in `mirror/rules/` before this TU is trustworthy in the mirror. Review =",
        "likely benign (compare/measure only) but must be eyeballed once.",
        "",
        "## By category",
        "",
        "| Category | Count |",
        "|----------|------:|",
    ]
    for cat, n in sorted(by_cat.items(), key=lambda kv: -kv[1]):
        lines.append(f"| `{cat}` | {n} |")
    lines += ["", "## Critical", ""]
    lines += _table(crit) if crit else ["_none_", ""]
    lines += ["", "## Review", ""]
    lines += _table(rev_n) if rev_n else ["_none_", ""]
    lines += [
        "", "## Suggested rule stubs", "",
        "Paste the ones you accept into `mirror/rules/pointers.toml` and re-run the",
        "pipeline; resolved sites drop off this list on the next generation.", "",
        "```toml",
    ]
    for f in crit:
        lines.append(f"# {f.file}:{f.line}  {f.category}")
        lines.append(f.rule_hint)
    lines += ["```", ""]
    return "\n".join(lines)


def _table(fs: list[Finding]) -> list[str]:
    out = ["| Line | Category | Note | Code |", "|-----:|----------|------|------|"]
    for f in fs:
        snip = f.snippet.replace("|", "\\|")
        if len(snip) > 70:
            snip = snip[:67] + "..."
        note = f.note.replace("|", "\\|")
        out.append(f"| {f.line} | `{f.category}` | {note} | `{snip}` |")
    return out


def findings_as_dicts(findings: list[Finding]) -> list[dict]:
    return [asdict(f) for f in findings]


# --- P2: rewriter ------------------------------------------------------------
#
# Applies the resolutions in mirror/rules/pointers.toml to a TU's text. Two rule
# kinds today, both deterministic and idempotent:
#
#   [[pointer.promote]] symbol="gFoo" to="uintptr_t"
#       Widen the declaration of a narrow-int global / K&R param / array element
#       that actually holds a pointer. uintptr_t keeps integer semantics but is
#       wide enough to carry a 64-bit host pointer.
#
#   [[pointer.widen]]   file="decomp/src/main/foo.c" line=NN
#       At that line, widen a narrowing cast that touches a pointer:
#         (T*)(int)x     -> (T*)(uintptr_t)x     (round-trip / reconstruct)
#         (int)mmAlloc.. -> (uintptr_t)mmAlloc.. (allocation store)
#
#   [[pointer.audit]]   file="..." line=NN  ok=true
#       Reviewed and judged benign; no rewrite, just records intent.
#
# Line numbers are stable under these rules (they never add or remove lines), so
# every rule is applied against the original line index.

import tomllib

_NARROW_TYPE_RE = r"(?:unsigned int|signed int|int|u32|s32|long)"


def load_pointer_rules(rules_dir: Path) -> dict:
    """Merge the [pointer.*] tables from every *.toml under rules_dir."""
    promote, widen, audit, ret, osglob = [], [], [], [], []
    for toml in sorted(rules_dir.glob("*.toml")):
        try:
            data = tomllib.loads(toml.read_text())
        except Exception as e:  # a malformed manifest should fail loudly
            raise SystemExit(f"error: {toml}: {e}")
        p = data.get("pointer", {})
        promote += p.get("promote", [])
        widen += p.get("widen", [])
        audit += p.get("audit", [])
        ret += p.get("ret", [])
        osglob += data.get("osglobals", {}).get("read", [])
    return {"promote": promote, "widen": widen, "audit": audit,
            "ret": ret, "osglobals": osglob}


# Absolute OS-globals read: *(T*)0xADDR -> os_globals_read_u32(0xADDRu).
_RE_OSGLOB_READ = re.compile(r"\*\s*\(\s*[A-Za-z_]\w*\s*\*\s*\)\s*0x([0-9A-Fa-f]+)")


def _rewrite_osglobals(line: str) -> str | None:
    new = _RE_OSGLOB_READ.sub(r"os_globals_read_u32(0x\1u)", line)
    return new if new != line else None


def _promote_return(line: str, func: str, to: str) -> str | None:
    """Widen the return type in a prototype or definition of `func`."""
    pat = re.compile(rf"^(\s*(?:static\s+)?){_NARROW_TYPE_RE}(\s+{re.escape(func)}\s*\()")
    new = pat.sub(rf"\1{to}\2", line, count=1)
    return new if new != line else None


def _promote_decl(line: str, symbol: str, to: str) -> str | None:
    """Replace the narrow type in a declaration of `symbol` with `to`."""
    pat = re.compile(rf"^(\s*(?:static\s+)?){_NARROW_TYPE_RE}(\s+{re.escape(symbol)}\s*(?:=|;|\[))")
    new = pat.sub(rf"\1{to}\2", line, count=1)
    return new if new != line else None


def _widen_casts(line: str) -> str | None:
    """Widen pointer-touching narrowing casts on a single line."""
    new = re.sub(rf"(\([A-Za-z_]\w*\s*\*\)\s*)\(\s*{_NARROW_TYPE_RE}\s*\)",
                 r"\1(uintptr_t)", line)                       # (T*)(int)x
    new = re.sub(rf"\(\s*{_NARROW_TYPE_RE}\s*\)(\s*mmAlloc)",
                 r"(uintptr_t)\1", new)                        # (int)mmAlloc
    return new if new != line else None


def apply_pointer_rules(text: str, rules: dict, rel: str) -> tuple[str, list[str], list[str]]:
    """Return (new_text, applied_notes, unmatched_notes)."""
    lines = text.splitlines(keepends=True)
    applied, unmatched = [], []

    # depth-0 map so symbol-only promotions target file-scope decls and K&R
    # params (both at brace-depth 0), never an unrelated local of the same name.
    depth0 = _depth0_lines("".join(lines))

    # promotions: symbol-targeted, optionally pinned to a specific line.
    for r in rules["promote"]:
        sym, to = r.get("symbol"), r.get("to", "uintptr_t")
        pin = r.get("line")
        if not sym:
            continue
        if isinstance(pin, int):
            if not (1 <= pin <= len(lines)):
                unmatched.append(f"promote {sym}: line {pin} out of range")
                continue
            nl = _promote_decl(lines[pin - 1], sym, to)
            if nl:
                lines[pin - 1] = nl
                applied.append(f"promote {sym} -> {to}  (line {pin})")
            else:
                unmatched.append(f"promote {sym}: no narrow decl of '{sym}' at line {pin}")
            continue
        hit = False
        for i, ln in enumerate(lines):
            if (i + 1) not in depth0:
                continue
            nl = _promote_decl(ln, sym, to)
            if nl:
                lines[i] = nl
                applied.append(f"promote {sym} -> {to}  (line {i+1})")
                hit = True
                break
        if not hit:
            unmatched.append(
                f"promote {sym}: no file-scope narrow declaration found "
                "(add line=NN to target a K&R param or local)")

    # return-type widening: function-name-targeted (proto + definition).
    for r in rules["ret"]:
        func, to = r.get("func"), r.get("to", "uintptr_t")
        if not func:
            continue
        hits = 0
        for i, ln in enumerate(lines):
            nl = _promote_return(ln, func, to)
            if nl:
                lines[i] = nl
                hits += 1
        if hits:
            applied.append(f"return {func} -> {to}  ({hits} site(s))")
        else:
            unmatched.append(f"return {func}: no narrow return type found")

    # widen: (file,line)-targeted; only for rules naming this TU
    for r in rules["widen"]:
        if _norm(r.get("file", "")) != _norm(rel):
            continue
        n = r.get("line")
        if not isinstance(n, int) or not (1 <= n <= len(lines)):
            unmatched.append(f"widen {rel}:{r.get('line')}: line out of range")
            continue
        nl = _widen_casts(lines[n - 1])
        if nl:
            lines[n - 1] = nl
            applied.append(f"widen cast at line {n}")
        else:
            unmatched.append(f"widen {rel}:{n}: no narrowing pointer cast to widen")

    # OS-globals reads: (file,line)-targeted; rewrite to the runtime accessor and
    # ensure the runtime header is included.
    need_os_header = False
    for r in rules.get("osglobals", []):
        if _norm(r.get("file", "")) != _norm(rel):
            continue
        n = r.get("line")
        if not isinstance(n, int) or not (1 <= n <= len(lines)):
            unmatched.append(f"osglobals {rel}:{r.get('line')}: line out of range")
            continue
        nl = _rewrite_osglobals(lines[n - 1])
        if nl:
            lines[n - 1] = nl
            need_os_header = True
            applied.append(f"osglobals read -> accessor at line {n}")
        else:
            unmatched.append(f"osglobals {rel}:{n}: no *(T*)0xADDR read to rewrite")

    text = "".join(lines)
    if need_os_header and "stairfax_os.h" not in text:
        text = '#include "stairfax_os.h"\n' + text
    return text, applied, unmatched


def _norm(p: str) -> str:
    return p.replace("\\", "/").lstrip("./")


def _depth0_lines(text: str) -> set[int]:
    """1-based line numbers that sit at brace-depth 0 (file scope or K&R param
    block), computed on comment/string-stripped code."""
    out: set[int] = set()
    depth = 0
    in_block = False
    for i, ln in enumerate(text.splitlines(), 1):
        code, in_block = _strip_noncode(ln, in_block)
        if depth == 0 and "{" not in code:
            out.add(i)
        depth += code.count("{") - code.count("}")
        if depth < 0:
            depth = 0
    return out


def render_tree_report_md(root: str, per_file: list[tuple[str, list["Finding"]]],
                          rev: str) -> str:
    """Aggregate P2 report across many TUs. `per_file` is [(relpath, findings)]."""
    all_findings = [f for _, fs in per_file for f in fs]
    crit = [f for f in all_findings if f.severity == "critical"]
    rev_n = [f for f in all_findings if f.severity == "review"]
    cat: dict[str, int] = {}
    for f in all_findings:
        cat[f.category] = cat.get(f.category, 0) + 1

    scanned = len(per_file)
    dirty = [(p, fs) for p, fs in per_file if fs]
    clean = scanned - len(dirty)

    def ncrit(fs):
        return sum(1 for f in fs if f.severity == "critical")

    ranked = sorted(dirty, key=lambda pf: (-ncrit(pf[1]), -len(pf[1]), pf[0]))

    lines = [
        f"# P2 pointer-width scan — `{root}`",
        "",
        "> GENERATED by `tools/mirror/mirror_build.py --scan-tree` (pass P2). Do not hand-edit.",
        f"> decomp revision: `{rev}`",
        "",
        "## Totals",
        "",
        f"- **{scanned}** TUs scanned — {len(dirty)} with findings, {clean} clean.",
        f"- **{len(crit)} critical** (truncate/corrupt on a 64-bit host), "
        f"{len(rev_n)} to review, **{len(all_findings)} total**.",
        "",
        "Critical findings must each be resolved by a rule in `mirror/rules/` before",
        "the owning TU is trustworthy in the 64-bit mirror.",
        "",
        "## By category",
        "",
        "| Category | Count |",
        "|----------|------:|",
    ]
    for c, n in sorted(cat.items(), key=lambda kv: -kv[1]):
        lines.append(f"| `{c}` | {n} |")

    lines += [
        "", "## By TU (worst first)", "",
        "Per-TU worklists: run `--worklist decomp/src/main/<tu>.c` for line-level detail.",
        "",
        "| TU | Critical | Review | Total |",
        "|----|---------:|-------:|------:|",
    ]
    for p, fs in ranked:
        c = ncrit(fs)
        r = len(fs) - c
        lines.append(f"| `{p}` | {c} | {r} | {len(fs)} |")
    if clean:
        lines += ["", f"_{clean} TU(s) had no pointer-width findings._", ""]
    return "\n".join(lines)
