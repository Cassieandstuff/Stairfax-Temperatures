/* Driver for the extracted objRender -> playerRender path.
 *
 * Frame 1: the player (no DLL, romDefNo 0) renders through objRender into
 *   playerRender with a carried object (raw-offset pokes + its VEHICLE_INTERFACE
 *   render), a Krazoa-spirit shader latched into the int global
 *   gPlayerHeldObject, knockback/footstep FX, and a staff child.
 * Frame 2: the spirit is gone, so playerRender clears the shader's flag through
 *   gPlayerHeldObject (`*(u32*)((char*)gPlayerHeldObject + 0x3c)`) and zeroes it.
 * Frame 3: an object WITH a DLL dispatches through its interface render.
 * Every object comes from mmAlloc (low heap in the oracle, high in the mirror). */
#include "shim.h"
#include "snippet.c"
#include <string.h>

static void *zalloc(unsigned n) { void *p = mmAlloc(n, 0, 0); memset(p, 0, n); return p; }

static GameObject *mk(int tag) {
    GameObject *o = (GameObject *)zalloc(sizeof(GameObject));
    o->tag = tag;
    return o;
}

static void vehRender(GameObject *v, int a, int b, int c, int d, int f) {
    LOG(" vehRender(%d,%d,%d,%d,%d,%d,yaw=%d,x=%.2f,y=%.2f,z=%.2f)", v->tag, a, b, c, d, f,
        *(s16 *)v->raw, *(f32 *)(v->raw + 0xc), *(f32 *)(v->raw + 0x10), *(f32 *)(v->raw + 0x14));
}
static void vehScale(GameObject *v, f32 s) { LOG(" scale(%d,%.1f)", v->tag, s); }
static void dllRender(GameObject *o, int a, int b, int c, int d, int f) {
    LOG(" dllRender(%d,%d,%d,%d,%d,%d)", o->tag, a, b, c, d, f);
}

int main(void)
{
    GameObject *player = mk(1), *veh = mk(7), *staff = mk(9), *npc = mk(20);
    PlayerState *st = (PlayerState *)zalloc(sizeof(PlayerState));
    VehicleInterface *vif = (VehicleInterface *)zalloc(sizeof *vif);
    ObjectInterface *dif = (ObjectInterface *)zalloc(sizeof *dif);
    ObjModel *staffModel = (ObjModel *)zalloc(sizeof *staffModel);
    Shader *ops = (Shader *)zalloc(3 * sizeof(Shader));
    ModelFileHeader *mf = (ModelFileHeader *)zalloc(sizeof *mf);
    void **vdll = (void **)zalloc(sizeof(void *)), **ndll = (void **)zalloc(sizeof(void *));
    int i;

    /* the player and its state */
    player->extra = st;
    player->anim.romDefNo = 0;
    player->anim.localPosY = 10.0f;
    player->anim.velocityX = 1.0f; player->anim.velocityY = 2.0f; player->anim.velocityZ = 3.0f;
    st->sinkOffsetY = 0.5f;
    st->targetYaw = 0x1234;
    st->knockbackTimer = 1.0f;
    st->knockKindBits.knock = 2;
    st->pendingFxFlags = 1 | 8;
    st->surfaceType = 2;                              /* pfx mode 6 */
    st->footPoints[0][0] = 0.25f; st->footPoints[1][2] = -0.25f;

    /* the carried object: raw-offset fields + a vehicle interface */
    st->heldObj = veh;
    *(int *)(veh->raw + 0xf8) = 1;
    *(s16 *)(veh->raw + 0x46) = 0x112;
    vif->render = vehRender; vif->handleRiderScale = vehScale;
    *vdll = vif; veh->anim.dll = vdll;

    /* the active model: op 1 is the two-layer Krazoa shader */
    for (i = 0; i < 3; i++) { ops[i].tag = 100 + i; ops[i].layerCount = 1; }
    ops[1].layerCount = 2;
    mf->renderOpCount = 3; mf->ops = ops;
    gActiveModel.file = mf;

    /* a staff child */
    staffModel->tag = 99;
    staff->anim.classId = 0x2d; staff->banks[0] = staffModel;
    player->childCount = 1; player->childObjs[0] = TO_GUEST(staff);

    gKrazoa = 1;
    LOG("f1:");
    objRender(11, 22, 33, 44, player, 1);
    LOG(" | held=%s opflags=%x", gPlayerHeldObject ? "shader" : "none", ops[1].flags);

    gKrazoa = 0;
    st->pendingFxFlags = 0; st->knockbackTimer = 0.0f;
    LOG("\nf2:");
    objRender(1, 2, 3, 4, player, 1);
    LOG(" | held=%s opflags=%x", gPlayerHeldObject ? "shader" : "none", ops[1].flags);

    dif->render = (void *)dllRender; *ndll = dif; npc->anim.dll = ndll;
    LOG("\nf3:");
    objRender(5, 6, 7, 8, npc, 1);

    LOG("\nstate: y=%.2f foot=%.1f,%.1f p768=%.1f,%.1f,%.1f flags=%x",
        player->anim.localPosY, st->footPoints[0][0], st->footPoints[1][2],
        *(f32 *)(st->raw + 0x768), *(f32 *)(st->raw + 0x76c), *(f32 *)(st->raw + 0x770),
        st->flags360);
    puts(gLog);
    return 0;
}
