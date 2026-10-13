/* Compile surface for the hit-state views case. Objects and the entry arena
 * come from mmAlloc, so the oracle's (u32) pointer stores are lossless and the
 * mirror's must be full width. */
#ifndef HITSTATE_SHIM_H
#define HITSTATE_SHIM_H
#include "difftest.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t u32; typedef uint16_t u16; typedef int16_t s16; typedef uint8_t u8;
typedef int8_t s8; typedef float f32;
#define STATIC_ASSERT(...)
#define OBJHITS_PRIORITY_HIT_COUNT 3
#define OBJHITS_SHAPE_RESET_MODE_MASK 0x30

typedef struct ObjHitReactEntry ObjHitReactEntry;
typedef struct ObjAnimBank ObjAnimBank;
typedef struct ObjHitReactState ObjHitReactState;
typedef struct ObjAnimComponent {
    void* hitReactState;
    f32 localPosX, localPosY, localPosZ;
} ObjAnimComponent;
typedef struct GameObject {
    ObjAnimComponent anim;        /* first, as in the game: (ObjAnimComponent*)obj */
    int tag;                      /* test identity */
} GameObject;

#if !defined(DIFFTEST_MIRROR)
/* The decomp's ObjHitReactState with a 4-byte `entries`: its GameCube layout,
 * where every field coincides with ObjHitsPriorityState's. */
typedef struct ObjHitReactState {
  int activeHit;
  s16 activeEntryByteCount;
  s16 entryBufferByteCapacity;
  ObjHitReactEntry * __ptr32 __uptr entries;
  u8 pad0C[0x58 - 0x0C];
  s16 resetFrameCount;
  u8 pad5A[0x60 - 0x5A];
  s16 flags;
  u8 shapeFlags;
  u8 pad63[0xAE - 0x63];
  u8 activeHitboxMode;
  u8 resetHitboxMode;
} ObjHitReactState;
static int roundUpTo8(int v) { return (v + 7) & ~7; }
#else
static uintptr_t roundUpTo8(uintptr_t v) { return (v + 7) & ~(uintptr_t)7; }
#endif

/* defined in driver.c, after the snippet supplies the mirror's ObjHitReactState */
static void ObjHitReact_LoadMoveEntries(ObjAnimComponent* objAnim, ObjAnimBank* bank, int objType,
                                        ObjHitReactState* hitState, int a, int b);
#endif
