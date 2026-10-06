/* A cloud runner fires a projectile through the real writer, which stores the
 * runner in the projectile's userData2. The real SB_FireBall_update then runs
 * on that projectile for 17 frames, and on a second one whose userData2 is
 * empty. The first adopts the runner as its target, launches, flies, and arms
 * its hitbox after frame 15. The second never moves. Output is romDefNo tags,
 * counters and flags, never addresses. */
#include "shim.h"
#include "snippet.c"

f32 timeDelta = 1.0f;
u8 framesThisStep = 1;
static GameObject* gNextSpawn;
static int gSpawns, gTrails, gFrees;

int Obj_CanSetupObject(void) { return 1; }
void Sfx_PlayFromObject(GameObject* obj, u16 sfxId) { LOG(" sfx(%d)", obj->anim.romDefNo); (void)sfxId; }
#if defined(DIFFTEST_MIRROR)
typedef uintptr_t SetupType;                /* lifecycle.h: param widened in the mirror */
#else
typedef int SetupType;
#endif
ObjPlacement* Obj_AllocObjectSetup(int size, SetupType type)
{
    ObjPlacement* p = (ObjPlacement*)mmAlloc(size > (int)sizeof(ObjPlacement) ? size : sizeof(ObjPlacement), 0, 0);
    memset(p, 0, sizeof *p);
    LOG(" setup(%x)", (int)type);
    return p;
}
GameObject* objSetupObject(ObjPlacement* setup, int flags, int mapLayer, int objIndex, void* parent)
{
    (void)setup; (void)flags; (void)mapLayer; (void)objIndex; (void)parent;
    return gNextSpawn;
}
void vecRotateZXY(s16* rotation, f32* vector) { (void)rotation; (void)vector; }
void voxmaps_worldToGrid(f32* in, s16* out) { (void)in; (void)out; }
int voxmaps_traceLine(VoxPos* start, VoxPos* end, VoxPos* coordOut, u8* occOut, u8 skipFirst)
{
    (void)start; (void)end; (void)coordOut; (void)occOut; (void)skipFirst;
    return 1;                                       /* blocked: dist = 200 */
}
void voxmaps_gridToWorld(f32* out, s16* grid) { (void)out; (void)grid; }
void objfx_spawnFlaggedTrailBurst(void* obj, f32 fval, u8 mode, int f6val, int f4val, void* origin)
{
    (void)obj; (void)fval; (void)mode; (void)f6val; (void)f4val; (void)origin;
    gTrails++;
}
void Obj_FreeObject(GameObject* obj) { gFrees++; (void)obj; }
static void spawnObject(void* obj, int effectId, void* params, int mode, int modelId, void* extraArg)
{
    (void)params; (void)mode; (void)modelId; (void)extraArg;
    if (effectId == DRCLOUDRUNNER_PARTFX) LOG(" fx(%d,%x)", ((GameObject*)obj)->anim.romDefNo, effectId);
    gSpawns++;
}
EffectInterface** gPartfxInterface;

static GameObject* mk(int tag, int extraSize)
{
    GameObject* o = (GameObject*)mmAlloc(sizeof(GameObject), 0, 0);
    void* extra = mmAlloc(extraSize, 0, 0);
    ObjHitsPriorityState* hs = (ObjHitsPriorityState*)mmAlloc(sizeof(ObjHitsPriorityState), 0, 0);
    memset(o, 0, sizeof *o); memset(extra, 0, extraSize); memset(hs, 0, sizeof *hs);
    o->extra = extra;
    o->anim.hitReactState = hs;
    o->anim.romDefNo = (s16)tag;
    return o;
}

static void show(GameObject* f)
{
    SBFireBallState* st = (SBFireBallState*)f->extra;
    LOG(" | %d: target=%d launched=%d age=%d life=%d armed=%d moved=%d", f->anim.romDefNo,
        st->target ? st->target->anim.romDefNo : -1, st->launched, st->age, (int)f->userData1,
        (ObjAnim_GetPriorityHitState(&f->anim)->flags & OBJHITS_PRIORITY_STATE_ENABLED) != 0,
        f->anim.localPosZ != 0.0f);
}

int main(void)
{
    EffectInterface* iface = (EffectInterface*)mmAlloc(sizeof(EffectInterface), 0, 0);
    EffectInterface** ifacePtr = (EffectInterface**)mmAlloc(sizeof *ifacePtr, 0, 0);
    GameObject* runner; GameObject* fireball; GameObject* orphan;
    int i;

    memset(iface, 0, sizeof *iface);
    iface->spawnObject = spawnObject;
    *ifacePtr = iface;
    gPartfxInterface = ifacePtr;

    runner = mk(600, sizeof(CloudRunnerState));
    fireball = mk(493, sizeof(SBFireBallState));
    orphan = mk(494, sizeof(SBFireBallState));
    orphan->userData1 = 200;

    gNextSpawn = fireball;
    DR_CloudRunner_fireProjectile(runner);      /* the writer */
    fireball->anim.velocityZ = -16.0f;          /* vecRotateZXY is stubbed out */

    for (i = 0; i < 17; i++) {                  /* the reader */
        SB_FireBall_update(fireball);
        SB_FireBall_update(orphan);
    }
    show(fireball);
    show(orphan);
    LOG(" | trails=%d particles=%d frees=%d", gTrails, gSpawns, gFrees);
    printf("%s\n", gLog + 1);
    return 0;
}
