#!/usr/bin/env python3
"""Regenerate mirror/rules/model_hoststruct.toml (the model HOST-STRUCT pass).

The rules point the model loader at mirror/runtime/stairfax_model.c and rewrite
code that addressed on-disc model records by GameCube byte offsets or 32-bit
strides. Each rewrite is matched against the CURRENT output of every other rule
and must occur exactly once on its line, so a decomp re-vendor or a rule change
that moves a target fails here instead of silently missing it.

  rm mirror/rules/model_hoststruct.toml && python3 tools/mirror/gen_model_hoststruct_rules.py
"""
import sys, re
sys.path.insert(0, 'tools/mirror')
import passes
from pathlib import Path
MC = 'decomp/src/main/model.c'; OD = 'decomp/src/main/objprint_dolphin.c'
rules = passes.load_pointer_rules(Path('mirror/rules'))
cur = {f: passes.apply_pointer_rules(Path(f).read_text(errors='replace'), rules, f)[0].splitlines()
       for f in (MC, OD)}
out = []
def rep(f, line, frm, to, why="", exact=False):
    cands = [line] if exact else [l for l in range(max(1, line - 12), line + 13) if frm in cur[f][l - 1]]
    if len(cands) != 1:
        raise SystemExit(f"{f}:~{line}: {len(cands)} candidate lines for {frm!r}: {cands}")
    line = cands[0]
    t = cur[f][line - 1]
    n = t.count(frm)
    if n != 1:
        raise SystemExit(f"{f}:{line}: {n} occurrences of {frm!r} in: {t.strip()}")
    cur[f][line - 1] = t.replace(frm, to)
    out.append((f, line, frm, to, why))
def rng(f, a, b, pairs, why=""):
    hit = 0
    for ln in range(a, b + 1):
        for frm, to in pairs:
            while frm in cur[f][ln - 1]:
                t = cur[f][ln - 1]
                if t.count(frm) > 1:                    # several on one line: one rule each, left to right
                    i = t.index(frm)
                    # make the match unique by including the text before it
                    ctx = t[max(0, i - 24):i + len(frm)]
                    while t.count(ctx) > 1:
                        ctx = t[max(0, t.index(ctx) - 8):t.index(ctx) + len(ctx)]
                    rep(f, ln, ctx, ctx[:-len(frm)] + to, why, exact=True)
                else:
                    rep(f, ln, frm, to, why, exact=True)
                hit += 1
    if not hit:
        raise SystemExit(f"{f}:{a}-{b}: nothing matched {pairs}")

# --- loader: transcode into the host layout -----------------------------------
rep(MC, 2675, "loadAndDecompressDataFile(MLDF_FILEID_MODELS_BIN_A, model, fileOffset, dataLen, 0, id, 0);",
    "model = stairfax_model_load_unpacked(model, dataLen + amapSize + 0x1f4 - 0x10, fileOffset, dataLen, id);",
    "load + transcode to host layout (mirror/runtime/stairfax_model.c)")
# --- relocation: display-list stride ---------------------------------------------
rep(MC, 2645, "i * 0x1c)", "i * sizeof(ModelDisplayListEntry))", "display-list entry stride")
rep(MC, 2646, "0x1c);", "sizeof(ModelDisplayListEntry));", "display-list entry stride")
rep(MC, 2205, "displayListIndex * 0x1c", "displayListIndex * sizeof(ModelDisplayListEntry)", "modelFileGetDisplayList")
# --- ObjModel_Load: texture table by name; it holds 4-byte texture IDs ------------
rep(MC, 2800, "h[0][0xf2]", "((ModelFileHeader*)h[0])->textureCount", "header raw offset 0xF2")
rng(MC, 2802, 2803, [("*(int*)(h[0] + 0x20)", "(intptr_t)((ModelFileHeader*)h[0])->textureIds")],
    "header raw offset 0x20 (textureIds)")
rep(MC, 2803, "*(void**)(", "*(s32*)(", "the slot is a 4-byte texture ID")
rep(MC, 2803, "= tex;", "= (s32)(intptr_t)tex;", "textureLoad(..., 1) returns an ID")
# --- ObjModel_Release: animationModelPtrs is a pointer table ----------------------
rel = next(i + 1 for i, t in enumerate(cur[MC]) if i > 2735 and "animationCount; z[1] += 4" in t)
rep(MC, rel, "z[1] += 4", "z[1] += sizeof(u8*)", "animationModelPtrs stride (the textureIds loop above keeps 4: IDs)", exact=True)
# --- ResolveRenderOpTextures: raw Shader offsets ----------------------------------
rng(MC, 2440, 2530, [("*(int*)(op + 0x34)", "((Shader*)op)->auxTextureIndex"),
                     ("*(int*)(op + 0x1c)", "(int)((Shader*)op)->unk1C")], "Shader raw offsets")
# --- ObjModel buffer layout: addresses in `pos`, stored through (int*) puns ------
rng(MC, 830, 995, [("*(int*)&", "*(uintptr_t*)&")], "ObjModel pointer fields written via int puns")
# blendAnimData holds ADDRESSES (normalBuf + offset): pointer-width elements
rep(MC, 772, "blendAnimCount * 4", "blendAnimCount * sizeof(intptr_t)", "blendAnimData sizing")
rep(MC, 976, "blendAnimCount * 4", "blendAnimCount * sizeof(intptr_t)", "blendAnimData sizing")
bl = next(i + 1 for i, t in enumerate(cur[MC]) if 2550 < i < 2560 and "*(int*)&((ObjModel*)dst)->normalBuf" in t)
rep(MC, bl, "*(int*)&((ObjModel*)dst)->normalBuf", "*(intptr_t*)&((ObjModel*)dst)->normalBuf", "address, not offset", exact=True)
# --- vertex-anim kernels: chunk offsets, job view --------------------------------
K = [("*(u8**)(job + 0xc)", "*(u8**)(job + SFX_JOB_ENTRIES)"),
     ("[0x73]", "[SFX_VC(vtxWords)]"), ("[0x6f]", "[SFX_VC(weightWords)]"),
     ("[0xe7]", "[SFX_VC_NEXT(vtxWords)]"), ("[0xe3]", "[SFX_VC_NEXT(weightWords)]"),
     ("[0x6c]", "[SFX_VC(mtxIdxA)]"), ("[0x6d]", "[SFX_VC(mtxIdxB)]"), ("[0x72]", "[SFX_VC(dstByteOffset)]"),
     ("+ 0x60)", "+ SFX_VC(srcDataOffset))"), ("+ 0x64)", "+ SFX_VC(weightStream))"),
     ("+ 0x70)", "+ SFX_VC(vtxCount))"), ("+ 0xd4)", "+ SFX_VC_NEXT(srcDataOffset))"),
     ("+ 0xd8)", "+ SFX_VC_NEXT(weightStream))"), ("i * 0x74", "i * sizeof(ModelVtxAnimChunk)")]
rng(MC, 2878, 3015, K, "vertex-anim chunk offsets")
gm = [i + 1 for i, t in enumerate(cur[MC]) if 2878 <= i <= 3015 and "gModelCacheBuffersA + 4)" in t]
for ln in gm:
    t = cur[MC][ln - 1]
    m = re.search(r"\*\(u8\*\*\)\(\(\w+\)gModelCacheBuffersA \+ 4\)", t)
    rep(MC, ln, m.group(0), "gModelCacheBuffersA[1]", "pointer-array element 1, not +4 bytes", exact=True)
# --- objprint_dolphin: header counts, kernel jobs, ObjModel raw offsets -----------
rep(OD, 1200, "hdr[0xf3] + hdr[0xf4]",
    "((ModelFileHeader*)hdr)->jointCount + ((ModelFileHeader*)hdr)->extraJointCount", "header raw offsets")
for a, b in ((2365, 2386), (2695, 2713)):
    rng(OD, a, b, [("m + 0x88", "SFX_VTXJOB(m)"), ("m + 0xac", "SFX_BLENDJOB(m)")], "kernel job views")
    rng(OD, a, b, [("((ModelFileHeader*)am)->jointBlendData", "((ObjModel*)am)->vertexAnimData")],
        "GC 0x40 of an ObjModel is vertexAnimData")
for ln in [i + 1 for i, t in enumerate(cur[OD]) if "(char*)am + 0x1c)" in t]:
    t = cur[OD][ln - 1]
    for m in re.finditer(r"\(\(\w+\*\)\(\(char\*\)am \+ 0x1c\)\)", t):
        pass
    while "((char*)am + 0x1c))" in cur[OD][ln - 1]:
        t = cur[OD][ln - 1]
        m = re.search(r"\(\(\w+\*\)\(\(char\*\)am \+ 0x1c\)\)", t)
        rep(OD, ln, m.group(0), "(((ObjModel*)am)->vtxBuf)", "GC 0x1c of an ObjModel is vtxBuf[]", exact=True)

lines = ['''# Model files in the HOST layout (pointer-slot survey item 2: HOST-STRUCT).
#
# mirror/runtime/stairfax_model.c loads a model file and transcodes every on-disc
# struct to its host layout (generated by tools/mirror/gen_layouts.py); these rules
# point the loader at it and rewrite code that addressed model records by
# GameCube byte offsets or 32-bit strides. Generated by tools/mirror/gen_model_hoststruct_rules.py from the current
# rule output and checked to match exactly once per line; comments say why.
''']
for f, ln, frm, to, why in out:
    q = lambda s: s.replace("\\", "\\\\").replace('"', '\\"')
    lines.append(f'[[text.replace]]\nfile = "{f}"\nline = {ln}  # {why}\nfrom = "{q(frm)}"\nto = "{q(to)}"\n')
lines.append('''# ObjModel.blendAnimData holds addresses (normalBuf + offset) read back as u8**.
[[text.replace]]
file = "decomp/include/main/model.h"
line = 390
from = "s32 *blendAnimData;"
to = "intptr_t *blendAnimData;"
''')
lines.append('''# ObjModel_RelocateModelData: every `*(u32*)&field` reads a pointer-width slot that
# holds a (re-based) file offset until it's relocated.
[[pointer.widen_cast]]
file = "decomp/src/main/model.c"
line = 2567
line_end = 2660
all = true
from = "u32*"
to = "uintptr_t*"
''')
Path('mirror/rules/model_hoststruct.toml').write_text("\n".join(lines))
print(len(out), "text.replace rules")
