/* Print a canonical description of the model: counts, every relocated pointer as
 * the 4 marker bytes it points at, display-list sizes, the render op, the vertex
 * anim chunk and the texture ids. */
#include "shim.h"

#if defined(DIFFTEST_MIRROR)
#include "main/model.h"
#include <stdlib.h>
static uint8_t gBlob[BLOB_SIZE];
void* mmAlloc(int size, int type, uintptr_t flag) { (void)type; (void)flag; return calloc(1, (size_t)size); }
void mm_free(void* p) { (void)p; }   /* the test's buffers live until exit */
void* loadAndDecompressDataFile(int fileId, void* dst, int off, u32 len, int* sizeOut, int id, int f)
{ (void)fileId; (void)off; (void)len; (void)sizeOut; (void)id; (void)f; memcpy(dst, gBlob, BLOB_SIZE); return dst; }
#include "snippet.c"                 /* the rule-rewritten ObjModel_RelocateModelData */
#endif

static void mark(const char* what, const uint8_t* p) { printf("%s=%.4s ", what, (const char*)p); }

int main(void)
{
#if defined(DIFFTEST_MIRROR)
    build_blob(gBlob);
    uint8_t* gcBuf = (uint8_t*)malloc(BLOB_SIZE + 0x200);
    ModelFileHeader* h = (ModelFileHeader*)stairfax_model_load_unpacked(gcBuf, BLOB_SIZE, 0, BLOB_SIZE, 7);
    uint8_t* m = (uint8_t*)h;
    ObjModel_RelocateModelData(m);
    ModelDisplayListEntry* dl = (ModelDisplayListEntry*)h->displayLists;
    ModelVtxAnimChunk* c = (ModelVtxAnimChunk*)h->vertexAnimEntries;
    printf("counts dl=%d+%d ops=%d morph=%d tex=%d joints=%d vtxanim=%d | ", h->displayListCount,
           h->shadowDisplayListCount, h->renderOpCount, h->morphTargetCount, h->textureCount, h->jointCount,
           h->vertexAnimCount);
    mark("vtx", h->vertices); mark("nrm", h->normals);
    mark("dl0", dl[0].dlist); printf("sz0=%#x ", dl[0].dlistSize);
    mark("dl1", dl[1].dlist); printf("sz1=%#x ", dl[1].dlistSize);
    mark("mt0", h->morphTargetPtrs[0]); mark("mt1", h->morphTargetPtrs[1]);
    printf("| op tex=%d l0=%d aux=%d ind=%d flags=%#x layers=%d ", h->renderOps[0].textureId,
           h->renderOps[0].layers[0].textureIndex, (int)h->renderOps[0].auxTextureIndex,
           h->renderOps[0].indTextureId, h->renderOps[0].flags, h->renderOps[0].layerCount);
    printf("| chunk src=%#x mtx=%d,%d ww=%d vc=%#x dst=%d vw=%d ", c->srcDataOffset, c->mtxIdxA, c->mtxIdxB,
           c->weightWords, c->vtxCount, c->dstByteOffset, c->vtxWords);
    mark("wst", h->vertexAnimBase + (uintptr_t)c->weightStream);
    printf("| texids=%#x,%#x ", h->textureIds[0], h->textureIds[1]);
    mark("anim", m + h->dataSize);                  /* anim data lives at header + dataSize */
#else
    static uint8_t b[BLOB_SIZE];
    build_blob(b);
    #define P(off) (b + rd32(b + (off)))
    printf("counts dl=%d+%d ops=%d morph=%d tex=%d joints=%d vtxanim=%d | ", b[H_DLCOUNT], b[H_SHADOWDLCOUNT],
           b[H_RENDEROPCOUNT], b[H_MORPHCOUNT], b[H_TEXCOUNT], b[H_JOINTCOUNT], rd16(b + H_VTXANIMCOUNT));
    mark("vtx", P(H_VERTICES)); mark("nrm", P(H_NORMALS));
    uint8_t* dl = P(H_DISPLAYLISTS);
    mark("dl0", b + rd32(dl)); printf("sz0=%#x ", rd16(dl + 4));
    mark("dl1", b + rd32(dl + DL_SIZE)); printf("sz1=%#x ", rd16(dl + DL_SIZE + 4));
    uint8_t* mt = P(H_MORPHPTRS);
    mark("mt0", b + rd32(mt)); mark("mt1", b + rd32(mt + 4));
    uint8_t* op = P(H_RENDEROPS);
    printf("| op tex=%d l0=%d aux=%d ind=%d flags=%#x layers=%d ", (int)rd32(op + S_TEXTUREID),
           (int)rd32(op + S_LAYER0), (int)rd32(op + S_AUX), (int)rd32(op + S_IND), rd32(op + S_FLAGS),
           op[S_LAYERCOUNT]);
    uint8_t* c = P(H_VTXANIMENTRIES);
    printf("| chunk src=%#x mtx=%d,%d ww=%d vc=%#x dst=%d vw=%d ", rd32(c + C_SRC), c[C_MTXA], c[C_MTXB],
           c[C_WW], rd16(c + C_VCOUNT), c[C_DST], c[C_VW]);
    mark("wst", P(H_VTXANIMBASE) + rd32(c + C_WEIGHT));
    uint8_t* t = P(H_TEXIDS);
    printf("| texids=%#x,%#x ", rd32(t), rd32(t + 4));
    mark("anim", b + rd32(b + H_DATASIZE));
#endif
    printf("\n");
    return 0;
}
