/* Two objects whose hit state is allocated as ObjHitsPriorityState, as
 * objhits.c does. InitState runs on A's state through the ObjHitReactState
 * view. B then records a hit on A, which writes A's lastHitObject and B's
 * hitObjects[0] through the priority view. Each field is printed through the
 * view that did not write it, as tags and arena offsets, never addresses. */
#include "shim.h"
#include "snippet.c"

static int gLoadMoveCalls;
static void ObjHitReact_LoadMoveEntries(ObjAnimComponent* objAnim, ObjAnimBank* bank, int objType,
                                        ObjHitReactState* hitState, int a, int b)
{
    (void)objAnim; (void)bank; (void)objType; (void)a; (void)b;
    if (hitState->entries != NULL) gLoadMoveCalls++;
}

static GameObject* mk(int tag)
{
    GameObject* o = (GameObject*)mmAlloc(sizeof(GameObject), 0, 0);
    ObjHitsPriorityState* hs = (ObjHitsPriorityState*)mmAlloc(sizeof(ObjHitsPriorityState), 0, 0);
    memset(o, 0, sizeof *o);
    memset(hs, 0, sizeof *hs);
    o->anim.hitReactState = hs;
    o->tag = tag;
    return o;
}

int main(void)
{
    GameObject* a = mk(1);
    GameObject* b = mk(2);
    ObjHitsPriorityState* pa = (ObjHitsPriorityState*)a->anim.hitReactState;
    ObjHitsPriorityState* pb = (ObjHitsPriorityState*)b->anim.hitReactState;
    ObjHitReactState* ra = (ObjHitReactState*)pa;
    u8* arena = (u8*)mmAlloc(512, 0, 0);
    uintptr_t end;

    /* written through priority, read through react */
    pa->capsuleScale = 0x1234;
    pa->flags = OBJHITS_PRIORITY_STATE_ENABLED;
    pa->shapeFlags = 0x10;
    pb->flags = OBJHITS_PRIORITY_STATE_ENABLED;
    printf("react: reset=%x flags=%x shape=%x", ra->resetFrameCount, ra->flags, ra->shapeFlags);

    /* written through react, read through priority */
    end = (uintptr_t)ObjHitReact_InitState(7, (ObjAnimBank*)arena, ra, (uintptr_t)(arena + 3),
                                           &a->anim);
    printf(" | init: cap=%d entries=+%d end=+%d modes=%d,%d loads=%d",
           ra->entryBufferByteCapacity, (int)((u8*)ra->entries - arena),
           (int)(end - (uintptr_t)arena), pa->activeHitboxMode, pa->resetHitboxMode,
           gLoadMoveCalls);

    /* priority-view writes of pointers: lastHitObject on A, hitObjects[] on B */
    ObjHits_RecordObjectHit(b, a, 5, 2, 1);
    printf(" | hit: last=%d slot0=%d count=%d",
           ((GameObject*)(uintptr_t)pa->lastHitObject)->tag,
           ((GameObject*)(uintptr_t)pb->hitObjects[0])->tag, pb->priorityHitCount);

    /* nothing the priority writes did may disturb the react view */
    printf(" | after: entries=+%d modes=%d,%d reset=%x\n",
           (int)((u8*)ra->entries - arena), ra->activeHitboxMode, ra->resetHitboxMode,
           ra->resetFrameCount);
    return 0;
}
