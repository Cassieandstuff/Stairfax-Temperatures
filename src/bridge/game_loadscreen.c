// game_loadscreen.c - port glue for the boot loading-screen textures (engine/50).
//
// Two jobs, both required to run the real dlls/engine/50/50.c load path natively:
//
// 1. BE->host header fixup. gLoadingScreenTextures (src/main/boot_logo.c) is the
//    verbatim retail asset: three back-to-back Texture records (Nintendo, Rareware,
//    Dolby logos) whose 0x60-byte headers are big-endian. videoInit() copies the
//    whole blob to OSGetArenaHi()-0x40000 and initLoadingScreenTextures() reads each
//    header's width/height as native u16. On this little-endian host those two u16
//    fields would read swapped (garbage -> wrong GXGetTexBufferSize -> wrong arena
//    layout -> crash), so we byte-swap them in the source blob BEFORE videoInit runs.
//    The pixel data needs no swap: gxTexDecode handles GC texel endianness per format
//    (the same decoder that renders model textures correctly). We arm at CRT init,
//    ahead of main()/videoInit, exactly like the WGPIPE trap.
//
// 2. Retail .sdata2 constants 50.c references but that live in a data TU not yet
//    ported. Values derived from 50.c's own fade math (see below); replace with the
//    exact retail floats if/when that data TU is brought up.

#include "dolphin/types.h"
#include "main/pi_dolphin.h"   // extern u8 gLoadingScreenTextures[]
#include "port/vi_shim.h"
#include "port/plat_window.h"
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

// --- retail constants referenced by 50.c (derived stand-ins) ----------------
// runLoadingScreens fades alpha as alphaMax * counter / fadeFrames, reaching full
// (0xff) at the fade-in boundary counter==0x1e (30). alphaMax/fadeFrames*30 = 255
// => alphaMax 255, fadeFrames 30. The lbl_ floats feed GXInitTexObjLOD (LOD 0) and
// the title fade timer seed (0).
f32 gTitleScreenInitAlphaMax = 255.0f;
f32 gTitleScreenInitFadeFrames = 30.0f;
f32 lbl_803E1CF0 = 0.0f;
f32 lbl_803E1D00 = 0.0f;

// --- BE->host header fixup --------------------------------------------------
// Record base offsets in the blob (docs/orig/embedded_assets.md, boot_logo.c):
// Nintendo @ +0x0, Rareware @ +0x131E0, Dolby @ +0x1D240. width @ +0xA, height @ +0xC.
static const u32 kLoadTexRecordOffsets[3] = { 0x0u, 0x131E0u, 0x1D240u };

static void swapU16(u8* p) { u8 t = p[0]; p[0] = p[1]; p[1] = t; }

static void stairfax_loadscreen_fixup(void) {
    static int done = 0;
    int i;
    if (done) return;
    done = 1;
    for (i = 0; i < 3; ++i) {
        u8* rec = gLoadingScreenTextures + kLoadTexRecordOffsets[i];
        swapU16(rec + 0xA);   // width
        swapU16(rec + 0xC);   // height
    }
}

// Run before main() -> videoInit()'s memcpy of the blob to arena-top. The blob is
// a statically-initialised array (ready at load), so the CRT-init timing is safe.
static int stairfax_loadscreen_autofixup(void) { stairfax_loadscreen_fixup(); return 0; }
#pragma section(".CRT$XCU", read)
__declspec(allocate(".CRT$XCU")) static int (*stairfax_loadscreen_fixup_ptr)(void) = stairfax_loadscreen_autofixup;

// --- dev-only: hold the boot loading loop open to see the logos ---------------
// gameLoop's init loop spins `while (filesDone==0 || audioDone==0)` and calls
// runLoadingScreens() each pass; with the initLoadFiles stub returning 1 immediately
// the logos flash for ~1 frame. This lets the loading phase be held for N frames
// (STAIRFAX_LOADING_HOLD=N) and a window frame captured mid-fade
// (STAIRFAX_LOADING_CAP=path, STAIRFAX_LOADING_CAP_FRAME=frame). initLoadFiles (the
// port stub) returns this: 0 = keep holding, 1 = done -> proceed to the game.
// gx_draw's RHI is normally wired lazily by game_scene.cpp's first sceneRender -
// but that runs only AFTER the boot loading loop, so runLoadingScreens' logo draws
// land in a gx_draw with rhi==NULL and are dropped (black screen). The loading loop
// is the game's own gameLoop init phase, so connect the renderer here, before the
// first runLoadingScreens gets to draw. Wire the RHI ONLY - do NOT gx_draw_init(),
// which would memset the vertex-attribute table videoInit already programmed for the
// boot/hud draws (that reset left the hud verts decoding at stride 4 -> off-screen).
extern struct RhiInstance* vi_host_rhi(void);
extern void gx_draw_setRhi(struct RhiInstance* rhi, struct RhiSwapchain* sc);
extern void gx_draw_reset_matrices(void);  // identity gProj/gPosMtx, preserves the VAT
extern void VIWaitForRetrace(void);  // vi_shim.c: end+present the open frame, begin+clear the next

static void stairfax_loadscreen_ensure_rhi(void) {
    static int wired = 0;
    struct RhiInstance* rhi;
    if (wired) return;
    rhi = vi_host_rhi();
    if (!rhi) return;                    // VI not up yet; try again next frame
    gx_draw_setRhi(rhi, 0);
    gx_draw_reset_matrices();            // identity transform; hud sets its ortho per-draw
    wired = 1;
}

int stairfax_loadscreen_filesdone(void) {
    const char* hold = getenv("STAIRFAX_LOADING_HOLD");
    static int frame = 0;
    int holdFrames, capFrame;
    const char* cap;
    stairfax_loadscreen_ensure_rhi();    // connect the renderer for the logo draws
    // The boot loading loop has no present of its own (waitNextFrame only sleeps),
    // so drive one retrace per iteration: present the previous frame's runLoadingScreens
    // draws and open a fresh cleared frame for this iteration's draws.
    VIWaitForRetrace();
    ++frame;
    cap = getenv("STAIRFAX_LOADING_CAP");
    if (cap) {
        const char* cf = getenv("STAIRFAX_LOADING_CAP_FRAME");
        capFrame = cf ? atoi(cf) : 50;
        if (frame == capFrame && vi_host_window()) {
            if (plat_window_capture_bmp(vi_host_window(), cap))
                fprintf(stderr, "[loadscreen] captured loading frame %d -> %s\n", frame, cap);
        }
    }
    if (!hold) return 1;                 // not holding: normal fast boot
    holdFrames = atoi(hold);
    if (holdFrames <= 0) holdFrames = 200;
    return frame >= holdFrames ? 1 : 0;
}
