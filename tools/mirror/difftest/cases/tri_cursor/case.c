/* tri_cursor — a miniature of track_dolphin.c's trackBuildBlockTriangles.
 *
 * It reproduces the exact pointer-width hazards the P2 rewriter targets:
 *   - a file-scope pointer stored in `int` (gTriBufEnd),
 *   - an allocation truncated by `(int)mmAlloc(...)`,
 *   - a K&R function that returns a pointer through an `int` return type,
 *   - `(T*)(int)x` pointer round-trips,
 *   - local ints holding pointers (buf, end).
 *
 * The oracle (untransformed) is only correct because the harness confines its
 * heap to the low 4 GB. The mirror (rewritten to uintptr_t) must produce the
 * same canonical output with an unconfined heap. Same source, two builds.
 */
#include "difftest.h"

int gTriBufEnd;                         /* pointer stored in int (hazard) */

int triBuild(cur, n)                    /* K&R; returns cursor pointer via int */
int cur;
int n;
{
    int i;
    for (i = 0; i < n; i++) {
        *(unsigned *)cur = (unsigned)(i * 7 + 1);   /* write a packed value */
        cur += 4;
        if ((unsigned)cur >= (unsigned)gTriBufEnd)   /* end-of-buffer guard */
            break;
    }
    return cur;
}

void run_case(void)
{
    int buf = (int)mmAlloc(64, 0, 0);   /* (int)mmAlloc truncation hazard */
    int end;
    unsigned *p;
    unsigned sum = 0;
    int count, i;

    gTriBufEnd = buf + 64;
    end = triBuild(buf, 16);
    count = (end - buf) / 4;
    p = (unsigned *)(int)buf;           /* (T*)(int) round-trip hazard */
    for (i = 0; i < count; i++)
        sum += p[i];

    printf("count=%d sum=%u\n", count, sum);
}

#ifdef DIFFTEST_MAIN
int main(void)
{
    run_case();
    return 0;
}
#endif
