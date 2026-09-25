#pragma once
// Host shadow: forwards to the umbrella dolphin/os.h so a direct
// #include "dolphin/os/OSFastCast.h" gets one consistent host definition.
#include "dolphin/os.h"

// OSFastCast integer<->float conversions. On PPC these are single paired-single
// cast instructions; on host a plain conversion is equivalent.
#ifdef __cplusplus
extern "C" {
#endif
static inline void OSs8tof32 (const s8*  in, float* out) { *out = (float)*in; }
static inline void OSu8tof32 (const u8*  in, float* out) { *out = (float)*in; }
static inline void OSs16tof32(const s16* in, float* out) { *out = (float)*in; }
static inline void OSu16tof32(const u16* in, float* out) { *out = (float)*in; }
#ifdef __cplusplus
}
#endif
