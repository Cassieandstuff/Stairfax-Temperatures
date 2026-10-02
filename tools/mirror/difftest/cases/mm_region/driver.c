/* Driver for the extracted mmInitRegion. Two regions: one whose free pointer
 * (buf + slotsBytes) lands unaligned (takes the `& ~0x1f` + 0x20 branch), one that
 * lands aligned. Prints the first free item's offset from buf, its size and the
 * slot counts: data, not addresses. Buffers come from mmAlloc (low heap in the
 * oracle, high heap in the mirror) at a fixed 32-byte-aligned base + offset, so
 * both builds see the same alignment. */
#include "shim.h"
#include "snippet.c"
#include <stdio.h>

static void run(const char *label, unsigned off, int size, int numSlots)
{
    u8 *raw = (u8 *)mmAlloc(8192, 0, 0);
    u8 *base = (u8 *)(((uintptr_t)raw + 31) & ~(uintptr_t)31);
    u8 *buf = base + off;
    HeapItem *first;
    mmInitRegion(buf, size, numSlots);
    first = (HeapItem *)gMmRegionTable[gMmRegionCount - 1].start;
    printf("%s slotsBytes=%d loc_off=%ld aligned=%d size=%d used=%d stack1=%d\n", label,
           (int)(numSlots * sizeof(HeapItem)), (long)((u8 *)first->loc - buf),
           (int)(((uintptr_t)first->loc & 31) == 0), first->size,
           gMmRegionTable[gMmRegionCount - 1].slotsUsed, first[1].stack);
}

int main(void)
{
    run("unaligned", 0, 4096, 5);    /* 5 * sizeof(HeapItem) = 5*24 = 120: not 32-aligned */
    run("aligned",   0, 4096, 4);    /* 4 * 24 = 96: 32-aligned */
    return 0;
}
