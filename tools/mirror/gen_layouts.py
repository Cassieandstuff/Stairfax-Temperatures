#!/usr/bin/env python3
"""Generate GameCube -> host struct transcoders for on-disc formats (HOST-STRUCT).

On-disc structs that contain pointer members (relocated file offsets on the
GameCube) can't be used in place on a 64-bit host: every member after the first
pointer moves. This parses the structs twice with libclang, once for the
GameCube (32-bit big-endian PowerPC EABI, which reproduces the decomp's
STATIC_ASSERTed offsets) and once for the host, flattens each into scalar leaves
(nested structs, arrays, the FIRST member of each union), and emits C that copies
every leaf from its GameCube offset to its host offset, byte-swapping from big
endian. A pointer leaf is a 4-byte disc value (usually a file offset), stored
zero-extended in the pointer-width host slot; relocation is the caller's job.

  python3 tools/mirror/gen_layouts.py        # writes mirror/runtime/gen_model_layout.h
"""
from __future__ import annotations

import sys
from pathlib import Path

import clang.cindex as ci

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
# (header to parse, structs to transcode, output, generated headers it builds on)
TARGETS = [
    ("main/model.h", ["ModelFileHeader", "Shader", "ModelDisplayListEntry", "ModelVtxAnimChunk"],
     "gen_model_layout.h", []),
    ("main/map_block.h", ["MapBlockData", "MapBlockBoundsRec"],
     "gen_mapblock_layout.h", ["gen_model_layout.h"]),        # Shader comes from the model header
]
HEADER, STRUCTS = TARGETS[0][0], TARGETS[0][1]                # defaults for parse()/leaves() callers
K, T = ci.CursorKind, ci.TypeKind
FLOATS = {T.FLOAT: 4, T.DOUBLE: 8}


def parse(target: str, host: bool, header: str | None = None, structs: list | None = None) -> dict:
    header = header or HEADER
    structs = structs or STRUCTS
    # The GameCube view is the decomp's own headers (s32 = long = 4 bytes there).
    # The host view is the MIRROR's header tree + prelude, i.e. exactly what compiled
    # mirror code sees (u32/s32 pinned to 32-bit, any rule-widened fields).
    if host:
        inc = [f"-I{REPO/'mirror/include'}", f"-I{REPO/'mirror/runtime'}", f"-I{REPO/'decomp'}",
               f"-I{REPO/'decomp/src'}", "-include", "mirror_prelude.h"]
    else:
        inc = [f"-I{REPO/'decomp/include'}", f"-I{REPO/'decomp'}", f"-I{REPO/'decomp/src'}"]
    args = ["-xc", "-std=gnu99", "-fdeclspec", "-fms-extensions", "-w", "-target", target, *inc]
    src = f'#include "{header}"\n'
    tu = ci.Index.create().parse("probe.c", args=args, unsaved_files=[("probe.c", src)])
    errs = [d.spelling for d in tu.diagnostics if d.severity >= ci.Diagnostic.Error]
    if errs:
        raise SystemExit(f"{target}: {errs[:3]}")
    found = {}
    for c in tu.cursor.walk_preorder():
        if c.kind == K.TYPEDEF_DECL and c.spelling in structs and c.spelling not in found:
            found[c.spelling] = c.underlying_typedef_type.get_canonical()
    missing = set(structs) - set(found)
    if missing:
        raise SystemExit(f"{target}: structs not found: {sorted(missing)}")
    return found


def leaves(t, base=0, path=""):
    """Yield (path, byte offset, kind, size) for every scalar leaf of type t."""
    t = t.get_canonical()
    if t.kind == T.RECORD:
        decl = t.get_declaration()
        members = []                                  # (offset, path, type)
        for ch in decl.get_children():
            if ch.kind == K.FIELD_DECL:
                if ch.is_anonymous():                 # anonymous struct/union member
                    continue                          # (seen as its record decl below)
                members.append((ch.get_field_offsetof() // 8, ch.spelling, ch.type))
            elif ch.kind in (K.UNION_DECL, K.STRUCT_DECL) and ch.is_anonymous():
                # offset of an anonymous member = offset of its first named field,
                # which the parent record resolves through the anonymous record
                first = next(f for f in ch.get_children() if f.kind == K.FIELD_DECL)
                members.append((t.get_offset(first.spelling) // 8, "", ch.type))
        if decl.kind == K.UNION_DECL:
            members = members[:1]                     # first member: the disc meaning
        for off, name, mt in members:
            sub = (f"{path}.{name}" if path else name) if name else path
            yield from leaves(mt, base + off, sub)
        return
    if t.kind in (T.CONSTANTARRAY,):
        el = t.element_type.get_canonical()
        for i in range(t.element_count):
            yield from leaves(el, base + i * el.get_size(), f"{path}[{i}]")
        return
    if t.kind == T.POINTER:
        yield (path, base, "ptr", t.get_size())
    elif t.kind in FLOATS:
        yield (path, base, "float", t.get_size())
    elif t.kind == T.ENUM:
        yield (path, base, "int", t.get_size())
    else:
        yield (path, base, "int", t.get_size())


def main() -> int:
    sys.path.insert(0, str(HERE))
    import build_mirror as bm, passes
    bm.gen_headers(passes.load_pointer_rules(REPO / "mirror" / "rules"))   # the real mirror tree
    for header, structs, outname, deps in TARGETS:
        emit(header, structs, REPO / "mirror" / "runtime" / outname, deps)
    return 0


def emit(header: str, structs: list, out_path: Path, deps: list) -> None:
    gc = parse("powerpc-unknown-eabi", False, header, structs)
    host = parse("x86_64-unknown-linux-gnu", True, header, structs)
    guard = "STAIRFAX_" + out_path.stem.upper() + "_H"
    out = [f"/* GENERATED by tools/mirror/gen_layouts.py from decomp/include/{header}.",
           " * DO NOT EDIT; re-run the generator.",
           " *",
           f" * GameCube -> host transcoders for on-disc structs (HOST-STRUCT): {', '.join(structs)}.",
           " * Each copies every scalar leaf from its GameCube (big-endian, 32-bit) offset",
           " * to its host offset. Pointer leaves are 4-byte disc values stored",
           " * zero-extended; relocating them is the caller's job. Unions transcode their",
           " * first member (the on-disc meaning). */",
           f"#ifndef {guard}", f"#define {guard}",
           "#include <stdint.h>", "#include <string.h>", f'#include "{header}"',
           *[f'#include "{d}"' for d in deps], ""]
    if not deps:
        out += ["static inline uint16_t sfx_be16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }",
                "static inline uint32_t sfx_be32(const uint8_t* p)",
                "{ return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }",
                ""]
    for s in structs:
        g = {p: (o, k, z) for p, o, k, z in leaves(gc[s])}
        h = {p: (o, k, z) for p, o, k, z in leaves(host[s])}
        if list(g) != list(h):
            raise SystemExit(f"{s}: leaf lists differ between targets")
        gsz, hsz = gc[s].get_size(), host[s].get_size()
        cov = set()
        for go, _, gz in g.values():
            span = set(range(go, go + gz))
            if cov & span:
                raise SystemExit(f"{s}: leaves overlap at 0x{min(cov & span):x}")
            cov |= span
        gaps = sorted(set(range(gsz)) - cov)
        if gaps and gaps[0] < max(cov):               # only trailing padding is allowed
            raise SystemExit(f"{s}: GC bytes not covered by any leaf: {[hex(x) for x in gaps[:8]]}")
        out += [f"#define SFX_GC_SIZEOF_{s} 0x{gsz:x}",
                f"_Static_assert(sizeof({s}) == 0x{hsz:x}, \"{s}: host layout changed; re-run gen_layouts.py\");",
                f"static inline void sfx_unpack_{s}(const uint8_t* gc, {s}* host)", "{",
                "    uint8_t* h = (uint8_t*)host;", f"    memset(host, 0, sizeof({s}));"]
        for p, (go, kind, gz) in g.items():
            ho, _, hz = h[p]
            if kind == "ptr":
                if gz != 4:
                    raise SystemExit(f"{s}.{p}: GC pointer is {gz} bytes")
                out.append(f"    {{ uintptr_t v = sfx_be32(gc + 0x{go:x}); memcpy(h + 0x{ho:x}, &v, sizeof v); }}"
                           f"  /* {p} */")
            elif gz != hz:
                raise SystemExit(f"{s}.{p}: scalar size differs ({gz} vs {hz})")
            elif gz == 1:
                out.append(f"    h[0x{ho:x}] = gc[0x{go:x}];  /* {p} */")
            elif gz == 2:
                out.append(f"    {{ uint16_t v = sfx_be16(gc + 0x{go:x}); memcpy(h + 0x{ho:x}, &v, 2); }}  /* {p} */")
            elif gz == 4:
                out.append(f"    {{ uint32_t v = sfx_be32(gc + 0x{go:x}); memcpy(h + 0x{ho:x}, &v, 4); }}  /* {p} */")
            else:
                raise SystemExit(f"{s}.{p}: unhandled scalar size {gz}")
        out += ["}", ""]
    out.append("#endif")
    out_path.write_text("\n".join(out) + "\n")
    print(f"wrote {out_path.relative_to(REPO)} ({', '.join(structs)})")


if __name__ == "__main__":
    raise SystemExit(main())
