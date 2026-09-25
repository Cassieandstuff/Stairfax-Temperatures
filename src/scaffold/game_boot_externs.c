// game_boot_externs.c - link-time home for the pi_dolphin module globals that
// videoInit()/waitNextFrame() reference.
//
// In the full game these live in pi_dolphin.c alongside videoInit; pulling that
// whole TU in would cascade into the entire game (models, objects, DVD, ...).
// For the boot milestone we run ONLY the video-init path, so we give its globals
// a standalone definition here. Types come from the real declaring headers so the
// definitions bind exactly to the extern decls the game TU compiled against.
//
// This defines no game behaviour - it is storage plus two callback stubs the GP
// would invoke on FIFO breakpoints / errors that never fire on the host.

#include "main/pi_dolphin.h"   // most globals + their exact types
#include "main/frame_timing.h" // timeDelta et al.
#include "main/fileio.h"       // gDvdErrorPauseActive

// --- frame timing ----------------------------------------------------------
f32 timeDelta;
f32 oneOverTimeDelta;
u8  framesThisStep;
u8  framesThisStepUnclamped;

// --- fileio ----------------------------------------------------------------
u8 gDvdErrorPauseActive;

// --- pi_dolphin pool constants (named .sdata2 in the retail data segment) ---
u8    lbl_803DCD00;
int   lbl_803DCCFC;
u8    lbl_803DCCF8;
int   lbl_803DCCF4;
char* lbl_803DCCE0;

// --- framebuffers / FIFO ---------------------------------------------------
void*      externalFrameBuffer0;
void*      externalFrameBuffer1;
void*      renderFrameBuffer;
void*      displayFrameBuffer;
u32        gGxFifoSize;
void*      gGxFifoBase;
GXFifoObj* gGxFifoObj;

// --- copy pipeline ---------------------------------------------------------
int     gDispCopyYScaleLines;
GXColor gEfbCopyClearColor;
u8      gDispCopyFilterWeights[8];

// --- frame pacing / video state --------------------------------------------
char        gVideoFlipWaitQueue;
char        gVideoFlipQueueBuffer[10 * 3 * sizeof(void*)];
f32         gFrameElapsedMs;
f32         gFrameStepRemainder;
u8          gGpuHangRecoveryEnabled;
volatile int gGpuStallRetraceCount;
u8          gGxBreakPtEnabled;
u8          gVideoBlackScreenFrameCount;
u16         gGxDrawSyncToken;
OSThread*   gVideoWaitThread;
OSStopwatch gFrameStopwatch;
RingBufferQueue gVideoFlipQueue;
// gLoadingScreenTextures[0x40000] now provided with the REAL retail asset by
// src/main/boot_logo.c (gitignored gLoadingScreenTextures.inc); videoInit copies
// it to OSGetArenaHi()-0x40000 and initLoadingScreenTextures reads it back.

// --- HUD ortho matrix (defined in intersect.c in the full game) ------------
f32 hudMatrix[4][4];

// --- progressive-scan render mode (SDK data, from GXFrameBuf.c) -------------
GXRenderModeObj GXNtsc480Prog = {
    2, 640, 480, 480, 40, 0, 640, 480, 0, 0, 0,
    {{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}},
    {0,0,21,22,21,0,0}
};

// --- GP breakpoint / error callbacks (never fire on the host) --------------
void gpuErrorHandler(u32 retraceCount)   { (void)retraceCount; }
void videoBreakPointCallback(void)       { }
