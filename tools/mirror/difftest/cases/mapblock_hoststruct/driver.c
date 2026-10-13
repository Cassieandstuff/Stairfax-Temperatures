/* Print a canonical description of the block: counts and bounds, every relocated
 * pointer as the 4 marker bytes it points at, the texture-id table, both shaders
 * and both display-list records. */
#include "shim.h"

#if defined(DIFFTEST_MIRROR)
#include "main/map_block.h"
#include <stdlib.h>
static uint8_t gBlob[BLOB_SIZE];
void* mmAlloc(int size, int type, uintptr_t flag) { (void)type; (void)flag; return calloc(1, (size_t)size); }
void mm_free(void* p) { (void)p; }
void* loadAndDecompressDataFile(int fileId, void* dst, int off, u32 len, int* sizeOut, int id, int f)
{ (void)fileId; (void)off; (void)len; (void)sizeOut; (void)id; (void)f; memcpy(dst, gBlob, BLOB_SIZE); return dst; }
#include "snippet.c"                 /* mapBlockRelocatePointer + MapBlock_init */
#endif

static void mark(const char* what, const void* p) { printf("%s=%.4s ", what, (const char*)p); }

int main(void)
{
#if defined(DIFFTEST_MIRROR)
    build_blob(gBlob);
    MapBlockData* b = (MapBlockData*)stairfax_mapblock_load_unpacked(malloc(BLOB_SIZE + 0x40), BLOB_SIZE, 0, 0);
    MapBlock_init(b);
    printf("counts tex=%d sh=%d dl=%d vtx=%d polys=%d y=%d..%d t00=%g | ", b->textureCount, b->shaderCount,
           b->displayListCount, b->vertexCount, b->nPolygons, b->minY, b->maxY, b->transform[0][0]);
    mark("ply", b->gcPolygons); mark("grp", b->polygonGroups); mark("vtx", b->vertices); mark("col", b->vertexColors);
    mark("tex", b->vertexTexCoords); mark("imn", b->renderInstrsMain); mark("itr", b->renderInstrsTransp);
    mark("iwt", b->renderInstrsWater);
    printf("| texids=%#x,%#x ", b->textures[0].fileId, b->textures[1].fileId);
    for (int i = 0; i < 2; i++)
        printf("| sh%d tex=%d l0=%d aux=%d flags=%#x layers=%d ", i, b->shaders[i].textureId,
               b->shaders[i].layers[0].textureIndex, (int)b->shaders[i].auxTextureIndex, b->shaders[i].flags,
               b->shaders[i].layerCount);
    for (int i = 0; i < 2; i++) {
        MapBlockBoundsRec* r = &b->displayLists[i];
        printf("| dl%d ", i); mark("at", r->dlist);
        printf("sz=%#x minX=%d maxZ=%d shader=%d ", r->dlistSize, r->minX, r->maxZ, r->shaderIndex);
    }
#else
    static uint8_t b[BLOB_SIZE];
    build_blob(b);
    #define P(off) (b + rd32(b + (off)))
    union { uint32_t u; float f; } t00 = { rd32(b + B_TRANSFORM) };
    printf("counts tex=%d sh=%d dl=%d vtx=%d polys=%d y=%d..%d t00=%g | ", b[B_TEXCOUNT], b[B_SHCOUNT], b[B_DLCOUNT],
           rd16(b + B_VCOUNT), rd16(b + B_NPOLYS), (int16_t)rd16(b + B_MINY), (int16_t)rd16(b + B_MAXY), t00.f);
    mark("ply", P(B_POLYS)); mark("grp", P(B_GROUPS)); mark("vtx", P(B_VERTICES)); mark("col", P(B_COLORS));
    mark("tex", P(B_TEXCOORDS)); mark("imn", P(B_INSTR_MAIN)); mark("itr", P(B_INSTR_TRANSP));
    mark("iwt", P(B_INSTR_WATER));
    printf("| texids=%#x,%#x ", rd32(P(B_TEXTURES)), rd32(P(B_TEXTURES) + 4));
    for (int i = 0; i < 2; i++) {
        uint8_t* s = P(B_SHADERS) + i * S_SIZE;
        printf("| sh%d tex=%d l0=%d aux=%d flags=%#x layers=%d ", i, (int)rd32(s + S_TEXTUREID),
               (int)rd32(s + S_LAYER0), (int)rd32(s + S_AUX), rd32(s + S_FLAGS), s[S_LAYERCOUNT]);
    }
    for (int i = 0; i < 2; i++) {
        uint8_t* r = P(B_DLISTS) + i * R_SIZE;
        printf("| dl%d ", i); mark("at", b + rd32(r + R_DLIST));
        printf("sz=%#x minX=%d maxZ=%d shader=%d ", rd16(r + R_DLSIZE), (int16_t)rd16(r + R_MINX),
               (int16_t)rd16(r + R_MAXZ), r[R_SHADER]);
    }
#endif
    printf("\n");
    return 0;
}
