/* RomCurveWalker with the two node-pointer fields. The oracle uses the GameCube's
 * 4-byte pointer slots (GuestPtr = u32 under DIFFTEST_ORACLE_LOWMEM), like the
 * real 32-bit build; the mirror and the negative control use native pointers. */
#ifndef ROMCURVE_SWAP_SHIM_H
#define ROMCURVE_SWAP_SHIM_H
#include "difftest.h"
typedef uint32_t u32; typedef float f32;
#if defined(DIFFTEST_ORACLE_LOWMEM)
typedef u32 GuestPtr;
#  define TO_GUEST(p) ((GuestPtr)(uintptr_t)(p))
#  define FROM_GUEST(g) ((void*)(uintptr_t)(g))
#else
typedef void* GuestPtr;
#  define TO_GUEST(p) ((GuestPtr)(p))
#  define FROM_GUEST(g) ((void*)(g))
#endif
#define OBJFSA_PHASE_LIMIT 1.0f
typedef struct RomCurveWalker {
    GuestPtr previousNode;
    GuestPtr nextNode;
    f32 phase;
} RomCurveWalker;
#endif
