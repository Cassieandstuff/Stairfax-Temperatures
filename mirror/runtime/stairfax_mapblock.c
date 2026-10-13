/* stairfax_mapblock.c - load a map block into the host layout (HOST-STRUCT).
 *
 * The map-block twin of stairfax_model.c. On the GameCube a block file is used
 * in place: MapBlockData sits at the start and MapBlock_init turns its 4-byte
 * file offsets (and each display-list record's dlist) into pointers. On a 64-bit
 * host MapBlockData and its pointer-carrying records are wider. This replaces the
 * load step of MapBlock_loadFromFile (mirror/rules/mapblock_hoststruct.toml):
 *
 *   [host MapBlockData | pad]  [file body, shifted by `delta`]
 *   [host texture table]  [host Shader table]  [host display-list table]
 *
 * Body offsets move by `delta`; the three tables' header fields point at their
 * host copies; MapBlock_init then relocates as on the GameCube. Records without
 * pointers (MapTriIndex, MapTriGroup, vertices, render bitstreams) stay in file
 * layout, as the 32-bit port leaves them. `delta` is a multiple of 32 to keep GX
 * data aligned, and everything is one allocation, so freeing the block frees all. */
#include "stairfax_mapblock.h"
#include "gen_mapblock_layout.h"
#include "main/pi_dolphin_api.h"
#include "main/mldf_fileid.h"
#include "main/mm.h"
#include <string.h>

#define SFX_ALIGN(x, a) (((x) + ((a) - 1)) & ~(uintptr_t)((a) - 1))

static void rebase(void* field, uintptr_t delta)
{
    uintptr_t v;
    memcpy(&v, field, sizeof v);
    if (v != 0)
        v += delta;
    memcpy(field, &v, sizeof v);
}

void* stairfax_mapblock_load_unpacked(void* gcBuf, int gcBufSize, int blockOff, int compressedLen)
{
    const uint8_t* gc = (const uint8_t*)gcBuf;
    MapBlockData hdr;
    uintptr_t delta, off, total, oTex, oSh, oDl, gcTex, gcSh, gcDl;
    uint8_t* m;
    int i;

    loadAndDecompressDataFile(MLDF_FILEID_BLOCKS_BIN_A, gcBuf, blockOff, compressedLen, 0, 0, 0);
    sfx_unpack_MapBlockData(gc, &hdr);
    gcTex = (uintptr_t)hdr.textures;
    gcSh = (uintptr_t)hdr.shaders;
    gcDl = (uintptr_t)hdr.displayLists;

    delta = SFX_ALIGN(sizeof(MapBlockData) - SFX_GC_SIZEOF_MapBlockData, 32);
    off = SFX_ALIGN(delta + (uintptr_t)gcBufSize, 32);
    oTex = gcTex ? off : 0; off += SFX_ALIGN((uintptr_t)hdr.textureCount * sizeof(MapTextureRef), 32);
    oSh = gcSh ? off : 0;   off += SFX_ALIGN((uintptr_t)hdr.shaderCount * sizeof(Shader), 32);
    oDl = gcDl ? off : 0;   off += SFX_ALIGN((uintptr_t)hdr.displayListCount * sizeof(MapBlockBoundsRec), 32);
    total = off;

    m = (uint8_t*)mmAlloc((int)total, 5, 0);
    if (m == NULL) {
        mm_free(gcBuf);
        return NULL;
    }
    memset(m, 0, delta);
    memcpy(m + delta + SFX_GC_SIZEOF_MapBlockData, gc + SFX_GC_SIZEOF_MapBlockData,
           (size_t)gcBufSize - SFX_GC_SIZEOF_MapBlockData);

    rebase(&hdr.gcPolygons, delta);
    rebase(&hdr.polygonGroups, delta);
    rebase(&hdr.vertices, delta);
    rebase(&hdr.vertexColors, delta);
    rebase(&hdr.vertexTexCoords, delta);
    rebase(&hdr.renderInstrsMain, delta);
    rebase(&hdr.renderInstrsTransp, delta);
    rebase(&hdr.renderInstrsWater, delta);
    hdr.size += (u32)delta;

    for (i = 0; i < hdr.textureCount && gcTex; i++) {   /* s32 file ids; texture.c loads them */
        MapTextureRef* r = (MapTextureRef*)(m + oTex) + i;
        memset(r, 0, sizeof *r);
        r->fileId = (int32_t)sfx_be32(gc + gcTex + (uintptr_t)i * 4);
    }
    for (i = 0; i < hdr.shaderCount && gcSh; i++)
        sfx_unpack_Shader(gc + gcSh + (uintptr_t)i * SFX_GC_SIZEOF_Shader, (Shader*)(m + oSh) + i);
    for (i = 0; i < hdr.displayListCount && gcDl; i++) {
        MapBlockBoundsRec* r = (MapBlockBoundsRec*)(m + oDl) + i;
        sfx_unpack_MapBlockBoundsRec(gc + gcDl + (uintptr_t)i * SFX_GC_SIZEOF_MapBlockBoundsRec, r);
        rebase(&r->dlist, delta);
    }

    hdr.textures = (MapTextureRef*)oTex;
    hdr.shaders = (Shader*)oSh;
    hdr.displayLists = (MapBlockBoundsRec*)oDl;
    memcpy(m, &hdr, sizeof hdr);

    mm_free(gcBuf);
    return m;
}
