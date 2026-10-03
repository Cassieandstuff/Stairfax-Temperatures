/* Shared by both builds: the synthetic GameCube map block blob. */
#ifndef MAPBLOCK_HOSTSTRUCT_SHIM_H
#define MAPBLOCK_HOSTSTRUCT_SHIM_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void be32(uint8_t* p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void be16(uint8_t* p, uint16_t v) { p[0] = v >> 8; p[1] = v; }
static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }

#define BLOB_SIZE 0x400
/* GameCube offsets, from map_block.h's STATIC_ASSERTs and offset comments */
enum {
    B_SIZE = 0x08, B_TRANSFORM = 0x0C, B_POLYS = 0x4C, B_GROUPS = 0x50, B_TEXTURES = 0x54, B_VERTICES = 0x58,
    B_COLORS = 0x5C, B_TEXCOORDS = 0x60, B_SHADERS = 0x64, B_DLISTS = 0x68, B_INSTR_MAIN = 0x78,
    B_INSTR_TRANSP = 0x7C, B_INSTR_WATER = 0x80, B_MINY = 0x8A, B_MAXY = 0x8C, B_VCOUNT = 0x90,
    B_NPOLYS = 0x98, B_TEXCOUNT = 0xA0, B_DLCOUNT = 0xA1, B_SHCOUNT = 0xA2,
    S_TEXTUREID = 0x18, S_LAYER0 = 0x24, S_AUX = 0x34, S_FLAGS = 0x3C, S_LAYERCOUNT = 0x41, S_SIZE = 0x44,
    R_DLIST = 0x00, R_DLSIZE = 0x04, R_MINX = 0x06, R_MAXZ = 0x10, R_SHADER = 0x13, R_SIZE = 0x1C,
};

static void put(uint8_t* b, int field, uint32_t off, const char* mark)
{
    be32(b + field, off);
    memcpy(b + off, mark, 4);
}

static void build_blob(uint8_t* b)
{
    memset(b, 0, BLOB_SIZE);
    be32(b + B_SIZE, 0x3C0);
    be32(b + B_TRANSFORM, 0x3F800000);                 /* 1.0f */
    be16(b + B_MINY, (uint16_t)-40); be16(b + B_MAXY, 900);
    be16(b + B_VCOUNT, 12); be16(b + B_NPOLYS, 3);
    put(b, B_POLYS, 0x100, "PLY!"); put(b, B_GROUPS, 0x110, "GRP!");
    put(b, B_VERTICES, 0x120, "VTX!"); put(b, B_COLORS, 0x130, "COL!"); put(b, B_TEXCOORDS, 0x140, "TEX!");
    put(b, B_INSTR_MAIN, 0x150, "IMN!"); put(b, B_INSTR_TRANSP, 0x160, "ITR!"); put(b, B_INSTR_WATER, 0x170, "IWT!");
    be32(b + B_TEXTURES, 0x180); b[B_TEXCOUNT] = 2; be32(b + 0x180, 0x1234); be32(b + 0x184, 0x0567);
    be32(b + B_SHADERS, 0x200); b[B_SHCOUNT] = 2;
    for (int i = 0; i < 2; i++) {
        uint8_t* s = b + 0x200 + i * S_SIZE;
        be32(s + S_TEXTUREID, 10 + i); be32(s + S_LAYER0, (uint32_t)i); be32(s + S_AUX, i ? 1 : 0xFFFFFFFF);
        be32(s + S_FLAGS, 0xA0000000u | (uint32_t)i); s[S_LAYERCOUNT] = 1;
    }
    be32(b + B_DLISTS, 0x2A0); b[B_DLCOUNT] = 2;
    for (int i = 0; i < 2; i++) {
        uint8_t* r = b + 0x2A0 + i * R_SIZE;
        be32(r + R_DLIST, 0x300 + i * 0x20); be16(r + R_DLSIZE, 0x40 + i);
        be16(r + R_MINX, (uint16_t)(-5 - i)); be16(r + R_MAXZ, 77 + i); r[R_SHADER] = (uint8_t)i;
        memcpy(b + 0x300 + i * 0x20, i ? "DL1!" : "DL0!", 4);
    }
}
#endif
