/* Minimal compile surface for the extracted gameTextDrawBox. The callees are
 * recording stubs: the measurer captures the string it was handed (proving the
 * pointer survived the trip through boxId), the draw calls capture the subtitle
 * coordinates derived from the measured bounds. */
#ifndef DRAWBOX_STR_SHIM_H
#define DRAWBOX_STR_SHIM_H

#include "difftest.h"
#include <string.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int16_t  s16;
typedef float    f32;

typedef struct { u8 r, g, b, a; } GXColor;
typedef struct Texture Texture;
struct GameTextDef;                       /* opaque: only *(u16*)def is read */

typedef struct GameTextBox {             /* decomp/include/main/gametext_box_api.h */
    u16 unk00; u16 maxWidth; u16 unk04; u16 maxHeight;
    u16 width; u16 height; f32 scale;
    u8 alignH; u8 alignV; u8 alignment; u8 style;
    s16 x; s16 y; s16 cursorX; s16 cursorY;
    u16 flags; u8 alpha; u8 unk1F;
} GameTextBox;                            /* 0x20 bytes */

GameTextBox gTextBoxes[8];
GXColor gGameTextBoxFillColor;
Texture *gGameTextBoxCornerTexture, *gGameTextBoxBgTexture;
Texture *gSubtitleBoxTextures[3], *gGameTextBoxFrameTextures[5];
int gGameTextBoxCornerInset = 4;

/* --- recorders --- */
static char gMeasuredStr[32];
static int gMeasuredIdx = -1, gMeasuredId = -1, gDraws, gDrawSum;
static u8 gWindow[4];

void gameTextMeasureStringBounds(char* str, int boxIdx, int* minX, int* maxX, int* minY, int* maxY) {
    strncpy(gMeasuredStr, str, sizeof gMeasuredStr - 1);   /* faults if str was truncated */
    gMeasuredIdx = boxIdx;
    *minX = 100; *maxX = 100 + 8 * (int)strlen(str); *minY = 200; *maxY = 216;
}
void gameTextMeasureById(int id, int x, int y, int* minX, int* maxX, int* minY, int* maxY) {
    (void)x; (void)y;
    gMeasuredId = id;
    *minX = 50; *maxX = 50 + id; *minY = 60; *maxY = 76;
}
void* gameTextGetCurBox(void) { return gWindow; }
void gameTextSetWindow(u8* textBox) { (void)textBox; }
int getCurGameText(void) { return 0; }
void hudDrawRect(int x1, int y1, int x2, int y2, GXColor* c) { (void)c; gDraws++; gDrawSum += x1 + y1 + x2 + y2; }
void GXSetScissor(u32 l, u32 t, u32 w, u32 h) { (void)l; (void)t; (void)w; (void)h; }
void drawHudBox(s16 x, s16 y, s16 w, s16 h, u8 a, u8 f) { (void)a; (void)f; gDraws++; gDrawSum += x + y + w + h; }
void drawTexture(void* tex, f32 x, f32 y, int alpha, int scale) {
    (void)tex; (void)alpha; (void)scale; gDraws++; gDrawSum += (int)x + (int)y;
}
void drawScaledTexture(void* tex, f32 x, f32 y, int alpha, int scale, int w, int h, int flags) {
    (void)tex; (void)alpha; (void)scale; (void)flags; gDraws++; gDrawSum += (int)x + (int)y + w + h;
}
void drawPartialTexture(void* tex, f32 x, f32 y, int alpha, int scale, int w, int h, int sx, int flags) {
    (void)tex; (void)alpha; (void)scale; (void)sx; (void)flags; gDraws++; gDrawSum += (int)x + (int)y + w + h;
}
/* forwarded only by style 4 (not exercised); unused body, as in the real TU */
static void gameTextDrawBoxEdges(u16* strPtr, uintptr_t boxId, u8* box) { (void)strPtr; (void)boxId; (void)box; }

#endif /* DRAWBOX_STR_SHIM_H */
