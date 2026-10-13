/* Driver for the extracted modelRenderInterpolateRootTransform.
 *
 * Runs two deterministic synthetic animations over the same bitstream (window A,
 * window B one stride later). Descriptors are chosen so the two scenarios
 * together exercise BOTH bitstream refill paths:
 *   A: 64 bits overflow inside an iteration    -> RENDER_BITS_REFILL_NEXT
 *   B: 64 bits overflow on an iteration's first -> RENDER_BITS_REFILL
 *      component
 * (one call can't hit both: after either refill at most ~52 bits remain to
 * consume.) Prints the decoded rotation and position for each.
 *
 * Every buffer the function packs into an integer (stream, descriptors, both
 * output arrays) comes from mmAlloc: low-4 GB in the oracle (so its u32 packing
 * is lossless, as on the real 32-bit build), ordinary high-heap malloc in the
 * mirror. The printed values are decoded data, not addresses, so a correct
 * transform must reproduce them exactly. Without the transform the high-heap
 * pointers truncate and the decode faults (the negative control). */
#include "shim.h"
#include "snippet.c"
#include <stdio.h>

static u8 *gStream;

static void run_scenario(const char *label, const u16 *words, unsigned n, f32 phase)
{
    unsigned i;
    u16 *desc = (u16 *)mmAlloc(64, 0, 0);
    s16 *outPosition = (s16 *)mmAlloc(16, 0, 0);
    s16 *outRotation = (s16 *)mmAlloc(16, 0, 0);
    ObjAnimState anim;

    desc[0] = desc[1] = 0;                      /* 4-byte header before descriptors */
    for (i = 0; i < n; i++)
        desc[2 + i] = words[i];
    for (i = 0; i < 3; i++)
        outPosition[i] = outRotation[i] = 0;

    anim.framePhase = phase;
    anim.frameStreamCursor = gStream;
    anim.moveFrameData = (ObjAnimFrameCommand *)desc;
    anim.frameStreamStride = 24;

    modelRenderInterpolateRootTransform(&anim, outPosition, outRotation);

    printf("%s rot=%d,%d,%d pos=%d,%d,%d\n", label,
           outRotation[0], outRotation[1], outRotation[2],
           outPosition[0], outPosition[1], outPosition[2]);
}

int main(void)
{
    /* A: 15+14+13 | 15+0+12 -> 69 > 64 on iteration 2's third component */
    static const u16 a[] = { 0x1A3F, 0x2B3E, 0x3C3D, 0x4D1F, 0x5E30, 0x6F3C,
                             0x713B, 0x823A, 0x9339 };
    /* B: 15+15+15 | 15+0+4 = 64 (no overflow) | 15 -> 79 > 64 on iteration 3's first */
    static const u16 b[] = { 0x7A3F, 0x6B3F, 0x5C3F, 0x113F, 0x1230, 0x1334,
                             0x143F, 0x153F, 0x163F };
    unsigned i;

    gStream = (u8 *)mmAlloc(256, 0, 0);
    for (i = 0; i < 256; i++)
        gStream[i] = (u8)(i * 37u + 11u);

    run_scenario("A", a, sizeof a / sizeof a[0], 2.375f);   /* frac 0.375 -> 6144 */
    run_scenario("B", b, sizeof b / sizeof b[0], 7.8125f);  /* frac 0.8125 -> 13312 */
    return 0;
}
