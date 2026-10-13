/* seqPairTableLookupPtr: the pointer-valued twin of maketex.c's seqPairTableLookup.
 *
 * The game keeps {key, value} pair tables and looks them up with one function
 * that walks them as int[2] (8-byte pairs). Most tables hold ints (game bits,
 * move ids) and stay that way. A few hold POINTERS: ObjSeqStreamMapEntry
 * {int trackId; u32* streamIds;} (2.c) and CFGuardian's sequence-choice table.
 * On a 64-bit host those pairs are 16 bytes {int32 key; pad; void* value}, so the
 * int[2] walk reads the wrong rows and half a pointer. The mirror retargets only
 * the pointer-valued call sites here ([[call.retarget]] rules); the int tables
 * keep the original.
 *
 * Same algorithm as the decomp, deliberately including its binary-search loop
 * condition (`while (count <= lo)`): behavior is ported, not repaired. Keys are
 * int32 at offset 0. A table widened to intptr_t[N][2] (CFGuardian) matches this
 * layout on a little-endian host (the int key is the low half of slot 0). */
#ifndef STAIRFAX_SEQPAIR_H
#define STAIRFAX_SEQPAIR_H
#include <stdint.h>

typedef struct SfxSeqPtrPair {
    int32_t key;
    void* value;
} SfxSeqPtrPair;

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "seqPairTableLookupPtr: intptr_t[N][2] tables assume a little-endian host"
#endif

static inline void* seqPairTableLookupPtr(void* entries, int count, int key)
{
    SfxSeqPtrPair* arr = (SfxSeqPtrPair*)entries;
    int lo, mid, i;
    if (count <= 16) {
        for (i = 0; i != count; i++) {
            if (arr->key == key)
                return arr->value;
            arr++;
        }
        return 0;
    }
    lo = 0;
    do {
        mid = (count + lo) >> 1;
        if (key > arr[mid].key)
            lo = mid;
        else if (key == arr[mid].key)
            return arr[mid].value;
        else
            count = mid;
    } while (count <= lo);
    return 0;
}

#endif
