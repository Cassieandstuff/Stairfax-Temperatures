// vi_shim.c - VI (video interface) on the host, backed by the RHI + platform window.
//
// The game rides the VI retrace as its frame heartbeat: VIInit() once, then a
// per-frame VIWaitForRetrace() that on hardware blocks until vblank and fires the
// registered pre/post-retrace callbacks (the game hangs its buffer swap on the
// pre-retrace one). Here VI owns the window/RHI/swapchain; VIWaitForRetrace pumps
// events, fires the callbacks, and presents a frame through the RHI.
//
// There is no GX draw path yet, so the presented frame is a flat clear: black when
// VISetBlack(1), otherwise a "VI alive" color. That is the honest state of the
// port - the real framebuffer contents arrive when gx_shim renders into it.

#include "port/vi_shim.h"
#include "port/plat_window.h"
#include "port/renderer/rhi.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct ViState {
    PlatWindow*      window;
    RhiInstance*     rhi;
    RhiSwapchain*    swapchain;
    int              width, height;
    bool             black;
    bool             initialized;
    uint32_t         retraceCount;
    VIRetraceCallback preCb;
    VIRetraceCallback postCb;
    void*            nextFrameBuffer;
    bool             frameOpen;
} ViState;

static ViState gVi;

// Open a frame for drawing: begin + clear to the current background. Game GX draws
// (issued before the next VIWaitForRetrace) accumulate into this open frame; the
// retrace then presents it. Clearing here - not at present - is what lets the
// geometry survive to the screen.
// Optional clear-color override (e.g. the sky day/night tint). Off until set.
static int   gClearSet;
static float gClearRGB[3];
void vi_set_clear_color(float r, float g, float b) {
    gClearRGB[0] = r; gClearRGB[1] = g; gClearRGB[2] = b; gClearSet = 1;
}

static void viOpenFrame(void) {
    if (!gVi.initialized || gVi.frameOpen) return;
    rhi_beginFrame(gVi.rhi);
    if (gVi.black)      rhi_clear(gVi.rhi, 0.0f, 0.0f, 0.0f, 1.0f);
    else if (gClearSet) rhi_clear(gVi.rhi, gClearRGB[0], gClearRGB[1], gClearRGB[2], 1.0f);
    else                rhi_clear(gVi.rhi, 0.10f, 0.18f, 0.10f, 1.0f);
    gVi.frameOpen = true;
}

static RhiBackend vi_pick_backend(void) {
    const char* g = getenv("STAIRFAX_GFX");
    if (g) {
        if (!strcmp(g, "vk") || !strcmp(g, "vulkan")) return RHI_BACKEND_VULKAN;
        if (!strcmp(g, "d3d12"))                      return RHI_BACKEND_D3D12;
        if (!strcmp(g, "d3d11"))                      return RHI_BACKEND_D3D11;
        if (!strcmp(g, "gl"))                         return RHI_BACKEND_GL;
    }
    return RHI_BACKEND_AUTO;
}

void VIInit(void) {
    if (gVi.initialized) return;
    memset(&gVi, 0, sizeof(gVi));
    gVi.width  = 1280;
    gVi.height = 720;
    gVi.black  = true;

    gVi.window = plat_window_create("Stairfax Temperatures (PC Port)", gVi.width, gVi.height);
    if (!gVi.window) { fprintf(stderr, "[vi] failed to create window\n"); return; }

    RhiCreateInfo ci = {0};
    ci.backend      = vi_pick_backend();
    ci.windowHandle = plat_window_native_handle(gVi.window);
    ci.width        = gVi.width;
    ci.height       = gVi.height;
    ci.vsync        = true;
    ci.debug        = false;
    ci.appName      = "Stairfax Temperatures";

    gVi.rhi = rhi_create(&ci);
    if (!gVi.rhi) { fprintf(stderr, "[vi] rhi_create failed\n"); return; }

    gVi.swapchain = rhi_swapchainCreate(gVi.rhi, ci.windowHandle, gVi.width, gVi.height, ci.vsync);
    if (!gVi.swapchain) { fprintf(stderr, "[vi] swapchain create failed\n"); return; }

    gVi.initialized = true;
    printf("[vi] init: %s %dx%d\n", rhi_backendName(rhi_getBackend(gVi.rhi)), gVi.width, gVi.height);
    viOpenFrame(); // first frame ready to receive draws
}

void VIConfigure(GXRenderModeObj* rmode) {
    // The render mode carries the GC framebuffer dimensions (e.g. 640x480/528).
    // We present into a host-sized swapchain and will scale the framebuffer later;
    // for now the mode only marks that VI has been configured.
    (void)rmode;
}

void VISetNextFrameBuffer(void* fb) {
    gVi.nextFrameBuffer = fb;
}

void VISetBlack(bool black) {
    gVi.black = black;
}

void VIFlush(void) {
    // Commit pending VI register writes. No deferred state on the host yet.
}

void VIWaitForRetrace(void) {
    if (!gVi.initialized) return;

    // Window/OS events first; a close request stops the heartbeat cleanly.
    if (!plat_window_pump(gVi.window)) {
        // Leave initialized so callers can detect close via vi_shim_shouldClose.
    }

    // Pre-retrace callback: the game swaps display buffers here.
    if (gVi.preCb) gVi.preCb(gVi.retraceCount);

    // Present the frame the game just drew into (or a bare clear if it drew
    // nothing). The GX draw path (gx_draw) has been filling this open frame.
    if (!gVi.frameOpen) viOpenFrame();
    rhi_endFrame(gVi.rhi);
    rhi_present(gVi.rhi, gVi.swapchain);
    gVi.frameOpen = false;

    // Post-retrace callback: the game runs its GPU error/metrics handler here.
    if (gVi.postCb) gVi.postCb(gVi.retraceCount);

    gVi.retraceCount++;

    // Reopen a fresh frame so the next iteration's GX draws have somewhere to land.
    viOpenFrame();
}

int VIGetNextField(void)  { return (int)(gVi.retraceCount & 1); }
int VIGetTvFormat(void)   { return 0; /* NTSC */ }
int VIGetDTVStatus(void)  { return 0; }

VIRetraceCallback VISetPreRetraceCallback(VIRetraceCallback cb) {
    VIRetraceCallback prev = gVi.preCb;
    gVi.preCb = cb;
    return prev;
}
VIRetraceCallback VISetPostRetraceCallback(VIRetraceCallback cb) {
    VIRetraceCallback prev = gVi.postCb;
    gVi.postCb = cb;
    return prev;
}

PlatWindow*  vi_host_window(void) { return gVi.window; }
RhiInstance* vi_host_rhi(void)    { return gVi.rhi; }

void* vi_shim_getWindow(void) {
    return gVi.window ? plat_window_native_handle(gVi.window) : NULL;
}
void vi_shim_pollEvents(void) {
    if (gVi.window) plat_window_pump(gVi.window);
}
int vi_shim_shouldClose(void) {
    if (!gVi.window) return 1;
    // plat_window_pump returns false once closed; probe without consuming more.
    return plat_window_pump(gVi.window) ? 0 : 1;
}
