/* Hits are recorded through the real writer (ObjHits_RecordObjectHit), then
 * the real ARWBombColl_update runs on each pickup:
 *   1  shot open, flown through by the Arwing  -> collected (bomb pickup)
 *   2  closed, shot by the Arwing's bomb, then flown through -> opened, burst
 *   3  shot open, struck by some other object  -> nothing
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

static GameObject* mk(int tag, s16 romDefNo, int shotOpen)
{
    GameObject* o = (GameObject*)mmAlloc(sizeof(GameObject), 0, 0);
    ObjHitsPriorityState* hs = (ObjHitsPriorityState*)mmAlloc(sizeof(ObjHitsPriorityState), 0, 0);
    ARWBombCollState* st = (ARWBombCollState*)mmAlloc(sizeof(ARWBombCollState), 0, 0);
    memset(o, 0, sizeof *o); memset(hs, 0, sizeof *hs); memset(st, 0, sizeof *st);
    hs->flags = OBJHITS_PRIORITY_STATE_ENABLED;
    st->flags.shotOpen = shotOpen;
    o->anim.hitReactState = hs; o->anim.romDefNo = romDefNo;
    o->extra = st; o->tag = tag;
    return o;
}

static void show(GameObject* p)
{
    ARWBombCollState* st = p->extra;
    LOG(" | %d: collected=%d open=%d hidden=%d\n", p->tag, st->flags.collected,
        st->flags.shotOpen, (p->anim.flags & OBJANIM_FLAG_HIDDEN) != 0);
}

int main(void)
{
    GameObject* other; GameObject* bomb;
    GameObject* p1; GameObject* p2; GameObject* p3;

    gArwing = mk(1, 0x29a, 0);
    bomb = mk(2, ARW_ARWING_BOMB_OBJ, 0);
    other = mk(3, 0x123, 0);
    p1 = mk(10, 0x608, 1);
    p2 = mk(11, 0x609, 0);
    p3 = mk(12, 0x608, 1);

    ObjHits_RecordObjectHit(gArwing, p1, 5, 2, 1);
    ObjHits_RecordObjectHit(p2, bomb, 5, 2, 1);     /* p2's priority hit: the bomb */
    ObjHits_RecordObjectHit(gArwing, p2, 5, 2, 1);  /* p2's lastHitObject: the Arwing */
    ObjHits_RecordObjectHit(other, p3, 5, 2, 1);

    ARWBombColl_update(p1); show(p1);
    ARWBombColl_update(p2); show(p2);
    ARWBombColl_update(p3); show(p3);

    printf("%s", gLog + 1);
    return 0;
}
