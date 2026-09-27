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

#endif /* DIFFTEST_H */
