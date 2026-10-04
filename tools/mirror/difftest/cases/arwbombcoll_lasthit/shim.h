/* Compile surface for ARWBombColl_update. Every object comes from mmAlloc, so
 * the oracle's (u32) stores and comparisons are lossless and the mirror's must
 * be full width. Engine calls are logged by object tag. */
#ifndef ARWBOMBCOLL_SHIM_H
#define ARWBOMBCOLL_SHIM_H
#include "difftest.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t u32; typedef uint16_t u16; typedef int16_t s16; typedef uint8_t u8;
typedef int8_t s8; typedef float f32;
#define STATIC_ASSERT(...)
#define OBJHITS_PRIORITY_HIT_COUNT 3
#define OBJANIM_FLAG_HIDDEN 0x4000
#define SFXTRIG_ar_ring_pickup 0x1
#define SFXTRIG_ar_largeenergy_pickup 0x2
#define SFXTRIG_ar_smallenergy_pickup 0x3

typedef struct ObjAnimComponent {
    s16 romDefNo;
    s16 rotX;
    u16 flags;
    u8 alpha;
    f32 localPosX, localPosY, localPosZ;
    void* hitReactState;
} ObjAnimComponent;
typedef struct GameObject {
    ObjAnimComponent anim;        /* first, as in the game: (ObjAnimComponent*)obj */
    void* extra;
    int tag;                      /* test identity */
} GameObject;

static char gLog[768];
#define LOG(...) snprintf(gLog + strlen(gLog), sizeof gLog - strlen(gLog), __VA_ARGS__)

static f32 timeDelta = 1.0f;
static GameObject* gArwing;
static GameObject* getArwing(void) { return gArwing; }
static int arwarwing_isExplodingOrWarping(GameObject* a) { (void)a; return 0; }
static void arwarwing_addScore(GameObject* a, int n) { LOG(" score(%d,%x)", a->tag, n); }
static void arwarwing_addBomb(GameObject* a) { LOG(" bomb(%d)", a->tag); }
static void arwarwing_upgradeLaserLevel(GameObject* a) { LOG(" laser(%d)", a->tag); }
static void arwarwing_incrementPickup6D8Count(GameObject* a) { (void)a; }
static void arwarwing_incrementPickup6D9Count(GameObject* a) { (void)a; }
static void arwarwing_incrementPickup6DACount(GameObject* a) { (void)a; }
static void arwarwing_incrementPickup6DBCount(GameObject* a) { (void)a; }
static void Obj_FreeObject(GameObject* o) { LOG(" free(%d)", o->tag); }
static void Obj_SetActiveModelIndex(GameObject* o, int i) { LOG(" model(%d,%d)", o->tag, i); }
static void Sfx_PlayFromObject(GameObject* o, int id) { LOG(" sfx(%d,%d)", o->tag, id); }
static void spawnExplosion(GameObject* o, f32 s, int a, int b, int c, int d, int e, int f, int g)
{
    (void)s; (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g;
    LOG(" boom(%d)", o->tag);
}
static void ObjHits_SetHitVolumeSlot(ObjAnimComponent* o, int slot, int a, int b)
{
    (void)o; (void)slot; (void)a; (void)b;
}
static void ObjHits_EnableObject(GameObject* o) { (void)o; }
static void ObjHits_DisableObject(GameObject* o) { LOG(" off(%d)", o->tag); }
/* defined in driver.c, after the snippet supplies ObjHitsPriorityState */
static int ObjHits_GetPriorityHit(GameObject* obj, GameObject** hit, int a, int b);
#endif
