/* Compile surface for the extracted objRender.
 *
 * The oracle must behave like the REAL 32-bit build, which needs more than a
 * low-memory heap here: objRender walks obj->childObjs[] with `walk += 4`, which is
 * correct only when the array holds 4-byte pointers. So under
 * DIFFTEST_ORACLE_LOWMEM the stride-walked array uses the GameCube's 32-bit layout
 * (u32 slots). The mirror uses native pointer slots, which is exactly why its
 * stride must become sizeof(void*). The negative control (no low-mem define)
 * gets native slots + the original stride + a high heap: both hazards at once. */
#ifndef OBJRENDER_SHIM_H
#define OBJRENDER_SHIM_H

#include "difftest.h"

typedef uint8_t u8; typedef int8_t s8; typedef uint16_t u16;
typedef int32_t s32; typedef uint32_t u32; typedef float f32;

typedef struct GameObject GameObject;
typedef struct ObjModel { int tag; } ObjModel;
typedef struct ObjectInterface { void *render; } ObjectInterface;
typedef ObjectInterface **ObjectInterfaceHandle;

#if defined(DIFFTEST_ORACLE_LOWMEM)
typedef u32 GuestPtr;                     /* 32-bit build: pointer slot = 4 bytes */
#  define TO_GUEST(p) ((GuestPtr)(uintptr_t)(p))
#else
typedef GameObject *GuestPtr;             /* host: native pointer slot */
#  define TO_GUEST(p) ((GuestPtr)(p))
#endif

struct GameObject {
    u32 objectFlags;
    GameObject *ownerObj;
    struct {
        u32 flags; void *parent; void *dll; int romDefNo; void *hitVolumeTransforms;
        int classId;
    } anim;
    ObjModel *banks[2];
    s8 bankIndex;
    int tag;                              /* test identity */
    u8 childCount;
    GuestPtr childObjs[4];               /* walked with `walk += stride` */
};

#define OBJECT_OBJFLAG_FREED    0x1
#define OBJECT_OBJFLAG_RENDERED 0x2
#define OBJECT_OBJFLAG_HIDDEN   0x4
#define OBJANIM_FLAG_HIDDEN     0x1
/* objprint_internal.h: ((int*)OBJPRINT_BANK_TABLE(obj)[OBJPRINT_ACTIVE_BANK_INDEX(obj)]) */
#define OBJPRINT_ACTIVE_BANK(o) \
    ((int *)((GameObject *)(o))->banks[((GameObject *)(o))->bankIndex])

static int gCalls; static int gSeen[8];
void staffUpdateSegmentTransforms(uintptr_t staffArg, GameObject *objArg, uintptr_t modelArg,
                                  int a, int b, int c) {
    (void)objArg; (void)a; (void)b; (void)c;
    gSeen[gCalls * 2]     = ((GameObject *)staffArg)->tag;   /* faults if truncated */
    gSeen[gCalls * 2 + 1] = ((ObjModel *)modelArg)->tag;
    gCalls++;
}
void doNothing_beforeRenderObject(int x) { (void)x; }
void doNothing_afterRenderObject(void) {}
void objRenderModel(GameObject *o) { (void)o; }
void objUpdateHitVolumeTransforms(GameObject *o) { (void)o; }
void playerRender(int o, int a, int b, int c, int d, int f) { (void)o;(void)a;(void)b;(void)c;(void)d;(void)f; }

#endif
