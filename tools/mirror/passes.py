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

# Absolute guest addresses baked into code (MEM1 / MMIO). None expected in
# track_dolphin, but the pass must catch them everywhere.
_RE_ABS_ADDR = re.compile(r"0x8[0-9A-Fa-f]{7}\b|0xCC0[0-9A-Fa-f]{4}\b")


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
    text = path.read_text(errors="replace")
    lines = text.splitlines()
    rel = path.as_posix()
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
                "[[pointer.roundtrip]] file=\"%s\" line=%d action=\"widen-or-handle\"" % (rel, i)))
        if _mmalloc_returning_narrow(ln):
            findings.append(Finding(
                "alloc_stored_narrow", "critical", rel, i, s,
                "mmAlloc() result stored into a narrow int; the allocation "
                "pointer is truncated.",
                "[[pointer.alloc]] file=\"%s\" line=%d action=\"return/accept pointer\"" % (rel, i)))
        if _RE_NARROW_TO_PTR.search(ln):
            findings.append(Finding(
                "narrow_to_ptr", "critical", rel, i, s,
                "pointer reconstructed from a narrow int value.",
                "[[pointer.reconstruct]] file=\"%s\" line=%d" % (rel, i)))
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
        if _RE_ABS_ADDR.search(ln):
            findings.append(Finding(
                "absolute_address", "critical", rel, i, s,
                "hard-coded guest MEM1/MMIO address; no fixed guest map on host.",
                "[[address.rewrite]] file=\"%s\" line=%d" % (rel, i)))

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
