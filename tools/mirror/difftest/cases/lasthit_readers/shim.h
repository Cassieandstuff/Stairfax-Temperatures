/* Compile surface for the lastHitObject readers. Every object comes from
 * mmAlloc, so the oracle's (u32) stores and comparisons are lossless and the
 * mirror's must be full width. Engine calls are recorded as object tags. */
#ifndef LASTHIT_SHIM_H
#define LASTHIT_SHIM_H
#include "difftest.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t u32; typedef uint16_t u16; typedef int16_t s16; typedef uint8_t u8;
typedef int8_t s8; typedef float f32;
#define STATIC_ASSERT(...)
#define OBJHITS_PRIORITY_HIT_COUNT 3
#define SFXTRIG_lummy311 0x311

typedef struct ObjAnimComponent {
    s16 romDefNo;
    u8 alpha;
    s16 rotX, rotY;
    f32 velocityX, velocityY, velocityZ;
    f32 localPosX, localPosY, localPosZ;
    void* placementData;
    void* hitReactState;
} ObjAnimComponent;
typedef struct GameObject {
    ObjAnimComponent anim;        /* first, as in the game: (ObjAnimComponent*)obj */
    void* extra;
#if defined(DIFFTEST_MIRROR)
    intptr_t userData1;           /* gameobject.toml */
#else
    int userData1;
#endif
    int tag;                      /* test identity */
} GameObject;

typedef struct Dll19DPlacement { s8 variant; } Dll19DPlacement;
typedef struct Dll19DState { s16 despawnTimer; } Dll19DState;
typedef struct PartFxSpawnParams { f32 posX, posY, posZ, scale; } PartFxSpawnParams;

static char gLog[512];
#define LOG(...) snprintf(gLog + strlen(gLog), sizeof gLog - strlen(gLog), __VA_ARGS__)

typedef struct PartFxInterface {
    void (*spawnObject)(void* obj, int id, void* params, int mode, int a, void* b);
} PartFxInterface;
static int gSpawns;
static void spawnObject(void* obj, int id, void* params, int mode, int a, void* b)
{
    (void)params; (void)mode; (void)a; (void)b;
    gSpawns++;
    if (gSpawns == 1 || id != 0x715) LOG(" fx(%d,%x)", ((GameObject*)obj)->tag, id);
}
static PartFxInterface gPartfx = { spawnObject };
static PartFxInterface* gPartfxPtr = &gPartfx;
static PartFxInterface** gPartfxInterface = &gPartfxPtr;

static f32 timeDelta = 1.0f;
static GameObject* gPlayer;
static GameObject* gTricky;
static GameObject* Obj_GetPlayerObject(void) { return gPlayer; }
static GameObject* getTrickyObject(void) { return gTricky; }
static void Obj_FreeObject(GameObject* o) { LOG(" free(%d)", o->tag); }
static void Sfx_PlayFromObject(GameObject* o, int id) { LOG(" sfx(%d,%x)", o->tag, id); }
static void objMove(GameObject* o, f32 x, f32 y, f32 z)
{
    o->anim.localPosX += x; o->anim.localPosY += y; o->anim.localPosZ += z;
}
static s16 getAngle(f32 a, f32 b) { (void)a; (void)b; return 0; }
static void ObjHits_SetHitVolumeSlot(ObjAnimComponent* o, int slot, int a, int b)
{
    (void)o; (void)slot; (void)a; (void)b;
}
static void ObjHits_EnableObject(GameObject* o) { (void)o; }
#endif
