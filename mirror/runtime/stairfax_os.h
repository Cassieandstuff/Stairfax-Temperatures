/* Portable OS-globals accessor for the 64-bit mirror target (ADR 0001).
 *
 * The GameCube exposes a block of "OS globals" in low memory (bus/CPU clock,
 * console type, ...). The real hardware and the 32-bit port read them through
 * absolute guest addresses (e.g. *(u32*)0x800000F8). The mirror target has no
 * fixed guest memory map, so the P-pass rewrites those reads into calls here and
 * this accessor answers from a host-side model of the region.
 *
 * See src/hal/os_shim.c for the 32-bit port's equivalent (which populates the
 * same values into a mapped MEM1). Keep the two in sync.
 */
#ifndef STAIRFAX_OS_H
#define STAIRFAX_OS_H

#include <stdint.h>

/* Read a 32-bit OS-globals word by its guest address (0x8000_00xx range).
 * Returns the value the retail console reports; 0 for an unmodeled offset. */
uint32_t os_globals_read_u32(uint32_t guest_addr);

#endif /* STAIRFAX_OS_H */
