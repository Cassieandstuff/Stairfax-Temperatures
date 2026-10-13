/* stairfax_model: load a model file into the HOST layout (HOST-STRUCT). See
 * stairfax_model.c and mirror/rules/model_hoststruct.toml. */
#ifndef STAIRFAX_MODEL_H
#define STAIRFAX_MODEL_H
#include <stddef.h>

void* stairfax_model_load_unpacked(void* gcBuf, int gcBufSize, int fileOffset, int dataLen, int id);

/* Host offsets for code that addressed on-disc model records by GameCube byte
 * offsets (model_hoststruct.toml rewrites those constants to these). */
#define SFX_VC(f)      offsetof(ModelVtxAnimChunk, f)
#define SFX_VC_NEXT(f) (sizeof(ModelVtxAnimChunk) + offsetof(ModelVtxAnimChunk, f))

/* The vertex-anim kernels take a "job": a view into the middle of the header,
 * at GameCube 0x88 (vertex anim) or 0xAC (blend anim), read as
 *   job + 2 : u16 count     job[6] : u8 GQR scale     job + 0xC : entries pointer.
 * On the host the count and byte keep their relative offsets; the pointer moves
 * (alignment), so it's named here. stairfax_model.c asserts both views agree. */
#define SFX_VTXJOB(m)   ((u8*)&((ModelFileHeader*)(m))->unk86[2])
#define SFX_BLENDJOB(m) ((u8*)((ModelFileHeader*)(m))->unkAC)
#define SFX_JOB_ENTRIES (offsetof(ModelFileHeader, vertexAnimEntriesRaw) - \
                         (offsetof(ModelFileHeader, unk86) + 2))
#endif
