/* Compile surface for spitembeam_update. Every object comes from mmAlloc, so
 * the oracle's (int) handle store is lossless and the mirror's must be full
 * width. Shop calls are logged by object tag. */
#ifndef USERDATA1_SHIM_H
#define USERDATA1_SHIM_H
#include "difftest.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t u32; typedef uint16_t u16; typedef int16_t s16; typedef uint8_t u8;
typedef int32_t s32; typedef float f32;
#define OBJANIM_FLAG_HIDDEN 0x4000
#define OBJECT_OBJFLAG_UPDATE_DISABLED 0x8

typedef struct GameObject GameObject;
typedef struct ShopInterface {
    int (*isItemAvailable)(GameObject* shop, int slot);
    int (*isItemBought)(GameObject* shop, int slot);
} ShopInterface;
typedef struct ObjAnimComponent {
    s16 flags;
    void* placementData;
    ShopInterface** dll;
} ObjAnimComponent;
struct GameObject {
    ObjAnimComponent anim;
    u16 objectFlags;
#if defined(DIFFTEST_MIRROR)
    intptr_t userData1;           /* gameobject.toml */
#else
    s32 userData1;
#endif
    int tag;                      /* test identity */
};
#define SHOP_INTERFACE(shop) ((ShopInterface*)*((GameObject*)(shop))->anim.dll)

typedef struct SpitembeamPlacement { s16 itemIndex; } SpitembeamPlacement;
typedef struct ObjTextureRuntimeSlot { s16 offsetS; } ObjTextureRuntimeSlot;

static char gLog[512];
#define LOG(...) snprintf(gLog + strlen(gLog), sizeof gLog - strlen(gLog), __VA_ARGS__)

static GameObject* gShop;
static int gSearches;
static GameObject* objGetNearestTypeTo(int group, GameObject* from, f32* radius)
{
    (void)from; (void)radius;
    gSearches++;
    return group == 9 ? gShop : NULL;
}
static ObjTextureRuntimeSlot gTex[3];
static ObjTextureRuntimeSlot* objFindTexture(GameObject* obj, int a, int b)
{
    (void)a; (void)b;
    return &gTex[obj->tag - 10];
}
#endif
