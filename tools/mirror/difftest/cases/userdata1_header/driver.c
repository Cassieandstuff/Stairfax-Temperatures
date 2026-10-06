/* Same scenario as userdata1_readers, on the real GameObject: three beams
 * (slot 0 for sale, 1 bought, 2 unavailable). Frame one latches the shop into
 * userData1, frame two reads it back and calls the shop through its interface
 * table. Output is romDefNo tags and flags, never addresses. */
#include "shim.h"
#include "snippet.c"

static GameObject* gShop;
static int gSearches;
struct GameObject* objGetNearestTypeTo(int group, struct GameObject* obj, f32* maxDistance)
{
    (void)obj; (void)maxDistance;
    gSearches++;
    return group == 9 ? gShop : NULL;
}
static ObjTextureRuntimeSlot* gTex[3];
ObjTextureRuntimeSlot* objFindTexture(GameObject* obj, int target, int unusedMaterialIndex)
{
    (void)target; (void)unusedMaterialIndex;
    return gTex[obj->anim.romDefNo - 10];
}

static int isItemAvailable(GameObject* shop, int slot)
{
    LOG(" avail(%d,%d)", shop->anim.romDefNo, slot);
    return slot != 2;
}
static int isItemBought(GameObject* shop, int slot)
{
    LOG(" bought(%d,%d)", shop->anim.romDefNo, slot);
    return slot == 1;
}
static ShopInterface gShopIface;
static ShopInterface* gShopIfacePtr = &gShopIface;

static GameObject* mk(int tag, int slot)
{
    GameObject* o = (GameObject*)mmAlloc(sizeof(GameObject), 0, 0);
    SpitembeamPlacement* pl = (SpitembeamPlacement*)mmAlloc(sizeof *pl, 0, 0);
    memset(o, 0, sizeof *o);
    memset(pl, 0, sizeof *pl);
    pl->itemIndex = (s16)slot;
    o->anim.placementData = (void*)pl;
    *(ShopInterface***)&o->anim.dll = &gShopIfacePtr;
    o->anim.romDefNo = (s16)tag;
    return o;
}

int main(void)
{
    GameObject* beams[3];
    int i, pass;

    gShopIface.isItemAvailable = isItemAvailable;
    gShopIface.isItemBought = isItemBought;
    gShop = mk(1, 0);
    for (i = 0; i < 3; i++) {
        beams[i] = mk(10 + i, i);
        gTex[i] = (ObjTextureRuntimeSlot*)mmAlloc(sizeof(ObjTextureRuntimeSlot), 0, 0);
        memset(gTex[i], 0, sizeof *gTex[i]);
        gTex[i]->offsetS = (s16)(0x3FC + i);
    }

    for (pass = 0; pass < 2; pass++)
        for (i = 0; i < 3; i++)
            spitembeam_update(beams[i]);

    LOG(" | searches=%d", gSearches);
    for (i = 0; i < 3; i++)
        LOG(" | %d: shop=%d hidden=%d off=%d tex=%x", beams[i]->anim.romDefNo,
            ((GameObject*)(uintptr_t)beams[i]->userData1)->anim.romDefNo,
            (beams[i]->anim.flags & OBJANIM_FLAG_HIDDEN) != 0,
            (beams[i]->objectFlags & OBJECT_OBJFLAG_UPDATE_DISABLED) != 0, gTex[i]->offsetS);
    printf("%s\n", gLog + 1);
    return 0;
}
