/* Shared by both builds: the synthetic GameCube model blob. */
#ifndef MODEL_HOSTSTRUCT_SHIM_H
#define MODEL_HOSTSTRUCT_SHIM_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void be32(uint8_t* p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void be16(uint8_t* p, uint16_t v) { p[0] = v >> 8; p[1] = v; }
static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }

#define BLOB_SIZE 0x400
/* GameCube offsets, from the decomp (STATIC_ASSERTs, comments, raw-offset code) */
enum {
    H_DATASIZE = 0x0C, H_TEXIDS = 0x20, H_VERTICES = 0x28, H_NORMALS = 0x2C, H_RENDEROPS = 0x38,
    H_VTXANIMCOUNT = 0x8A, H_VTXANIMENTRIES = 0xA4, H_VTXANIMBASE = 0xA8, H_DISPLAYLISTS = 0xD0,
    H_MORPHPTRS = 0xDC, H_TEXCOUNT = 0xF2, H_JOINTCOUNT = 0xF3, H_DLCOUNT = 0xF5, H_SHADOWDLCOUNT = 0xF6,
    H_RENDEROPCOUNT = 0xF8, H_MORPHCOUNT = 0xF9,
    S_TEXTUREID = 0x18, S_LAYER0 = 0x24, S_AUX = 0x34, S_IND = 0x38, S_FLAGS = 0x3C, S_LAYERCOUNT = 0x41,
    S_SIZE = 0x44, DL_SIZE = 0x1C,
    C_SRC = 0x60, C_WEIGHT = 0x64, C_MTXA = 0x6C, C_MTXB = 0x6D, C_WW = 0x6F, C_VCOUNT = 0x70,
    C_DST = 0x72, C_VW = 0x73,
};

static void build_blob(uint8_t* b)
{
    memset(b, 0, BLOB_SIZE);
    b[0] = 1;
    be32(b + H_DATASIZE, 0x360);
    be32(b + H_VERTICES, 0x100);   memcpy(b + 0x100, "VTX!", 4);
    be32(b + H_NORMALS, 0x110);    memcpy(b + 0x110, "NRM!", 4);
    be32(b + H_DISPLAYLISTS, 0x120);
    be32(b + 0x120, 0x200); be16(b + 0x124, 0x40);           memcpy(b + 0x200, "DL0!", 4);
    be32(b + 0x120 + DL_SIZE, 0x240); be16(b + 0x124 + DL_SIZE, 0x20); memcpy(b + 0x240, "DL1!", 4);
    b[H_DLCOUNT] = 1; b[H_SHADOWDLCOUNT] = 1;
    be32(b + H_RENDEROPS, 0x180); b[H_RENDEROPCOUNT] = 1;
    be32(b + 0x180 + S_TEXTUREID, 1); be32(b + 0x180 + S_LAYER0, 0);
    be32(b + 0x180 + S_AUX, 0xFFFFFFFF); be32(b + 0x180 + S_IND, 1);
    be32(b + 0x180 + S_FLAGS, 0x12345678); b[0x180 + S_LAYERCOUNT] = 1;
    be32(b + H_MORPHPTRS, 0x1D0); b[H_MORPHCOUNT] = 2;
    be32(b + 0x1D0, 0x280); be32(b + 0x1D4, 0x290);
    memcpy(b + 0x280, "MT0!", 4); memcpy(b + 0x290, "MT1!", 4);
    be32(b + H_VTXANIMENTRIES, 0x2A0); be16(b + H_VTXANIMCOUNT, 1);
    be32(b + 0x2A0 + C_SRC, 0x1234); be32(b + 0x2A0 + C_WEIGHT, 0x10);
    b[0x2A0 + C_MTXA] = 3; b[0x2A0 + C_MTXB] = 5; b[0x2A0 + C_WW] = 7;
    be16(b + 0x2A0 + C_VCOUNT, 0x0102); b[0x2A0 + C_DST] = 9; b[0x2A0 + C_VW] = 11;
    be32(b + H_VTXANIMBASE, 0x320); memcpy(b + 0x330, "WST!", 4);
    be32(b + H_TEXIDS, 0x340); b[H_TEXCOUNT] = 2; be32(b + 0x340, 0x55); be32(b + 0x344, 0x66);
    b[H_JOINTCOUNT] = 4;
    memcpy(b + 0x360, "ANM!", 4);                     /* at header + dataSize */
}
#endif
