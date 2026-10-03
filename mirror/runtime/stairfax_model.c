/* stairfax_model.c - load a model file into the host layout (HOST-STRUCT).
 *
 * On the GameCube a model file is used in place: ModelFileHeader sits at the start
 * of the buffer and ObjModel_RelocateModelData turns ~25 of its 4-byte file
 * offsets (and offsets in nested records) into absolute pointers. On a 64-bit host
 * those structs are wider, so every member after the first pointer moves. This
 * replaces the in-place load step of ObjModel_LoadModelData
 * (mirror/rules/model_hoststruct.toml retargets that line here):
 *
 *   1. the decomp's own loader reads the file into the GameCube-layout buffer;
 *   2. a new buffer is laid out as
 *        [host ModelFileHeader | pad]  [file body, shifted by `delta`]
 *        [anim area + slack, as the decomp sized it]  [host-layout nested tables]
 *      and every on-disc struct is transcoded field by field, big-endian ->
 *      host, by the generated gen_model_layout.h;
 *   3. offsets are re-based: body offsets move by `delta`; the nested tables'
 *      header fields point at their host copies. ObjModel_RelocateModelData
 *      then runs unchanged in spirit (its puns widened) and adds the base.
 *
 * The header stays at the start of the one allocation, so `header + dataSize`
 * (dataSize also moves by delta) and mm_free(header) keep working. `delta` is a
 * multiple of 32 so GX data in the body keeps its alignment. Leaf geometry
 * (vertices, display-list bytes, anim streams) is copied as-is, as the 32-bit
 * port's bridge does today. */
#include "stairfax_model.h"
#include "gen_model_layout.h"
#include "main/pi_dolphin_api.h"
#include "main/mldf_fileid.h"
#include "main/mm.h"
#include <string.h>

#define SFX_ALIGN(x, a) (((x) + ((a) - 1)) & ~(uintptr_t)((a) - 1))

/* header fields ObjModel_RelocateModelData rebases from file offsets */
static void rebase(uintptr_t* field, uintptr_t delta)
{
    if (*field != 0)
        *field += delta;
}

void* stairfax_model_load_unpacked(void* gcBuf, int gcBufSize, int fileOffset, int dataLen, int id)
{
    const uint8_t* gc = (const uint8_t*)gcBuf;
    ModelFileHeader hdr;
    uintptr_t delta, tail, off, total;
    uint8_t* raw;
    uint8_t* m;
    int i, nDl, nVtx, nBlend;
    uintptr_t gcRenderOps, gcDl, gcVtx, gcBlend, gcMorph, gcTex;
    uintptr_t oRenderOps, oDl, oVtx, oBlend, oMorph, oTex;

    loadAndDecompressDataFile(MLDF_FILEID_MODELS_BIN_A, gcBuf, fileOffset, dataLen, 0, id, 0);
    sfx_unpack_ModelFileHeader(gc, &hdr);

    /* nested GameCube tables (file offsets, before any rebasing) */
    gcRenderOps = (uintptr_t)hdr.renderOps;
    gcDl = (uintptr_t)hdr.displayLists;
    gcVtx = (uintptr_t)hdr.vertexAnimEntries;
    gcBlend = (uintptr_t)hdr.blendAnimEntries;
    gcMorph = (uintptr_t)hdr.morphTargetPtrs;
    gcTex = (uintptr_t)hdr.textureIds;
    nDl = hdr.displayListCount + hdr.shadowDisplayListCount;
    nVtx = hdr.vertexAnimCount;
    nBlend = hdr.blendAnimCount;

    delta = SFX_ALIGN(sizeof(ModelFileHeader) - SFX_GC_SIZEOF_ModelFileHeader, 32);
    tail = SFX_ALIGN(delta + (uintptr_t)gcBufSize, 32);  /* nested tables start here */
    off = tail;
    oRenderOps = gcRenderOps ? off : 0; off += SFX_ALIGN((uintptr_t)hdr.renderOpCount * sizeof(Shader), 32);
    oDl = gcDl ? off : 0;               off += SFX_ALIGN((uintptr_t)nDl * sizeof(ModelDisplayListEntry), 32);
    oVtx = gcVtx ? off : 0;             off += SFX_ALIGN((uintptr_t)nVtx * sizeof(ModelVtxAnimChunk), 32);
    oBlend = gcBlend ? off : 0;         off += SFX_ALIGN((uintptr_t)nBlend * sizeof(ModelVtxAnimChunk), 32);
    oMorph = gcMorph ? off : 0;         off += SFX_ALIGN((uintptr_t)hdr.morphTargetCount * sizeof(uintptr_t), 32);
    oTex = gcTex ? off : 0;             off += SFX_ALIGN((uintptr_t)hdr.textureCount * sizeof(intptr_t), 32);
    total = off;

    raw = (uint8_t*)mmAlloc((int)(total + 16), 9, 0);
    m = (uint8_t*)SFX_ALIGN((uintptr_t)raw, 16);         /* as the decomp's roundUpTo16 */
    memset(m, 0, delta);
    memcpy(m + delta + SFX_GC_SIZEOF_ModelFileHeader, gc + SFX_GC_SIZEOF_ModelFileHeader,
           (size_t)gcBufSize - SFX_GC_SIZEOF_ModelFileHeader);

    /* body offsets move with the body */
    rebase((uintptr_t*)&hdr.hitVolumes, delta);
    rebase((uintptr_t*)&hdr.jointData, delta);
    rebase((uintptr_t*)&hdr.unk18, delta);
    rebase((uintptr_t*)&hdr.unk1C, delta);
    rebase((uintptr_t*)&hdr.jointBlendData, delta);
    rebase((uintptr_t*)&hdr.extraJointDefs, delta);
    rebase((uintptr_t*)&hdr.vertices, delta);
    rebase((uintptr_t*)&hdr.normals, delta);
    rebase((uintptr_t*)&hdr.colors, delta);
    rebase((uintptr_t*)&hdr.texCoords, delta);
    rebase((uintptr_t*)&hdr.instrs, delta);
    rebase((uintptr_t*)&hdr.vertexAnimBase, delta);
    rebase((uintptr_t*)&hdr.blendAnimBase, delta);
    rebase((uintptr_t*)&hdr.collisionTriangles, delta);
    rebase((uintptr_t*)&hdr.collisionBlocks, delta);
    hdr.dataSize += (int32_t)delta;                      /* anim data at header + dataSize */

    /* nested tables: transcode into host layout at the tail */
    for (i = 0; i < hdr.renderOpCount && gcRenderOps; i++)
        sfx_unpack_Shader(gc + gcRenderOps + (uintptr_t)i * SFX_GC_SIZEOF_Shader,
                          (Shader*)(m + oRenderOps) + i);
    for (i = 0; i < nDl && gcDl; i++) {
        ModelDisplayListEntry* e = (ModelDisplayListEntry*)(m + oDl) + i;
        sfx_unpack_ModelDisplayListEntry(gc + gcDl + (uintptr_t)i * SFX_GC_SIZEOF_ModelDisplayListEntry, e);
        rebase((uintptr_t*)e, delta);                    /* dlist (offset 0) is a body offset */
    }
    for (i = 0; i < nVtx && gcVtx; i++)
        sfx_unpack_ModelVtxAnimChunk(gc + gcVtx + (uintptr_t)i * SFX_GC_SIZEOF_ModelVtxAnimChunk,
                                     (ModelVtxAnimChunk*)(m + oVtx) + i);
    for (i = 0; i < nBlend && gcBlend; i++)
        sfx_unpack_ModelVtxAnimChunk(gc + gcBlend + (uintptr_t)i * SFX_GC_SIZEOF_ModelVtxAnimChunk,
                                     (ModelVtxAnimChunk*)(m + oBlend) + i);
    for (i = 0; i < hdr.morphTargetCount && gcMorph; i++) {
        uintptr_t v = sfx_be32(gc + gcMorph + (uintptr_t)i * 4);
        ((uintptr_t*)(m + oMorph))[i] = v ? v + delta : 0;
    }
    for (i = 0; i < hdr.textureCount && gcTex; i++)       /* s32 ids, later Texture* */
        ((intptr_t*)(m + oTex))[i] = (int32_t)sfx_be32(gc + gcTex + (uintptr_t)i * 4);

    hdr.renderOps = (Shader*)oRenderOps;
    hdr.displayLists = (u8*)oDl;
    hdr.vertexAnimEntries = (u8*)oVtx;
    hdr.blendAnimEntries = (u8*)oBlend;
    hdr.morphTargetPtrs = (u8**)oMorph;
    hdr.textureIds = (s32*)oTex;
    memcpy(m, &hdr, sizeof hdr);

    mm_free(gcBuf);
    return m;
}
