// os_shim.c - host implementation of the Dolphin OS API (see dolphin/os.h shadow).
//
// The console's OS handles interrupts, caches, threads, and a boot-time arena.
// On a coherent multitasking host most of this collapses: cache ops are no-ops,
// interrupt enable/disable is a bookkeeping flag, and time comes from the host
// clock. Threads/messages/mutexes are stubbed for now (the game boots effectively
// single-threaded); they get real host backing when a subsystem needs concurrency.

#include "dolphin/os.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

// --- error / report --------------------------------------------------------
void OSReport(const char* msg, ...) {
    va_list ap; va_start(ap, msg); vprintf(msg, ap); va_end(ap);
}
void OSVReport(const char* msg, va_list list) { vprintf(msg, list); }

void OSPanic(const char* file, int line, const char* msg, ...) {
    fprintf(stderr, "OSPanic %s:%d: ", file, line);
    va_list ap; va_start(ap, msg); vfprintf(stderr, msg, ap); va_end(ap);
    fputc('\n', stderr);
    abort();
}
void OSHalt(const char* msg) { fprintf(stderr, "OSHalt: %s\n", msg ? msg : ""); abort(); }

OSErrorHandler OSSetErrorHandler(OSError error, OSErrorHandler handler) {
    (void)error; (void)handler; return NULL;
}

// --- interrupts (single flag; no real ISRs on the host) --------------------
static BOOL gInterruptsEnabled = TRUE;
BOOL OSDisableInterrupts(void) { BOOL prev = gInterruptsEnabled; gInterruptsEnabled = FALSE; return prev; }
BOOL OSEnableInterrupts(void)  { BOOL prev = gInterruptsEnabled; gInterruptsEnabled = TRUE;  return prev; }
BOOL OSRestoreInterrupts(BOOL level) { BOOL prev = gInterruptsEnabled; gInterruptsEnabled = level; return prev; }

// --- cache (host is coherent) ----------------------------------------------
void DCInvalidateRange(void* addr, u32 n)   { (void)addr; (void)n; }
void DCFlushRange(void* addr, u32 n)        { (void)addr; (void)n; }
void DCStoreRange(void* addr, u32 n)        { (void)addr; (void)n; }
void DCFlushRangeNoSync(void* addr, u32 n)  { (void)addr; (void)n; }
void DCStoreRangeNoSync(void* addr, u32 n)  { (void)addr; (void)n; }
void DCZeroRange(void* addr, u32 n)         { if (addr && n) memset(addr, 0, n); }
void DCTouchRange(void* addr, u32 n)        { (void)addr; (void)n; }
void ICInvalidateRange(void* addr, u32 n)   { (void)addr; (void)n; }
void ICFlashInvalidate(void)                { }
void DCEnable(void)                         { }

// --- time ------------------------------------------------------------------
u32 OSGetTickRate(void) { return 162000000u; } // GC bus 243MHz -> TB = bus/2? use 162MHz TB-like base

OSTime OSGetTime(void) {
#if defined(_WIN32)
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (OSTime)((c.QuadPart * (long long)OSGetTickRate()) / f.QuadPart);
#else
    return 0;
#endif
}
OSTick OSGetTick(void) { return (OSTick)OSGetTime(); }

// --- MEM1 backing ----------------------------------------------------------
// The console's 24MB MEM1 lives at the fixed physical/cached address 0x80000000,
// and the game reads console globals (bus clock at 0x800000F8, console type, ...)
// AND stores/sub-allocates through absolute 0x8xxxxxxx pointers. Rather than
// rewrite every such access, we map MEM1 at its real address: on 64-bit Windows a
// /LARGEADDRESSAWARE 32-bit process owns the full 4GB user range, so 0x80000000 is
// reservable. Every absolute guest access then simply lands in real memory - the
// most faithful backing, and it keeps stored guest pointers meaningful.
//
// The arena (framebuffers, GX FIFO, game heap) is carved from MEM1 above the OS
// low-memory globals, exactly as videoInit expects.
#define MEM1_BASE  0x80000000u
#define MEM1_SIZE  0x01800000u   // 24MB
#define OS_LOWMEM_END 0x80003100u // OS globals/BI2/dbg live below this on real HW

static u8*  gMem1     = NULL;   // actual mapped base (== MEM1_BASE on success)
static u8*  gArenaLo  = NULL;
static u8*  gArenaHi  = NULL;

static void osArenaInit(void) {
    if (gMem1) return;
#if defined(_WIN32)
    // Reserve+commit exactly at the GameCube MEM1 address.
    gMem1 = (u8*)VirtualAlloc((void*)MEM1_BASE, MEM1_SIZE,
                              MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!gMem1) {
        // Fallback: let the OS pick an address. Absolute 0x8xxxxxxx reads will then
        // fault, but relative arena use still works - surfaces the mapping failure.
        fprintf(stderr, "[os] WARNING: could not map MEM1 at 0x%08X (err %lu); "
                        "absolute guest addresses will fault\n",
                        MEM1_BASE, (unsigned long)GetLastError());
        gMem1 = (u8*)VirtualAlloc(NULL, MEM1_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    }
#else
    gMem1 = (u8*)malloc(MEM1_SIZE);
#endif
    // Populate the OS low-memory globals the game reads.
    if (gMem1 == (u8*)MEM1_BASE) {
        *(u32*)(MEM1_BASE + 0x00F8) = 162000000u; // bus clock (GC: 162MHz)
        *(u32*)(MEM1_BASE + 0x00FC) =  40500000u; // core clock / CPU (162/4-ish base)
        *(u32*)(MEM1_BASE + 0x002C) = 0x10000006u; // console type: retail
    }
    // Arena starts above OS low memory, ends near the top of MEM1.
    gArenaLo = (gMem1 == (u8*)MEM1_BASE) ? (u8*)OS_LOWMEM_END
                                         : (u8*)(((u32)gMem1 + 0x1f) & ~0x1fu);
    gArenaHi = gMem1 + MEM1_SIZE - 0x20000; // leave headroom at the top
}

// --- init / system ---------------------------------------------------------
void OSInit(void) { osArenaInit(); }
u32  OSGetConsoleType(void) { return 0x10000006u; } // retail
void OSResetSystem(int reset, u32 resetCode, BOOL forceMenu) { (void)reset; (void)resetCode; (void)forceMenu; }
u32  OSGetResetCode(void) { return 0; }
void OSSetSoundMode(u32 mode) { (void)mode; }
u32  OSGetSoundMode(void) { return 1; } // stereo
void OSSetProgressiveMode(u32 on) { (void)on; }
u32  OSGetProgressiveMode(void) { return 0; }

// --- arena / heap (backed by the C heap) -----------------------------------
void* OSGetArenaHi(void) { osArenaInit(); return gArenaHi; }
void* OSGetArenaLo(void) { osArenaInit(); return gArenaLo; }
void  OSSetArenaHi(void* addr) { gArenaHi = (u8*)addr; }
void  OSSetArenaLo(void* addr) { gArenaLo = (u8*)addr; }
void* OSAllocFromArenaLo(u32 size, u32 align) { (void)align; return malloc(size); }
void* OSAllocFromArenaHi(u32 size, u32 align) { (void)align; return malloc(size); }

volatile OSHeapHandle __OSCurrHeap = -1;
void*        OSInitAlloc(void* lo, void* hi, int maxHeaps) { (void)hi; (void)maxHeaps; return lo; }
OSHeapHandle OSCreateHeap(void* start, void* end) { (void)start; (void)end; return 0; }
OSHeapHandle OSSetCurrentHeap(OSHeapHandle heap) { (void)heap; return 0; }
void*        OSAllocFromHeap(OSHeapHandle heap, u32 size) { (void)heap; return malloc(size); }
void*        OSAllocFixed(void** rstart, void** rend) { (void)rend; return rstart ? *rstart : NULL; }
void         OSFreeToHeap(OSHeapHandle heap, void* ptr) { (void)heap; free(ptr); }

// --- threads (stubbed; game boots effectively single-threaded) -------------
void OSInitThreadQueue(OSThreadQueue* q) { if (q) { q->head = NULL; q->tail = NULL; } }
BOOL OSCreateThread(OSThread* t, void* (*f)(void*), void* p, void* s, u32 sz, OSPriority pr, u16 a) {
    (void)t; (void)f; (void)p; (void)s; (void)sz; (void)pr; (void)a; return TRUE;
}
void OSExitThread(void* v) { (void)v; }
void OSCancelThread(OSThread* t) { (void)t; }
BOOL OSJoinThread(OSThread* t, void** v) { (void)t; if (v) *v = NULL; return TRUE; }
s32  OSResumeThread(OSThread* t) { (void)t; return 0; }
s32  OSSuspendThread(OSThread* t) { (void)t; return 0; }
BOOL OSIsThreadTerminated(OSThread* t) { (void)t; return TRUE; }
OSThread* OSGetCurrentThread(void) {
    static OSThread gMainThread; // one host "main" thread, always running
    gMainThread.state = OS_THREAD_STATE_RUNNING;
    return &gMainThread;
}
void OSSleepThread(OSThreadQueue* q) { (void)q; }
void OSWakeupThread(OSThreadQueue* q) { (void)q; }
void OSYieldThread(void) { }
OSPriority OSGetThreadPriority(OSThread* t) { (void)t; return 16; }
BOOL OSSetThreadPriority(OSThread* t, OSPriority p) { (void)t; (void)p; return TRUE; }

// --- message queue (stub) --------------------------------------------------
void OSInitMessageQueue(OSMessageQueue* mq, OSMessage* arr, s32 n) { (void)mq; (void)arr; (void)n; }
BOOL OSSendMessage(OSMessageQueue* mq, OSMessage m, s32 f) { (void)mq; (void)m; (void)f; return TRUE; }
BOOL OSJamMessage(OSMessageQueue* mq, OSMessage m, s32 f) { (void)mq; (void)m; (void)f; return TRUE; }
BOOL OSReceiveMessage(OSMessageQueue* mq, OSMessage* m, s32 f) { (void)mq; (void)f; if (m) *m = NULL; return FALSE; }

// --- mutex (single-thread: trivially satisfied) ----------------------------
void OSInitMutex(OSMutex* m) { (void)m; }
void OSLockMutex(OSMutex* m) { (void)m; }
void OSUnlockMutex(OSMutex* m) { (void)m; }
BOOL OSTryLockMutex(OSMutex* m) { (void)m; return TRUE; }

// --- stopwatch -------------------------------------------------------------
void   OSInitStopwatch(OSStopwatch* sw, char* name) { if (sw) { memset(sw, 0, sizeof(*sw)); sw->name = name; } }
void   OSStartStopwatch(OSStopwatch* sw) { if (sw) sw->last = OSGetTime(); }
void   OSStopStopwatch(OSStopwatch* sw) { if (sw) { sw->total += OSGetTime() - sw->last; sw->hits++; } }
OSTime OSCheckStopwatch(OSStopwatch* sw) { return sw ? sw->total : 0; }
void   OSResetStopwatch(OSStopwatch* sw) { if (sw) { sw->total = 0; sw->hits = 0; } }
void   OSDumpStopwatch(OSStopwatch* sw) { (void)sw; }

// --- alarm (stub) ----------------------------------------------------------
void OSCreateAlarm(OSAlarm* a) { (void)a; }
void OSSetAlarm(OSAlarm* a, OSTime t, OSAlarmHandler h) { (void)a; (void)t; (void)h; }
void OSSetPeriodicAlarm(OSAlarm* a, OSTime s, OSTime p, OSAlarmHandler h) { (void)a; (void)s; (void)p; (void)h; }
void OSCancelAlarm(OSAlarm* a) { (void)a; }

// --- font (IPL ROM system font) --------------------------------------------
// The GameCube supplies this font from IPL ROM; the port has no ROM, and SFA uses it
// only for the fallback GAMETEXT_SLOT_ERROR charset (real text = disc font). ANSI
// encoding, empty load. Give real impls if the error charset is ever needed on screen.
u16 OSGetFontEncode(void) { return OS_FONT_ENCODE_ANSI; }
u32 OSLoadFont(OSFontHeader* fontData, void* tmp) {
    (void)tmp;
    if (fontData) memset(fontData, 0, sizeof(OSFontHeader));
    return 0;
}
char* OSGetFontTexel(const char* string, void* image, s32 pos, s32 stride, s32* width) {
    (void)string; (void)image; (void)pos; (void)stride;
    if (width) *width = 0;
    return 0;
}
char* OSGetFontWidth(const char* string, s32* width) {
    (void)string;
    if (width) *width = 0;
    return 0;
}

// --- math helpers used by intersect_render.c (real impls; float returns must be exact
// or callers NaN). __frsqrte is the PPC reciprocal-sqrt estimate; atanf_fast/mathSinCosf
// are game math helpers whose real TUs aren't compiled yet. ---
double __frsqrte(double x) { return x > 0.0 ? 1.0 / sqrt(x) : 0.0; }
float atanf_fast(float x) { return atanf(x); }
void mathSinCosf(float angle, float* sinOut, float* cosOut) {
    if (sinOut) *sinOut = sinf(angle);
    if (cosOut) *cosOut = cosf(angle);
}
