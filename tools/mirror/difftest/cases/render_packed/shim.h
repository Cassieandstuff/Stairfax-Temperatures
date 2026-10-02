/* Minimal compile surface for the extracted modelRenderInterpolateRootTransform.
 * Only the types and the ObjAnimState fields the function reads; the real
 * objanim_internal.h layout is irrelevant here because the function accesses the
 * state through named fields, never by packed address. */
#ifndef RENDER_PACKED_SHIM_H
#define RENDER_PACKED_SHIM_H

#include "difftest.h"   /* mmAlloc (low-mem oracle / malloc mirror) + stdint */
#include <math.h>       /* floorf */

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;
typedef float    f32;

typedef struct ObjAnimFrameCommand ObjAnimFrameCommand;   /* opaque */

typedef struct ObjAnimState {
    f32 framePhase;
    u8 *frameStreamCursor;               /* bitstream window A */
    ObjAnimFrameCommand *moveFrameData;  /* +4: u16 component descriptors */
    u16 frameStreamStride;               /* bytes from window A to window B */
} ObjAnimState;

#endif /* RENDER_PACKED_SHIM_H */
