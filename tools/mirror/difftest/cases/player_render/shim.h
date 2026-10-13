/* Compile surface for the extracted playerRender (player.c) + objRender (objprint.c).
 *
 * Only the fields the two functions touch are named. Where the code pokes raw
 * byte offsets (the carried object at +0x0..+0x20/+0x46/+0xf8, the player state at
 * +0x3c4/+0x768.., the shader flags at +0x3c), the struct is a union over a raw byte
 * block with the named fields placed clear of those offsets, so both builds see the
 * same bytes. Every recorder below prints object *tags*, never addresses. */
#ifndef PLAYER_RENDER_SHIM_H
#define PLAYER_RENDER_SHIM_H

#include "difftest.h"
#include <stdio.h>

typedef uint8_t u8; typedef int8_t s8; typedef uint16_t u16; typedef int16_t s16;
typedef int32_t s32; typedef uint32_t u32; typedef float f32;

/* A global the rules retype outside the extracted ranges (player.c's
 * `int gPlayerHeldObject;`): the oracle keeps the decomp's int, the mirror gets
 * the generated rule's widened type. */
#if defined(DIFFTEST_MIRROR)
typedef uintptr_t PtrInt;
#else
typedef int PtrInt;
#endif

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

typedef struct ObjDef { f32 rootMotionScaleBase; } ObjDef;
typedef struct ModelState { f32 overrideWorldPosX, overrideWorldPosY, overrideWorldPosZ; } ModelState;

struct GameObject {
    union {
        u8 raw[0x100];                    /* +0x00..+0xff: raw-offset poke region */
    };
    int tag;                              /* test identity */
    u32 objectFlags;
    GameObject *ownerObj;
    struct {
        u32 flags; void *parent; void **dll; int romDefNo; void *hitVolumeTransforms;
        int classId; ObjDef *modelInstance; ModelState *modelState; s16 rotX;
        f32 localPosX, localPosY, localPosZ;
        f32 velocityX, velocityY, velocityZ;
        f32 worldPosX, worldPosY, worldPosZ;
    } anim;
    ObjModel *banks[2];
    s8 bankIndex;
    u8 childCount;
    GuestPtr childObjs[4];
    void *extra;                          /* PlayerState* for the player */
};

typedef struct PlayerState {
    union {
        u8 raw[0x800];
        struct {                          /* named fields, all below +0x100 */
            u32 flags360;
            GameObject *focusObject;
            GameObject *heldObj;
            struct { int controlMode; } baddie;
            u8 teleportAnimActive;
            f32 sinkOffsetY;
            s16 targetYaw;
            f32 knockbackTimer;
            u32 pendingFxFlags;
            struct { u8 knock : 4; } knockKindBits;
            f32 waterDepth;
            u8 surfaceType;
        };
        struct { u8 p3c4[0x3c4]; f32 footPoints[2][3]; };   /* real offset 0x3C4 */
    };
} PlayerState;

#define OBJECT_OBJFLAG_FREED        0x1
#define OBJECT_OBJFLAG_RENDERED     0x2
#define OBJECT_OBJFLAG_HIDDEN       0x4
#define OBJECT_OBJFLAG_PARENT_SLACK 0x1000
#define OBJANIM_FLAG_HIDDEN         0x1
#define PLAYER_FLAG_WATER_SPLASH_PENDING 0x20000LL
#define OBJPRINT_ACTIVE_BANK(o) \
    ((int *)((GameObject *)(o))->banks[((GameObject *)(o))->bankIndex])

typedef struct Shader {
    union {
        u8 raw[0x40];
        struct { u8 p[0x3c]; u32 flags; };                  /* read raw at +0x3c */
    };
    int layerCount;
    int tag;
} Shader;
typedef struct ModelFileHeader { int renderOpCount; Shader *ops; } ModelFileHeader;
typedef struct ObjModelFile { ModelFileHeader *file; } ObjModelFile;

typedef struct VehicleInterface {
    void (*handleRiderScale)(GameObject *, f32);
    void (*render)(GameObject *, int, int, int, int, int);
} VehicleInterface;
#define VEHICLE_INTERFACE(vehicle) ((VehicleInterface *)*((GameObject *)(vehicle))->anim.dll)

typedef struct PlayerShadowInterface { void (*renderObject)(GameObject *); } PlayerShadowInterface;
typedef struct EffectInterface {
    void (*spawnObject)(void *, int, void *, int, int, f32 *);
} EffectInterface;
typedef struct PlayerIntPair { int v[2]; } PlayerIntPair;

/* ---- the event log both builds print ---- */
static char gLog[4096]; static int gLogLen;
#define LOG(...) (gLogLen += snprintf(gLog + gLogLen, sizeof gLog - gLogLen, __VA_ARGS__))

static const PlayerIntPair sPlayerKnockFxIds = {{6, 8}};
static int lbl_803DC6C4[2] = {24, 26};
static u8 gPlayerSurfacePfxModeTable[4] = {0, 3, 6, 1};
static PtrInt gPlayerHeldObject;
static int gKrazoa;                        /* scripted playerHasKrazoaSpirit result */
static ObjModelFile gActiveModel;

static int arrayIndexOf(int *a, int n, int v) {
    for (int i = 0; i < n; i++) if (a[i] == v) return i;
    return -1;
}
static void playerSyncTransformToFocusObject(GameObject *o, PlayerState *s, GameObject *f,
                                             int a, int b, int c, int d, int e) {
    (void)s; (void)a; (void)b; (void)c; (void)d; (void)e;
    LOG(" sync(%d,%d)", o->tag, f->tag);
}
static void playerDrawTeleportAnim(GameObject *o) { LOG(" teleport(%d)", o->tag); }
static void shadowRender(GameObject *o) { LOG(" shadow(%d)", o->tag); }
static PlayerShadowInterface gShadowIf = { shadowRender }, *gShadowIfP = &gShadowIf;
static PlayerShadowInterface **gPlayerShadowInterface = &gShadowIfP;
static void objRenderModelAndHitVolumes(GameObject *o, int a, int b, int c, int d, f32 s) {
    LOG(" model(%d,%d,%d,%d,%d,y=%.2f,s=%.1f)", o->tag, a, b, c, d, o->anim.localPosY, s);
}
static void playerRenderPostEffects(GameObject *o, PlayerState *s, int a, int b, int c) {
    (void)s; LOG(" post(%d,%d,%d,%d)", o->tag, a, b, c);
}
static void ObjPath_GetPointWorldPositionArray(GameObject *o, int first, int n, f32 *out) {
    for (int i = 0; i < n * 3; i++) out[i] = (f32)(o->tag + first + i);
}
static void ObjPath_GetPointWorldPosition(GameObject *o, int pt, f32 *x, f32 *y, f32 *z, int f) {
    (void)f; *x = (f32)pt; *y = (f32)(pt * 2); *z = (f32)(o->tag + pt);
}
static int playerHasKrazoaSpirit(int a, int b) { (void)a; (void)b; return gKrazoa; }
static ObjModelFile *Obj_GetActiveModel(GameObject *o) { (void)o; return &gActiveModel; }
static Shader *ObjModel_GetRenderOp(ModelFileHeader *m, int i) { return &m->ops[i]; }
static void Shader_getLayer(Shader *s, int i) { LOG(" layer(%d,%d)", s->tag, i); }
static void objDoParticleFx(GameObject *o, f32 sc, int id, f32 r, void *p) {
    (void)p; LOG(" fx(%d,%.1f,%d,%.1f)", o->tag, sc, id, r);
}
static void spawnObject(void *o, int id, void *pfx, int f, int g, f32 *vel) {
    (void)pfx; (void)f; (void)g;
    LOG(" spawn(%d,%x,%.3f)", ((GameObject *)o)->tag, id, vel[0]);
}
static EffectInterface gFxIf = { spawnObject }, *gFxIfP = &gFxIf;
static EffectInterface **gPartfxInterface = &gFxIfP;

/* objRender's other callees */
void staffUpdateSegmentTransforms(uintptr_t staffArg, GameObject *objArg, uintptr_t modelArg,
                                  int a, int b, int c) {
    (void)objArg; (void)a; (void)b; (void)c;
    LOG(" staff(%d,%d)", ((GameObject *)staffArg)->tag, ((ObjModel *)modelArg)->tag);
}
void doNothing_beforeRenderObject(int x) { (void)x; }
void doNothing_afterRenderObject(void) {}
void objRenderModel(GameObject *o) { LOG(" objModel(%d)", o->tag); }
void objUpdateHitVolumeTransforms(GameObject *o) { LOG(" hitvol(%d)", o->tag); }

#endif
