/* Three beams on one shop stall: slot 0 still for sale, slot 1 bought,
 * slot 2 no longer available. The first update latches the shop into
 * userData1. The second reads it back and asks the shop about the beam's
 * slot. The shop answers by its own tag, so a truncated handle would crash or
 * misroute the calls. Output is tags and flags, never addresses. */
#include "shim.h"
#include "snippet.c"

static int isItemAvailable(GameObject* shop, int slot)
{
    LOG(" avail(%d,%d)", shop->tag, slot);
    return slot != 2;
}
static int isItemBought(GameObject* shop, int slot)
{
    LOG(" bought(%d,%d)", shop->tag, slot);
    return slot == 1;
}
static ShopInterface gShopIface = { isItemAvailable, isItemBought };

static GameObject* mk(int tag, int slot)
{
    GameObject* o = (GameObject*)mmAlloc(sizeof(GameObject), 0, 0);
    SpitembeamPlacement* pl = (SpitembeamPlacement*)mmAlloc(sizeof *pl, 0, 0);
    ShopInterface** dll = (ShopInterface**)mmAlloc(sizeof *dll, 0, 0);
    memset(o, 0, sizeof *o);
    pl->itemIndex = (s16)slot;
    *dll = &gShopIface;
    o->anim.placementData = pl; o->anim.dll = dll; o->tag = tag;
    return o;
}

int main(void)
{
    GameObject* beams[3];
    int i, pass;

    gShop = mk(1, 0);
    for (i = 0; i < 3; i++) beams[i] = mk(10 + i, i);
    for (i = 0; i < 3; i++) gTex[i].offsetS = (s16)(0x3FC + i);

    for (pass = 0; pass < 2; pass++)
        for (i = 0; i < 3; i++)
            spitembeam_update(beams[i]);

    LOG(" | searches=%d", gSearches);
    for (i = 0; i < 3; i++)
        LOG(" | %d: shop=%d hidden=%d off=%d tex=%x", beams[i]->tag,
            ((GameObject*)beams[i]->userData1)->tag,
            (beams[i]->anim.flags & OBJANIM_FLAG_HIDDEN) != 0,
            (beams[i]->objectFlags & OBJECT_OBJFLAG_UPDATE_DISABLED) != 0, gTex[i].offsetS);
    printf("%s\n", gLog + 1);
    return 0;
}
