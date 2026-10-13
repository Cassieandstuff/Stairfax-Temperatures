/* Compile surface for the extracted mmInitRegion: the heap item + region table.
 * Field sets only; both builds use the same host layout, so sizeof(HeapItem)
 * (and thus slotsBytes) matches between oracle and mirror. */
#ifndef MM_REGION_SHIM_H
#define MM_REGION_SHIM_H
#include "difftest.h"
typedef uint8_t u8;
typedef int16_t s16;
typedef struct HeapItem {
    void *loc; int size; s16 type; s16 stack; s16 prev; s16 next;
} HeapItem;
typedef struct MmRegion {
    void *start; int size; int usedBytes; int numSlots; int slotsUsed;
} MmRegion;
MmRegion gMmRegionTable[4];
int gMmRegionCount;
#endif
