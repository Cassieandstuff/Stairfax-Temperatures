// mm_shim.c - host implementation of the game's memory manager (main/mm.h).
//
// The real src/main/mm.c can't be compiled on host: it aliases three named .bss
// globals as one MmGlobalLayout via (MmGlobalLayout*)gMmStoreArray, reading regions
// at +0x3F00 past a 0x80-byte array - a GameCube link-order overlay (same hazard as
// pad.c) that isn't reconstructable byte-neutrally. So we reimplement mm's OBSERVABLE
// contract cleanly: mmAlloc onto malloc, 32-byte aligned (the game assumes this for
// DVD reads), with a header so mm_free/getHeapItemSize work. The type/flag/region
// knobs are inert (one flat allocator), but the DEFERRED-FREE timing is faithful:
// mmFreeDeferred holds a pointer for gMmFreeDelay ticks before actually freeing, so
// game code that keeps using a "freed" pointer for a few frames stays valid (matching
// the console) instead of hitting a use-after-free.

#include "main/mm.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct MmHdr {
    void* base;   // original malloc pointer
    int   size;   // requested size
    int   magic;
} MmHdr;

#define MM_MAGIC 0x6D6D4831 /* "mmH1" */
#define MM_ALIGN 32

void* mmAlloc(int size, int type, int flag) {
    (void)type; (void)flag;
    if (size < 0) return NULL;
    size_t total = (size_t)size + sizeof(MmHdr) + MM_ALIGN;
    unsigned char* base = (unsigned char*)malloc(total);
    if (!base) return NULL;
    uintptr_t user = ((uintptr_t)base + sizeof(MmHdr) + (MM_ALIGN - 1)) & ~(uintptr_t)(MM_ALIGN - 1);
    MmHdr* h = (MmHdr*)(user - sizeof(MmHdr));
    h->base = base;
    h->size = size;
    h->magic = MM_MAGIC;
    return (void*)user;
}

void mm_free(void* ptr) {
    if (!ptr) return;
    MmHdr* h = (MmHdr*)((unsigned char*)ptr - sizeof(MmHdr));
    if (h->magic != MM_MAGIC) return; // not ours / double free guard
    h->magic = 0;
    free(h->base);
}

void  mmFree(void* p)          { mm_free(p); }

// --- deferred free: hold pointers gMmFreeDelay ticks before actually freeing ----
// The console defers so callers can keep touching a "freed" buffer for a few frames.
#define MM_DEFERRED_CAP 4096
static struct { void* ptr; int delay; } sDeferred[MM_DEFERRED_CAP];
static int sDeferredCount;
static int sFreeDelay = 2; // console default (mmInit sets gMmFreeDelay = 2)

void mmFreeDeferred(void* p) {
    if (!p) return;
    if (sDeferredCount >= MM_DEFERRED_CAP) { mm_free(p); return; } // overflow: free now
    sDeferred[sDeferredCount].ptr = p;
    sDeferred[sDeferredCount].delay = sFreeDelay > 0 ? sFreeDelay : 1;
    sDeferredCount++;
}

void mmFreeTick(int arg) {
    (void)arg;
    int i = 0;
    while (i < sDeferredCount) {
        if (--sDeferred[i].delay <= 0) {
            mm_free(sDeferred[i].ptr);
            sDeferred[i] = sDeferred[--sDeferredCount]; // swap-remove (matches mm.c)
        } else {
            i++;
        }
    }
}

void  mmInit(void)             { sDeferredCount = 0; sFreeDelay = 2; }

int getHeapItemSize(void* ptr) {
    if (!ptr) return 0;
    MmHdr* h = (MmHdr*)((unsigned char*)ptr - sizeof(MmHdr));
    return (h->magic == MM_MAGIC) ? h->size : 0;
}

// --- alignment helpers -----------------------------------------------------
int alignUp2(int v)    { return (v + 1) & ~1; }
int roundUpTo4(int v)  { return (v + 3) & ~3; }
int roundUpTo8(int v)  { return (v + 7) & ~7; }
int roundUpTo16(int v) { return (v + 15) & ~15; }
int roundUpTo32(int v) { return (v + 31) & ~31; }

// --- heap mode knobs (inert on host) ---------------------------------------
int mmSetFreeDelay(int v)               { int old = sFreeDelay; sFreeDelay = v; return old; }
int testAndSet_onlyUseHeaps1and2(int v) { (void)v; return 0; }
int testAndSet_onlyUseHeap3(int v)      { (void)v; return 0; }
int mmGetRegionForPtr(u8* ptr)          { (void)ptr; return 0; }
void mmSetTextureAllocationState(int s) { (void)s; }
int printHeapStats(int mode)            { (void)mode; return 0; }

// --- cache staging (host has no separate locked cache; use a real scratch buffer) ----
// The GC locked-cache staging area. The model render path stages matrices here:
// modelInitMtxs copies the joint bank to getCache()+0x2700, and renderOpMatrix/modelBuildPosNrmMtxs
// read/write pos matrices at +0, normal/tex at +0x12C0. Must be a persistent, aligned buffer big
// enough for +0x2700 + (jointCount+extra)*0x40 (~0x3000 for 36 joints); 0x8000 is ample.
void* getCache(void) {
    static _Alignas(32) unsigned char gCacheBuf[0x8000];
    return gCacheBuf;
}
void  cacheQueueWait(int sync)                    { (void)sync; }
// Faithful to mm.c's copyToCache non-locked-cache branch: the count argument is a cache-line
// count (0x20 bytes each), NOT a byte count - len = count ? count<<5 : 0x1000. modelInitMtxs
// relies on this: its full-chunk copies pass count=0 (=> 0x1000 bytes) and the final passes
// (jointCount+extra)*2 lines. The old memcpy(dst,src,n) copied n BYTES, so a 36-joint model
// staged only 72 of its 2304 joint-matrix bytes and every skinned draw read a garbage bank.
void  copyToCache(void* dst, void* src, u32 count) {
    if (dst && src) memcpy(dst, src, count ? ((size_t)count << 5) : 0x1000);
}
void  memcpyToCache(void* dst, void* src, u32 n)  { if (dst && src) memcpy(dst, src, n); }

// --- atomic singly-linked list (node's first word is the next pointer) ------
void AtomicSList_Push(void** list, void* node) {
    if (!list || !node) return;
    *(void**)node = *list;
    *list = node;
}
void* AtomicSList_Pop(void** list) {
    if (!list || !*list) return NULL;
    void* n = *list;
    *list = *(void**)n;
    return n;
}

// --- pools / stores --------------------------------------------------------
void* stackCreate(int count, int size) {
    return malloc((size_t)count * (size_t)size);
}
int   mmCreateMemoryStore(int size) { (void)size; return 0; }
void* mmAllocateFromFBMemoryStore(int handle, int size) { (void)handle; return malloc((size_t)size); }
