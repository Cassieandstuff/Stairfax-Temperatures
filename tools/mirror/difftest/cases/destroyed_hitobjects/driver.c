/* Two blasted rocks, each with two pieces, so three recorded stages. Hits go
 * into hitObjects[] through the real writer, one frame at a time.
 *   rock 10, fresh:  f1 A | f2 A B | f3 B A, plus priority-3 W | f4 C | f5 D
 *     A and B are deduped against slots 0 and 1. C fills slot 2 and breaks
 *     the rock. f5 returns early.
 *   rock 20, resumed from progress 1 (slot 0 never recorded):
 *     f1 D | f2 D E | f3 nothing
 *     D is recorded in slot 1 and deduped there. E breaks the rock.
 * On the high-heap builds (the mirror, and the untransformed control) A and B
 * are placed exactly 4 GiB apart, so their addresses share the low 32 bits.
 * Code that compares them through a 32-bit value takes B for the
 * already-recorded A. The low-memory oracle can't alias, so it gives the
 * intended trace.
 * Output is romDefNo tags, gamebits and model indices per frame, never
 * addresses. */
#include "shim.h"
#include "snippet.c"

static u8 gBits[0x400];

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
void objSetSlot(GameObject* obj, s8 slot) { (void)obj; (void)slot; }

static GameObject* mk(int tag, int extraSize)
{
    GameObject* o = (GameObject*)difftest_mmAlloc(sizeof(GameObject), 0, 0);
    ObjHitsPriorityState* hs = (ObjHitsPriorityState*)difftest_mmAlloc(sizeof(ObjHitsPriorityState), 0, 0);
    memset(o, 0, sizeof *o); memset(hs, 0, sizeof *hs);
    o->anim.hitReactState = hs;
    o->anim.romDefNo = (s16)tag;
    if (extraSize) {
        o->extra = difftest_mmAlloc(extraSize, 0, 0);
        memset(o->extra, 0, extraSize);
    }
    return o;
}
#define HS(o) ((ObjHitsPriorityState*)(o)->anim.hitReactState)

#if !defined(DIFFTEST_ORACLE_LOWMEM)
#  include <sys/mman.h>
/* a GameObject at a fixed address; A and B land 4 GiB apart */
static GameObject* mkAt(int tag, uintptr_t addr)
{
    GameObject* o = (GameObject*)mmap((void*)addr, 0x1000, PROT_READ | PROT_WRITE,
                                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    ObjHitsPriorityState* hs = (ObjHitsPriorityState*)difftest_mmAlloc(sizeof(ObjHitsPriorityState), 0, 0);
    if ((void*)o == MAP_FAILED || (uintptr_t)o != addr) { perror("mmap alias"); exit(2); }
    memset(hs, 0, sizeof *hs);
    o->anim.hitReactState = hs;
    o->anim.romDefNo = (s16)tag;
    return o;
}
#  define ALIAS_BASE ((uintptr_t)0x7e0000001000ull)
#  define MK_A() mkAt(11, ALIAS_BASE)
#  define MK_B() mkAt(12, ALIAS_BASE + ((uintptr_t)1 << 32))
#else
#  define MK_A() mk(11, 0)
#  define MK_B() mk(12, 0)
#endif

static GameObject* mkRock(int tag, s16 progressBit, s16 completedBit)
{
    GameObject* r = mk(tag, sizeof(BlastedTargetState));
    BlastedTargetPlacement* pl = (BlastedTargetPlacement*)difftest_mmAlloc(sizeof *pl, 0, 0);
    memset(pl, 0, sizeof *pl);
    pl->pieceCount = 2; pl->mapLayerId = tag / 10; pl->rotXByte = 1;
    pl->completedGameBit = completedBit; pl->progressGameBit = progressBit;
    r->anim.placement = (void*)pl;
    blasted_init(r, pl);
    return r;
}

/* one frame: record the given hits (NULL-terminated, priority 5 unless the
 * object's tag is 13), run the update, clear the hit list */
static void frame(GameObject* r, const char* name, GameObject** hits)
{
    LOG(" %s:", name);
    for (; *hits; hits++)
        ObjHits_RecordObjectHit(r, *hits, (*hits)->anim.romDefNo == 13 ? 3 : 5, 1, 0);
    blasted_update(r);
    HS(r)->priorityHitCount = 0;
}

static void show(GameObject* r)
{
    BlastedTargetState* st = (BlastedTargetState*)r->extra;
    LOG(" => stage=%d pieces=%d activated=%d rot=%d", st->damageStage, st->pieceCount,
        st->mapLayerActivated, r->anim.rotX);
}

int main(void)
{
    GameObject* a = MK_A(); GameObject* b = MK_B(); GameObject* w = mk(13, 0);
    GameObject* c = mk(14, 0); GameObject* d = mk(15, 0); GameObject* e = mk(16, 0);
    GameObject* fresh; GameObject* resumed;

    LOG("|");
    fresh = mkRock(10, 0x101, 0x100);
    { GameObject* h[] = { a, NULL };          frame(fresh, "f1", h); }
    { GameObject* h[] = { a, b, NULL };       frame(fresh, "f2", h); }
    { GameObject* h[] = { b, a, w, NULL };    frame(fresh, "f3", h); }
    { GameObject* h[] = { c, NULL };          frame(fresh, "f4", h); }
    { GameObject* h[] = { d, NULL };          frame(fresh, "f5", h); }
    show(fresh);

    LOG(" |");
    gBits[0x201] = 1;                           /* saved progress: one piece down */
    resumed = mkRock(20, 0x201, 0x200);
    { GameObject* h[] = { d, NULL };          frame(resumed, "f1", h); }
    { GameObject* h[] = { d, e, NULL };       frame(resumed, "f2", h); }
    { GameObject* h[] = { NULL };             frame(resumed, "f3", h); }
    show(resumed);

    printf("%s\n", gLog + 1);
    return 0;
}
