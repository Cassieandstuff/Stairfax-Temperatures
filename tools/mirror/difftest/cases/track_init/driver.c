/* Driver for the extracted trackInitCollisionBuffers.
 *
 * After init, write sentinels through the pointer-bearing globals and read them
 * back, then confirm the dynamic-slot cooldowns were zeroed. The printed values
 * are data, not addresses, so a correct oracle and a correct mirror must match
 * exactly — even though their raw pointer values differ (low heap vs high heap).
 *
 * If the transform were wrong (a pointer left in a 32-bit int), the mirror's high
 * malloc address would truncate and these writes would corrupt or fault. */
#include "shim.h"
#include "snippet.c"
#include <stdio.h>

int main(void)
{
    trackInitCollisionBuffers();

    unsigned *pool = (unsigned *)gIntersectLinePool;        /* int or uintptr_t */
    pool[0] = 0xDEADu;
    pool[1] = 0xBEEFu;

    unsigned *idx = (unsigned *)gIntersectLineIndexTable;
    idx[0] = 0x55u;

    unsigned *tri = (unsigned *)gTrackTriangleBuffer;
    tri[0] = 0x1234u;

    MapDynamicSlot *slots = (MapDynamicSlot *)gMapDynamicSlots;
    int cd_first = slots[0].cooldown;
    int cd_last = slots[MAP_DYNAMIC_SLOT_COUNT - 1].cooldown;

    printf("pool=%x,%x idx=%x tri=%x cd=%d,%d\n",
           pool[0], pool[1], idx[0], tri[0], cd_first, cd_last);
    return 0;
}
