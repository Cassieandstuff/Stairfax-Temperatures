/* Minimal compile surface for the extracted trackInitCollisionBuffers snippet.
 * Provides only the types, macros, and the allocator the extracted code needs —
 * everything else in the real TU's header web is irrelevant to this function. */
#ifndef TRACK_INIT_SHIM_H
#define TRACK_INIT_SHIM_H

#include "difftest.h"   /* mmAlloc (low-mem for oracle, malloc for mirror) + stdint */

typedef uint8_t  u8;
typedef uint32_t u32;
typedef int16_t  s16;
typedef int8_t   s8;
typedef float    f32;

/* TrackTriangle: only its size matters here (1200-entry buffer alloc). Real
 * stride is 0x4c bytes. */
typedef struct TrackTriangle { char _bytes[0x4c]; } TrackTriangle;

/* MapDynamicSlot: the extracted loop zeroes .cooldown across the slots. */
typedef struct MapDynamicSlot { int cooldown; char _pad[12]; } MapDynamicSlot;

#define MAP_DYNAMIC_SLOT_COUNT 64

#endif /* TRACK_INIT_SHIM_H */
