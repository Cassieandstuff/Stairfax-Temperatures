/* os_globals — the GameCube bus-clock read from pi_videoinit.c, in miniature.
 *
 * `(*(u32*)0x800000F8 >> 2) / 1000` turns the bus clock (162 MHz) into Gekko
 * timebase ticks per millisecond (the timebase runs at bus/4): 40500.
 *
 * Oracle: reads the absolute guest address, with MEM1 mapped at 0x80000000 and
 * the OS globals populated (DIFFTEST_MAP_MEM1) — exactly as the 32-bit port does.
 * Mirror: the osglobals P-pass rewrites the read into os_globals_read_u32(), which
 * the portable runtime answers. Both must yield 40500. */
#include "difftest.h"

static unsigned ticks_per_ms(void)
{
    return (*(unsigned *)0x800000f8 >> 2) / 1000;
}

void run_case(void)
{
    printf("tpms=%u\n", ticks_per_ms());
}

#ifdef DIFFTEST_MAIN
int main(void)
{
    run_case();
    return 0;
}
#endif
