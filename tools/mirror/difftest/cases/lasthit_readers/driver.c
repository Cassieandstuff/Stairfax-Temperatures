/* Hits are recorded through the real writer (ObjHits_RecordObjectHit), then
 * the real readers run on the struck objects. Output is object tags and
 * counters, never addresses. */
#include "shim.h"
#include "snippet.c"

static GameObject* mk(int tag, s16 romDefNo)
{
    GameObject* o = (GameObject*)mmAlloc(sizeof(GameObject), 0, 0);
    ObjHitsPriorityState* hs = (ObjHitsPriorityState*)mmAlloc(sizeof(ObjHitsPriorityState), 0, 0);
    Dll19DState* st = (Dll19DState*)mmAlloc(sizeof(Dll19DState), 0, 0);
    Dll19DPlacement* pl = (Dll19DPlacement*)mmAlloc(sizeof(Dll19DPlacement), 0, 0);
    memset(o, 0, sizeof *o); memset(hs, 0, sizeof *hs);
    memset(st, 0, sizeof *st); memset(pl, 0, sizeof *pl);
    hs->flags = OBJHITS_PRIORITY_STATE_ENABLED;
    pl->variant = 2;
    o->anim.hitReactState = hs; o->anim.romDefNo = romDefNo; o->anim.alpha = 0xff;
    o->anim.placementData = pl; o->extra = st; o->tag = tag;
    return o;
}

#define HS(o) ((ObjHitsPriorityState*)(o)->anim.hitReactState)

int main(void)
{
    GameObject* spikeP; GameObject* spikeT; GameObject* spikeO;
    GameObject* fxHit; GameObject* fxIgnored; GameObject* fxNone; GameObject* other;
    GameObject* ignored;

    gPlayer = mk(1, 0);
    gTricky = mk(2, 0);
    other = mk(3, 0);
    ignored = mk(4, DLL19D_IGNORED_HIT_SEQUENCE_ID);
    spikeP = mk(10, 0); spikeT = mk(11, 0); spikeO = mk(12, 0);
    fxHit = mk(20, 0); fxIgnored = mk(21, 0); fxNone = mk(22, 0);

    /* the writer: each striker records itself in the struck object's state */
    ObjHits_RecordObjectHit(gPlayer, spikeP, 5, 2, 1);
    ObjHits_RecordObjectHit(gTricky, spikeT, 5, 2, 1);
    ObjHits_RecordObjectHit(other, spikeO, 5, 2, 1);
    ObjHits_RecordObjectHit(gPlayer, fxHit, 5, 2, 1);
    ObjHits_RecordObjectHit(ignored, fxIgnored, 5, 2, 1);

    /* PinPonSpike: lastHitObject compared against the player and Tricky */
    pinponspike_update(spikeP);
    LOG(" | P: alpha=%d timer=%d en=%d", spikeP->anim.alpha, (int)spikeP->userData1,
        HS(spikeP)->flags & OBJHITS_PRIORITY_STATE_ENABLED);
    pinponspike_update(spikeT);
    LOG(" | T: alpha=%d timer=%d", spikeT->anim.alpha, (int)spikeT->userData1);
    pinponspike_update(spikeO);
    LOG(" | O: alpha=%d timer=%d", spikeO->anim.alpha, (int)spikeO->userData1);

    /* 413: lastHitObject dereferenced for its romDefNo */
    gSpawns = 0;
    dll413_hitDetect(fxHit);
    LOG(" | fx: spawns=%d despawn=%d", gSpawns, ((Dll19DState*)fxHit->extra)->despawnTimer);
    gSpawns = 0;
    dll413_hitDetect(fxIgnored);
    dll413_hitDetect(fxNone);
    LOG(" | ignored+none: spawns=%d", gSpawns);

    printf("%s\n", gLog + 1);
    return 0;
}
