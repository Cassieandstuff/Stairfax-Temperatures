/* Behavioral-diff test support.
 *
 * A diff case is compiled twice from the SAME source: once as the oracle (the
 * untransformed decomp-style code) and once as the mirror (the P2 rewriter's
 * output). Both build 64-bit here; the oracle is kept correct by confining its
 * allocations to the low 4 GB (MAP_32BIT) so its pointer-in-int truncation is
 * lossless — i.e. it behaves exactly as the real 32-bit build would. The mirror
 * uses normal high-address malloc and must still match.
 *
 * A machine with a real 32-bit toolchain can instead build the oracle with
 * `-m32` and drop DIFFTEST_ORACLE_LOWMEM; the harness output is the same.
 */
#ifndef DIFFTEST_H
#define DIFFTEST_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* mmAlloc mirrors the game allocator's (size, tag, align) shape. */
#if defined(DIFFTEST_ORACLE_LOWMEM)
#  include <sys/mman.h>
static void *mmAlloc(unsigned size, unsigned tag, int align) {
    (void)tag; (void)align;
    void *p = mmap(NULL, size ? size : 1, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (p == MAP_FAILED) { perror("mmap MAP_32BIT"); exit(2); }
    return p;
}
#else
static void *mmAlloc(unsigned size, unsigned tag, int align) {
    (void)tag; (void)align;
    void *p = malloc(size ? size : 1);
    if (!p) { perror("malloc"); exit(2); }
    return p;
}
#endif

/* OS-globals cases: the oracle reads absolute guest addresses (e.g.
 * *(u32*)0x800000F8), exactly as the 32-bit port does with MEM1 mapped at
 * 0x80000000. Under DIFFTEST_MAP_MEM1 we map one page there and populate the
 * OS globals to the retail-console values, so the untransformed read works and
 * is the ground truth. The mirror instead calls os_globals_read_u32 and must
 * match. Without this map (the negative control) the absolute read faults —
 * confirming the hazard the rewrite fixes. */
#if defined(DIFFTEST_MAP_MEM1)
#  include <sys/mman.h>
__attribute__((constructor))
static void difftest_map_mem1(void) {
    void *p = mmap((void *)0x80000000u, 0x1000,
                   PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (p == MAP_FAILED || p != (void *)0x80000000u) {
        perror("mmap MEM1@0x80000000");
        exit(2);
    }
    *(uint32_t *)0x800000F8u = 162000000u; /* bus clock  (matches os_shim.c) */
    *(uint32_t *)0x800000FCu = 40500000u;  /* core clock */
    *(uint32_t *)0x8000002Cu = 0x10000006u; /* console type: retail */
}
#endif

/* Aliased addresses. A pointer that is narrowed to 32 bits on BOTH sides of a
 * comparison (`(u32)a == (u32)b`, or a u32 record checked against a u32 copy)
 * still compares correctly for distinct objects, unless their addresses share
 * the low 32 bits. difftest_alias_alloc(pair, member, size) returns zeroed
 * storage for member 0, 1, ... of alias pair `pair`. On the high-heap builds
 * (the mirror and the untransformed control) the members of one pair sit
 * exactly 4 GiB apart, so their low 32 bits are equal. The low-memory oracle
 * can't alias, so it falls back to mmAlloc and gives the intended result. */
#if !defined(DIFFTEST_ORACLE_LOWMEM)
#  include <string.h>
#  include <sys/mman.h>
#  define DIFFTEST_ALIAS_BASE ((uintptr_t)0x7e0000001000ull)
static void *difftest_alias_alloc(int pair, int member, unsigned size) {
    uintptr_t addr = DIFFTEST_ALIAS_BASE + (uintptr_t)pair * 0x100000u + ((uintptr_t)member << 32);
    unsigned len = (size + 0xFFFu) & ~0xFFFu;
    void *p = mmap((void *)addr, len ? len : 0x1000, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (p == MAP_FAILED || (uintptr_t)p != addr) {
        perror("mmap alias");
        exit(2);
    }
    return p;                                   /* anonymous pages: already zero */
}
#else
static void *difftest_alias_alloc(int pair, int member, unsigned size) {
    (void)pair; (void)member;
    void *p = mmAlloc(size, 0, 0);
    unsigned i;
    for (i = 0; i < size; i++) ((unsigned char *)p)[i] = 0;
    return p;
}
#endif

#endif /* DIFFTEST_H */
