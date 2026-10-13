/* Host-side model of the GameCube OS globals for the mirror target.
 *
 * Values mirror what src/hal/os_shim.c writes into mapped MEM1 on the 32-bit port:
 *   0x800000F8  bus clock   = 162 MHz   (Gekko timebase runs at bus/4)
 *   0x800000FC  core/CPU    = 40.5 MHz-ish base
 *   0x8000002C  console type = 0x10000006 (retail)
 */
#include "stairfax_os.h"

#define MEM1_BASE 0x80000000u

/* GameCube retail constants (same as src/hal/os_shim.c). */
#define OSG_BUS_CLOCK    162000000u
#define OSG_CORE_CLOCK    40500000u
#define OSG_CONSOLE_TYPE 0x10000006u

uint32_t os_globals_read_u32(uint32_t guest_addr)
{
    uint32_t off = guest_addr - MEM1_BASE;   /* offset into the OS-globals area */
    switch (off) {
    case 0x00F8: return OSG_BUS_CLOCK;
    case 0x00FC: return OSG_CORE_CLOCK;
    case 0x002C: return OSG_CONSOLE_TYPE;
    default:     return 0u;
    }
}
