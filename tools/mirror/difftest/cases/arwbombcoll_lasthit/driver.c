/* Hits are recorded through the real writer (ObjHits_RecordObjectHit), then
 * the real ARWBombColl_update runs on each pickup:
 *   1  shot open, flown through by the Arwing  -> collected (bomb pickup)
 *   2  closed, shot by the Arwing's bomb, then flown through -> opened, burst
 *   3  shot open, struck by some other object  -> nothing
 *   4  shot open, struck by an impostor         -> nothing
 *   5  closed, struck by the impostor           -> nothing
 * The Arwing and the impostor are alias pair 0 (difftest_alias_alloc). On the
 * high-heap builds they sit 4 GiB apart, so `(u32)lastHitObject ==
 * (u32)getArwing()` takes the impostor for the Arwing unless BOTH casts are
 * widened. Pickups 4 and 5 cover the test on line 142 and the one on line 162.
 * Output is object tags and calls, never addresses. */
#include "shim.h"
#include "snippet.c"

#define HS(o) ((ObjHitsPriorityState*)(o)->anim.hitReactState)

static int ObjHits_GetPriorityHit(GameObject* obj, GameObject** hit, int a, int b)
{
    (void)a; (void)b;
    if (HS(obj)->priorityHitCount == 0) return 0;
    *hit = (GameObject*)(uintptr_t)HS(obj)->hitObjects[0];
    return 1;
}

static GameObject* mkObj(GameObject* o, int tag, s16 romDefNo, int shotOpen)
{
    ObjHitsPriorityState* hs = (ObjHitsPriorityState*)mmAlloc(sizeof(ObjHitsPriorityState), 0, 0);
    ARWBombCollState* st = (ARWBombCollState*)mmAlloc(sizeof(ARWBombCollState), 0, 0);
    memset(o, 0, sizeof *o); memset(hs, 0, sizeof *hs); memset(st, 0, sizeof *st);
    hs->flags = OBJHITS_PRIORITY_STATE_ENABLED;
    st->flags.shotOpen = shotOpen;
    o->anim.hitReactState = hs; o->anim.romDefNo = romDefNo;
    o->extra = st; o->tag = tag;
    return o;
}
static GameObject* mk(int tag, s16 romDefNo, int shotOpen)
{
    return mkObj((GameObject*)mmAlloc(sizeof(GameObject), 0, 0), tag, romDefNo, shotOpen);
}
static GameObject* mkAlias(int member, int tag, s16 romDefNo)
{
    return mkObj((GameObject*)difftest_alias_alloc(0, member, sizeof(GameObject)), tag, romDefNo, 0);
}

static void show(GameObject* p)
{
    ARWBombCollState* st = p->extra;
    LOG(" | %d: collected=%d open=%d hidden=%d\n", p->tag, st->flags.collected,
        st->flags.shotOpen, (p->anim.flags & OBJANIM_FLAG_HIDDEN) != 0);
}

int main(void)
{
    GameObject* other; GameObject* bomb; GameObject* impostor;
    GameObject* p1; GameObject* p2; GameObject* p3; GameObject* p4; GameObject* p5;

    gArwing = mkAlias(0, 1, 0x29a);
    impostor = mkAlias(1, 4, 0x123);
    bomb = mk(2, ARW_ARWING_BOMB_OBJ, 0);
    other = mk(3, 0x123, 0);
    p1 = mk(10, 0x608, 1);
    p2 = mk(11, 0x609, 0);
    p3 = mk(12, 0x608, 1);
    p4 = mk(13, 0x608, 1);
    p5 = mk(14, 0x609, 0);

    ObjHits_RecordObjectHit(gArwing, p1, 5, 2, 1);
    ObjHits_RecordObjectHit(p2, bomb, 5, 2, 1);     /* p2's priority hit: the bomb */
    ObjHits_RecordObjectHit(gArwing, p2, 5, 2, 1);  /* p2's lastHitObject: the Arwing */
    ObjHits_RecordObjectHit(other, p3, 5, 2, 1);
    ObjHits_RecordObjectHit(impostor, p4, 5, 2, 1);
    ObjHits_RecordObjectHit(impostor, p5, 5, 2, 1);

    ARWBombColl_update(p1); show(p1);
    ARWBombColl_update(p2); show(p2);
    ARWBombColl_update(p3); show(p3);
    ARWBombColl_update(p4); show(p4);
    ARWBombColl_update(p5); show(p5);

    printf("%s", gLog + 1);
    return 0;
}
