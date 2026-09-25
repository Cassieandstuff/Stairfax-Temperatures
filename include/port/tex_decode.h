#pragma once
// tex_decode.h - GameCube (GX) texture formats -> linear RGBA8.
//
// GC textures are stored swizzled in 4x4 / 8x4 tiles with several pixel encodings.
// The RHI deals only in linear RGBA8, so gx_shim decodes GX texels to RGBA8 here
// before uploading. Backend-independent. CMPR/palettized formats come later.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// GX texture format ids (subset; values match dolphin/gx GXTexFmt). Guard so this
// coexists with dolphin/gx/GXEnum.h when both are included (e.g. in map_view).
#ifndef GX_TF_I4
enum {
    GX_TF_I4     = 0,
    GX_TF_I8     = 1,
    GX_TF_IA4    = 2,
    GX_TF_IA8    = 3,
    GX_TF_RGB565 = 4,
    GX_TF_RGB5A3 = 5,
    GX_TF_RGBA8  = 6,
    GX_TF_CMPR   = 14,
};
#endif

// Decode a GX texture to linear, row-major RGBA8 (w*h*4 bytes). Returns a malloc'd
// buffer the caller frees, or NULL if the format is not yet supported.
uint8_t* gxTexDecode(int fmt, int w, int h, const uint8_t* src);

// Self-check: encodes known texels in a couple of formats, decodes, verifies.
// Returns 0 on success, non-zero on the first failing case.
int gxTexDecodeSelfTest(void);

#ifdef __cplusplus
}
#endif
