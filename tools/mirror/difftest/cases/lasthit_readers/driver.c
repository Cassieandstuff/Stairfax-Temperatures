/* Hits are recorded through the real writer (ObjHits_RecordObjectHit), then
 * the real readers run on the struck objects.
 * The player and Tricky are each member 0 of an alias pair
 * (difftest_alias_alloc), with an impostor as member 1. On the high-heap
 * builds each impostor sits 4 GiB above its twin. Spikes struck by an
 * impostor must not react, which fails if PinPonSpike's
 * `lastHitObject == (u32)Obj_GetPlayerObject()` (or the Tricky test) compares
 * through 32 bits on both sides. Output is object tags and counters, never
 * addresses. */
#include "shim.h"
#include "snippet.c"

static GameObject* mkObj(GameObject* o, int tag, s16 romDefNo)
{
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
static GameObject* mk(int tag, s16 romDefNo)
{
    return mkObj((GameObject*)mmAlloc(sizeof(GameObject), 0, 0), tag, romDefNo);
}
static GameObject* mkAlias(int pair, int member, int tag)
{
    return mkObj((GameObject*)difftest_alias_alloc(pair, member, sizeof(GameObject)), tag, 0);
}

#define HS(o) ((ObjHitsPriorityState*)(o)->anim.hitReactState)

int main(void)
{
    GameObject* spikeP; GameObject* spikeT; GameObject* spikeO;
    GameObject* fxHit; GameObject* fxIgnored; GameObject* fxNone; GameObject* other;
    GameObject* ignored; GameObject* fakeP; GameObject* fakeT;
    GameObject* spikeFP; GameObject* spikeFT;

    gPlayer = mkAlias(0, 0, 1);
    gTricky = mkAlias(1, 0, 2);
    fakeP = mkAlias(0, 1, 5);
    fakeT = mkAlias(1, 1, 6);
    other = mk(3, 0);
    ignored = mk(4, DLL19D_IGNORED_HIT_SEQUENCE_ID);
    spikeP = mk(10, 0); spikeT = mk(11, 0); spikeO = mk(12, 0);
    spikeFP = mk(13, 0); spikeFT = mk(14, 0);
    fxHit = mk(20, 0); fxIgnored = mk(21, 0); fxNone = mk(22, 0);

    /* the writer: each striker records itself in the struck object's state */
    ObjHits_RecordObjectHit(gPlayer, spikeP, 5, 2, 1);
    ObjHits_RecordObjectHit(gTricky, spikeT, 5, 2, 1);
    ObjHits_RecordObjectHit(other, spikeO, 5, 2, 1);
    ObjHits_RecordObjectHit(gPlayer, fxHit, 5, 2, 1);
    ObjHits_RecordObjectHit(ignored, fxIgnored, 5, 2, 1);
    ObjHits_RecordObjectHit(fakeP, spikeFP, 5, 2, 1);
    ObjHits_RecordObjectHit(fakeT, spikeFT, 5, 2, 1);

    /* PinPonSpike: lastHitObject compared against the player and Tricky */
    pinponspike_update(spikeP);
    LOG(" | P: alpha=%d timer=%d en=%d", spikeP->anim.alpha, (int)spikeP->userData1,
        HS(spikeP)->flags & OBJHITS_PRIORITY_STATE_ENABLED);
    pinponspike_update(spikeT);
    LOG(" | T: alpha=%d timer=%d", spikeT->anim.alpha, (int)spikeT->userData1);
    pinponspike_update(spikeO);
    LOG(" | O: alpha=%d timer=%d", spikeO->anim.alpha, (int)spikeO->userData1);
    pinponspike_update(spikeFP);
    pinponspike_update(spikeFT);
    LOG(" | impostors: alpha=%d,%d timer=%d,%d", spikeFP->anim.alpha, spikeFT->anim.alpha,
        (int)spikeFP->userData1, (int)spikeFT->userData1);

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
