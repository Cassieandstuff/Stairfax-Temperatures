/* Hits go into hitObjects[] through the real writer (ObjHits_RecordObjectHit),
 * then each real reader runs:
 *   GetPriorityHit  T is struck by A (priority 7), B (5) and C (9): the best
 *                   slot is B's.
 *   blasted_update  rock R (2 pieces) over four frames: X; X again + Y + a
 *                   priority-3 W; Z; then nothing. X is deduped against
 *                   destroyedHitObjects[], W is ignored, Z breaks the rock,
 *                   and the last frame returns early.
 *   InvHit_update   owner O is struck by I1 then I2. SELF_FREE I2 finds itself
 *                   in O's second slot and frees itself; I3 isn't there.
 * Output is romDefNo tags, counters and flags, never addresses. */
#include "shim.h"
#include "snippet.c"

f32 timeDelta = 1.0f;
u8 framesThisStep = 1;
EffectInterface** gPartfxInterface;
static u8 gBits[0x400];
static int gFrees;

u32 mainGetBit(int gameBit) { return gBits[gameBit & 0x3ff]; }
void mainSetBits(int gameBit, int value)
{
    gBits[gameBit & 0x3ff] = (u8)value;
    if (value) LOG(" bit(%x=%d)", gameBit, value);
}
int blasted_activateMapLayer(GameObject* obj, int mapLayerId)
{
    LOG(" layer(%d,%d)", obj->anim.romDefNo, mapLayerId);
    return 1;
}
void Obj_SetActiveModelIndex(GameObject* obj, int idx) { LOG(" model(%d,%d)", obj->anim.romDefNo, idx); }
void Obj_FreeObject(GameObject* obj) { LOG(" free(%d)", obj->anim.romDefNo); gFrees++; }
GameObject* Obj_GetPlayerObject(void) { return NULL; }
GameObject* getTrickyObject(void) { return NULL; }
GameObject* playerGetTargetObject(GameObject* playerObj) { (void)playerObj; return NULL; }
int ObjList_ContainsObject(GameObject* obj) { (void)obj; return 1; }
int trackGetHeight(GameObject* obj, f32 x, f32 y, f32 z, TrackGroundHit*** hitsOut, int mode, int queryMask)
{
    (void)obj; (void)x; (void)y; (void)z; (void)hitsOut; (void)mode; (void)queryMask;
    return 0;
}

static GameObject* mk(int tag, int extraSize)
{
    GameObject* o = (GameObject*)difftest_mmAlloc(sizeof(GameObject), 0, 0);
    ObjHitsPriorityState* hs = (ObjHitsPriorityState*)difftest_mmAlloc(sizeof(ObjHitsPriorityState), 0, 0);
    memset(o, 0, sizeof *o); memset(hs, 0, sizeof *hs);
    hs->flags = OBJHITS_PRIORITY_STATE_ENABLED;
    o->anim.hitReactState = hs;
    o->anim.romDefNo = (s16)tag;
    if (extraSize) {
        o->extra = difftest_mmAlloc(extraSize, 0, 0);
        memset(o->extra, 0, extraSize);
    }
    return o;
}
#define HS(o) ((ObjHitsPriorityState*)(o)->anim.hitReactState)

static void getPriorityHit(void)
{
    GameObject* t = mk(1, 0);
    GameObject* best = NULL;
    int sphere = -1;
    u32 volume = 0;
    int prio;

    ObjHits_RecordObjectHit(t, mk(2, 0), 7, 1, 1);
    ObjHits_RecordObjectHit(t, mk(3, 0), 5, 4, 2);
    ObjHits_RecordObjectHit(t, mk(4, 0), 9, 6, 3);
    prio = ObjHits_GetPriorityHit(t, &best, &sphere, &volume);
    LOG(" best=%d prio=%d sphere=%d volume=%d", best ? best->anim.romDefNo : -1, prio, sphere, (int)volume);
}

static void blasted(void)
{
    GameObject* r = mk(10, sizeof(BlastedTargetState));
    GameObject* x = mk(11, 0); GameObject* y = mk(12, 0);
    GameObject* w = mk(13, 0); GameObject* z = mk(14, 0);
    BlastedTargetPlacement* pl = (BlastedTargetPlacement*)difftest_mmAlloc(sizeof *pl, 0, 0);
    BlastedTargetState* st = (BlastedTargetState*)r->extra;

    memset(pl, 0, sizeof *pl);
    pl->pieceCount = 2; pl->mapLayerId = 3; pl->completedGameBit = 0x100; pl->progressGameBit = 0x101;
    r->anim.placement = (void*)pl;

    LOG(" | f1:");
    ObjHits_RecordObjectHit(r, x, 5, 1, 0);
    blasted_update(r);
    HS(r)->priorityHitCount = 0;
    LOG(" f2:");
    ObjHits_RecordObjectHit(r, x, 5, 1, 0);
    ObjHits_RecordObjectHit(r, y, 5, 1, 0);
    ObjHits_RecordObjectHit(r, w, 3, 1, 0);
    blasted_update(r);
    HS(r)->priorityHitCount = 0;
    LOG(" f3:");
    ObjHits_RecordObjectHit(r, z, 5, 1, 0);
    blasted_update(r);
    LOG(" f4:");
    blasted_update(r);
    LOG(" stage=%d activated=%d", st->damageStage, st->mapLayerActivated);
}

static void invHit(void)
{
    GameObject* owner = mk(20, 0);
    GameObject* i1 = mk(21, sizeof(InvHitState));
    GameObject* i2 = mk(22, sizeof(InvHitState));
    GameObject* i3 = mk(23, sizeof(InvHitState));

    ObjHits_RecordObjectHit(owner, i1, 5, 1, 0);
    ObjHits_RecordObjectHit(owner, i2, 6, 1, 0);
    ((InvHitState*)i2->extra)->mode = INVHIT_MODE_SELF_FREE;
    ((InvHitState*)i3->extra)->mode = INVHIT_MODE_SELF_FREE;
    i2->userData1 = (uintptr_t)owner;
    i3->userData1 = (uintptr_t)owner;
    LOG(" |");
    InvHit_update(i2);
    InvHit_update(i3);
    LOG(" frees=%d en=%d,%d", gFrees,
        (HS(i2)->flags & OBJHITS_PRIORITY_STATE_ENABLED) != 0,
        (HS(i3)->flags & OBJHITS_PRIORITY_STATE_ENABLED) != 0);
}

int main(void)
{
    getPriorityHit();
    blasted();
    invHit();
    printf("%s\n", gLog + 1);
    return 0;
}
