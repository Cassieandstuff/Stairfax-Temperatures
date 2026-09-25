// gx_draw.c - GX write-gather-pipe interpreter -> RHI.
//
// The game submits geometry the console way: GXBegin(prim, vtxfmt, n) states the
// primitive, then n vertices are written straight to the GXWGFifo write-gather pipe
// in the layout described by the current vertex descriptor (GXSetVtxDesc) and the
// vertex-attribute table for that format (GXSetVtxAttrFmt). We buffer the pipe
// bytes, and once GXBegin's n vertices have arrived we decode them per the VAT,
// transform by MVP = proj * posmtx, and draw through the RHI. This is the real draw
// path - the same entry points compiled game drawing TUs call.

#include "port/gx_draw.h"

#include "dolphin/gx/GXGeometry.h"
#include "dolphin/gx/GXTransform.h"

// gxTexDecode (port/tex_decode.h) - forward-declared rather than #included because
// that header re-defines the GX_TF_* enum, which collides with dolphin/gx/GXEnum.h
// (already pulled in above). uint8_t == unsigned char.
extern unsigned char* gxTexDecode(int fmt, int w, int h, const unsigned char* src);

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
static int gDbg = -1;
static int dbg(void){ if(gDbg<0) gDbg = getenv("GXDRAW_DEBUG")?1:0; return gDbg; }

// --- RHI wiring ------------------------------------------------------------
static RhiInstance*  gRhi;
static RhiSwapchain* gSc;
static RhiTexture*   gTex;

void gx_draw_setRhi(RhiInstance* rhi, RhiSwapchain* sc) { gRhi = rhi; gSc = sc; }
void gx_draw_setTexture(RhiTexture* tex) { gTex = tex; }
void gx_draw_setAlphaMode(int mode) { if (gRhi) rhi_setAlphaMode(gRhi, mode); }

// --- vertex format state ---------------------------------------------------
typedef struct { u8 cnt; u8 type; u8 frac; } VatAttr;
static GXAttrType gDesc[GX_VA_MAX_ATTR];
static VatAttr    gVat[GX_MAX_VTXFMT][GX_VA_MAX_ATTR];

// --- transform state -------------------------------------------------------
#define GX_MTX_SLOTS 64
static float gProj[4][4];
static float gPosMtx[GX_MTX_SLOTS][3][4];
static int   gCurMtx;

// Indexed vertex arrays: INDEX8/INDEX16 attributes carry an index into these bases
// (big-endian element data, `stride` bytes apart), set by the game before a draw.
typedef struct { const unsigned char* base; int stride; } ArrayDef;
static ArrayDef gArray[GX_VA_MAX_ATTR];

static void mtxIdentity3x4(float m[3][4]) { memset(m, 0, sizeof(float)*12); m[0][0]=m[1][1]=m[2][2]=1.0f; }
static void mtxIdentity4x4(float m[4][4]) { memset(m, 0, sizeof(float)*16); m[0][0]=m[1][1]=m[2][2]=m[3][3]=1.0f; }

// MVP = proj(4x4) * posMtx(3x4 extended with [0,0,0,1]); row-major output.
static void computeMVP(float out[16]) {
    float P[4][4];
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 4; ++j) P[i][j] = gPosMtx[gCurMtx][i][j];
    P[3][0]=P[3][1]=P[3][2]=0.0f; P[3][3]=1.0f;
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) {
        float s = 0.0f; for (int k = 0; k < 4; ++k) s += gProj[i][k]*P[k][j];
        out[i*4+j] = s;
    }
}

// Reset only the transform state to identity, leaving the vertex-attribute table
// (gVat/gDesc/gArray) intact. Used to attach the renderer mid-boot (loading screen)
// without clobbering the VAT videoInit already programmed for the hud draws.
void gx_draw_reset_matrices(void) {
    gCurMtx = 0; gTex = NULL;
    mtxIdentity4x4(gProj);
    for (int i = 0; i < GX_MTX_SLOTS; ++i) mtxIdentity3x4(gPosMtx[i]);
}

void gx_draw_init(void) {
    memset(gDesc, 0, sizeof(gDesc));
    memset(gVat, 0, sizeof(gVat));
    memset(gArray, 0, sizeof(gArray));
    gx_draw_reset_matrices();
}

// --- GX vertex format API --------------------------------------------------
void GXClearVtxDesc(void) { memset(gDesc, 0, sizeof(gDesc)); }
void GXSetVtxDesc(GXAttr attr, GXAttrType type) { if ((unsigned)attr < GX_VA_MAX_ATTR) gDesc[attr] = type; }
void GXSetVtxAttrFmt(GXVtxFmt vtxfmt, GXAttr attr, GXCompCnt cnt, GXCompType type, u8 frac) {
    if ((unsigned)vtxfmt < GX_MAX_VTXFMT && (unsigned)attr < GX_VA_MAX_ATTR) {
        gVat[vtxfmt][attr].cnt  = (u8)cnt;
        gVat[vtxfmt][attr].type = (u8)type;
        gVat[vtxfmt][attr].frac = frac;
    }
}

void GXSetArray(GXAttr attr, void* base, u8 stride) {
    if ((unsigned)attr < GX_VA_MAX_ATTR) { gArray[attr].base=(const unsigned char*)base; gArray[attr].stride=stride; }
}

// Optional source-buffer bounds for indexed fetches. The GX API carries no array
// length, so a display list with an out-of-range index would read past the source
// buffer (usually the map block). When a caller sets bounds, the decoder skips any
// indexed element that falls outside [lo,hi) instead of faulting on unmapped memory.
static const unsigned char* gSrcLo;
static const unsigned char* gSrcHi;
void gx_draw_setSourceBounds(const void* lo, const void* hi) {
    gSrcLo = (const unsigned char*)lo; gSrcHi = (const unsigned char*)hi;
}

// Bind-pose skinning: a per-matrix-slot POS translation added to each vertex by its
// PNMTXIDX (byte/3 -> slot). Set by the model renderer; cleared (n=0) for normal draws.
#define GX_MTX_IDX_SLOTS 16
static float gJointOff[GX_MTX_IDX_SLOTS][3];
static int   gHaveJointOff;
void gx_draw_setJointOffsets(const float off[][3], int n) {
    gHaveJointOff = (n > 0);
    for (int i = 0; i < GX_MTX_IDX_SLOTS; ++i)
        for (int c = 0; c < 3; ++c) gJointOff[i][c] = (i < n && off) ? off[i][c] : 0.0f;
}

// Full per-matrix-slot skinning: a 3x4 model-space transform applied to POS before the
// model->world->view matrix, selected by PNMTXIDX/3. Takes precedence over the offset path.
// Set by the model renderer (identity slots = bind pose; posed slots = animation).
static float gJointMtx[GX_MTX_IDX_SLOTS][3][4];
static int   gHaveJointMtx;
void gx_draw_setJointMatrices(const float m[][3][4], int n) {
    gHaveJointMtx = (n > 0);
    for (int i = 0; i < GX_MTX_IDX_SLOTS; ++i)
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 4; ++c)
                gJointMtx[i][r][c] = (i < n && m) ? m[i][r][c]
                                   : (r == c ? 1.0f : 0.0f);   // identity fallback
}

// Apply the joint transform for this vertex's matrix slot (PNMTXIDX/3): full 3x4 matrix if
// set, else the bind-pose translation offset, else nothing.
static void applyJointSkin(RhiTexVertex* v, int pnmtx) {
    int s = (pnmtx / 3) & (GX_MTX_IDX_SLOTS - 1);
    if (gHaveJointMtx) {
        const float (*M)[4] = gJointMtx[s];
        float x = v->x, y = v->y, z = v->z;
        v->x = M[0][0]*x + M[0][1]*y + M[0][2]*z + M[0][3];
        v->y = M[1][0]*x + M[1][1]*y + M[1][2]*z + M[1][3];
        v->z = M[2][0]*x + M[2][1]*y + M[2][2]*z + M[2][3];
    } else if (gHaveJointOff) {
        v->x += gJointOff[s][0]; v->y += gJointOff[s][1]; v->z += gJointOff[s][2];
    }
}

// Explicit POS-only layout override: when a skinned model's descriptor-derived layout
// fails to validate against its DL, the model renderer autodetects the true per-vertex
// stride and POS offset (as model_view does) and forces it here. In force mode decodePrim
// reads only the POS index (at posOff, posSz bytes) plus the leading PNMTXIDX for skinning,
// ignoring the rest of the vertex, and advances by the fixed stride. Cleared with stride<=0.
static int gForceStride, gForcePosOff, gForcePosSz;
void gx_draw_setForceLayout(int stride, int posOff, int posSz) {
    gForceStride = stride; gForcePosOff = posOff; gForcePosSz = posSz;
}

// Real GX skinning mode: each vertex is transformed by the GX position matrix its PNMTXIDX
// selects (gPosMtx[PNMTXIDX/3], loaded via GXLoadPosMtxImm), then projected by gProj alone -
// i.e. the retail model render path. Off by default (the interim renderModel keeps using the
// gJointOff/gJointMtx offset path + full MVP). The real model renderer sets this around a draw.
static int gRealSkin;
void gx_draw_setRealSkin(int on) { gRealSkin = on; }
// Transform one vertex to view space by its per-vertex matrix (real mode) or the interim skin.
static void skinVertex(RhiTexVertex* v, int pnmtx, int havePnmtx) {
    if (gRealSkin) {
        int slot = havePnmtx ? (pnmtx/3) : gCurMtx;
        if ((unsigned)slot >= GX_MTX_SLOTS) slot = 0;
        const float (*M)[4] = gPosMtx[slot];
        float x=v->x, y=v->y, z=v->z;
        v->x = M[0][0]*x + M[0][1]*y + M[0][2]*z + M[0][3];
        v->y = M[1][0]*x + M[1][1]*y + M[1][2]*z + M[1][3];
        v->z = M[2][0]*x + M[2][1]*y + M[2][2]*z + M[2][3];
    } else {
        applyJointSkin(v, pnmtx);
    }
}

// --- GX transform API ------------------------------------------------------
// The RHI (Vulkan) clips clip-space z to [0,w]. The game's GC-convention ortho
// (C_MTXOrtho) maps near->-1, far->0, so every hud/2D quad lands at z_ndc<0 and is
// clipped away (black). Correct GC ortho to Vulkan range by adding the w row to the
// z row (z' = z + w): near->0, far->1. Perspective projections used here are already
// built in the RHI convention (game_scene), so only orthographic ones are remapped.
void GXSetProjection(const f32 mtx[4][4], GXProjectionType type) {
    memcpy(gProj, mtx, sizeof(float)*16);
    if (type == GX_ORTHOGRAPHIC)
        for (int j = 0; j < 4; ++j) gProj[2][j] += gProj[3][j];
}
void GXLoadPosMtxImm(const f32 mtx[3][4], u32 id) {
    u32 s = id/3;
    if (s >= GX_MTX_SLOTS) return;
    memcpy(gPosMtx[s], mtx, sizeof(float)*12);
    // Safety on the skinned path: a NaN/inf or wildly out-of-range pos matrix (the not-yet-correct
    // vertex-group / extra-joint blend can still produce one) would fling its verts across the
    // screen. Push such a matrix's verts behind the far plane so they clip instead of spiking.
    if (gRealSkin) {
        float* M = &gPosMtx[s][0][0]; int bad = 0;
        for (int k=0;k<12;++k){ float a = M[k]<0?-M[k]:M[k]; if (a>1e6f || a!=a){ bad=1; break; } }
        // The skinned character is drawn under the follow-camera, which holds it ~120u away, so every
        // legit player pos matrix has a small view-space translation (well under a few hundred u). A
        // pos matrix whose bone sits ~2000u out is an extra vertex-group joint the not-yet-ported
        // blend path left at the WORLD ORIGIN (view-space ~(-1600,1900,-1760)); its verts draw a
        // degenerate triangle fan to that point - the diagonal spike. Clip those to the far plane.
        if (!bad) { float tx=gPosMtx[s][0][3], ty=gPosMtx[s][1][3], tz=gPosMtx[s][2][3];
            if (tx*tx + ty*ty + tz*tz > 900.0f*900.0f) bad = 1; }
        if (bad) { for (int k=0;k<12;++k) M[k]=0; gPosMtx[s][2][3]=1e9f; }
    }
}
void GXLoadNrmMtxImm(const f32 mtx[3][4], u32 id) { (void)mtx; (void)id; }  // normals unused in the untextured path
void GXLoadTexMtxImm(const f32 mtx[][4], u32 id, GXTexMtxType type) { (void)mtx; (void)id; (void)type; }
void GXSetCurrentMtx(u32 id) { u32 s = id/3; if (s < GX_MTX_SLOTS) gCurMtx = (int)s; }

// --- component sizing ------------------------------------------------------
static int compTypeSize(u8 t) { // GX_U8/S8=1, U16/S16=2, F32=4
    switch (t) { case GX_U8: case GX_S8: return 1; case GX_U16: case GX_S16: return 2; case GX_F32: return 4; }
    return 0;
}
static int colorSize(u8 t) { // GXCompType color enums
    switch (t) { case GX_RGB565: case GX_RGBA4: return 2; case GX_RGB8: case GX_RGBA6: return 3;
                 case GX_RGBX8: case GX_RGBA8: return 4; }
    return 0;
}
// Bytes one attribute occupies in the FIFO for the current descriptor/VAT.
static int attrSize(GXVtxFmt fmt, GXAttr a) {
    GXAttrType d = gDesc[a];
    if (d == GX_NONE) return 0;
    if (d == GX_INDEX8)  return 1;
    if (d == GX_INDEX16) return 2;
    // GX_DIRECT:
    VatAttr* v = &gVat[fmt][a];
    if (a <= GX_VA_TEX7MTXIDX) return 1;                 // matrix index attrs: 1 byte
    if (a == GX_VA_POS) return compTypeSize(v->type) * (v->cnt == GX_POS_XYZ ? 3 : 2);
    if (a == GX_VA_NRM) return compTypeSize(v->type) * (v->cnt == GX_NRM_XYZ ? 3 : 9);
    if (a == GX_VA_CLR0 || a == GX_VA_CLR1) return colorSize(v->type);
    if (a >= GX_VA_TEX0 && a <= GX_VA_TEX7) return compTypeSize(v->type) * (v->cnt == GX_TEX_ST ? 2 : 1);
    return 0;
}

// --- FIFO buffer -----------------------------------------------------------
#define GX_FIFO_CAP (1 << 20)
static unsigned char gFifo[GX_FIFO_CAP];
static int  gFifoLen;
static int  gCollecting;
static int  gExpectBytes;
static int  gPrimN;
static GXVtxFmt gPrimFmt;
static u8   gPrim;

static void fifoMaybeFlush(void); // draws once GXBegin's vertex count has arrived
static void fifoReset(void) { gFifoLen = 0; }
static void fifoByte(unsigned char b) { if (gFifoLen < GX_FIFO_CAP) gFifo[gFifoLen++] = b; }

void gxfifo_push_u8 (unsigned char v)  { fifoByte(v); fifoMaybeFlush(); }
void gxfifo_push_u16(unsigned short v) { fifoByte((unsigned char)(v>>8)); fifoByte((unsigned char)v); fifoMaybeFlush(); }
void gxfifo_push_u32(unsigned int v)   { fifoByte((unsigned char)(v>>24)); fifoByte((unsigned char)(v>>16));
                                         fifoByte((unsigned char)(v>>8));  fifoByte((unsigned char)v); fifoMaybeFlush(); }
void gxfifo_push_f32(float f)          { unsigned int u; memcpy(&u, &f, 4); gxfifo_push_u32(u); }

// --- big-endian readers over an arbitrary byte source ----------------------
static unsigned rdU(const unsigned char* d, int* c, int n) { unsigned x=0; for(int i=0;i<n;++i) x=(x<<8)|d[(*c)++]; return x; }
static int      rdS(const unsigned char* d, int* c, int n) { unsigned x=rdU(d,c,n); unsigned s=1u<<(n*8-1); return (int)((x^s)-s); }
static float    rdF(const unsigned char* d, int* c) { unsigned u=rdU(d,c,4); float f; memcpy(&f,&u,4); return f; }

static float compVal(const unsigned char* d, int* c, u8 type, u8 frac) { // one POS/TEX scalar
    switch (type) {
        case GX_U8:  return (float)rdU(d,c,1) / (float)(1<<frac);
        case GX_S8:  return (float)rdS(d,c,1) / (float)(1<<frac);
        case GX_U16: return (float)rdU(d,c,2) / (float)(1<<frac);
        case GX_S16: return (float)rdS(d,c,2) / (float)(1<<frac);
        case GX_F32: return rdF(d,c);
    }
    return 0.0f;
}
static u32 readColorAt(const unsigned char* d, int* c, u8 type) { // -> r|g<<8|b<<16|a<<24
    u8 r=255,g=255,b=255,a=255;
    switch (type) {
        case GX_RGB565: { unsigned v=rdU(d,c,2); r=(u8)((v>>11)<<3); g=(u8)(((v>>5)&0x3f)<<2); b=(u8)((v&0x1f)<<3); } break;
        case GX_RGB8:   { r=(u8)rdU(d,c,1); g=(u8)rdU(d,c,1); b=(u8)rdU(d,c,1); } break;
        case GX_RGBX8:  { r=(u8)rdU(d,c,1); g=(u8)rdU(d,c,1); b=(u8)rdU(d,c,1); (void)rdU(d,c,1); } break;
        case GX_RGBA4:  { unsigned v=rdU(d,c,2); r=(u8)(((v>>12)&0xf)*17); g=(u8)(((v>>8)&0xf)*17); b=(u8)(((v>>4)&0xf)*17); a=(u8)((v&0xf)*17); } break;
        case GX_RGBA6:  { unsigned v=rdU(d,c,3); r=(u8)(((v>>18)&0x3f)<<2); g=(u8)(((v>>12)&0x3f)<<2); b=(u8)(((v>>6)&0x3f)<<2); a=(u8)((v&0x3f)<<2); } break;
        case GX_RGBA8:  { r=(u8)rdU(d,c,1); g=(u8)rdU(d,c,1); b=(u8)rdU(d,c,1); a=(u8)rdU(d,c,1); } break;
    }
    return (u32)r | ((u32)g<<8) | ((u32)b<<16) | ((u32)a<<24);
}

// Read attribute `a` from `src` (cursor `c`) into `out`; used for both DIRECT
// (src = the vertex stream) and INDEX (src = the array element).
static void readAttrValue(GXAttr a, VatAttr* v, const unsigned char* src, int* c,
                          RhiTexVertex* out, int* haveTex) {
    if (a == GX_VA_POS) {
        out->x = compVal(src,c,v->type,v->frac);
        out->y = compVal(src,c,v->type,v->frac);
        if (v->cnt == GX_POS_XYZ) out->z = compVal(src,c,v->type,v->frac);
    } else if (a == GX_VA_NRM) {
        int cc = (v->cnt == GX_NRM_XYZ) ? 3 : 9;
        for (int i=0;i<cc;++i) compVal(src,c,v->type,0);
    } else if (a == GX_VA_CLR0) {
        out->rgba = readColorAt(src,c,v->type);
    } else if (a == GX_VA_CLR1) {
        readColorAt(src,c,v->type);
    } else if (a >= GX_VA_TEX0 && a <= GX_VA_TEX7) {
        float s = compVal(src,c,v->type,v->frac);
        float t = (v->cnt == GX_TEX_ST) ? compVal(src,c,v->type,v->frac) : 0.0f;
        if (a == GX_VA_TEX0) { out->u=s; out->v=t; *haveTex=1; }
    }
}

// --- decode + draw ---------------------------------------------------------
#define GX_MAX_BATCH 16384
static RhiTexVertex gVerts[GX_MAX_BATCH];
// Per-vertex validity: a vertex whose POS came from an out-of-bounds indexed fetch is
// left at the origin (garbage), which would stretch any triangle using it into a "spike".
// We mark such vertices invalid and drop every triangle that references one.
static unsigned char gVertOk[GX_MAX_BATCH];

// Emit a triangle only if all three source vertices decoded cleanly.
static inline int emitTri(RhiTexVertex* out, int n, int cap, int a, int b, int c) {
    if (n+3 > cap) return n;
    if (!gVertOk[a] || !gVertOk[b] || !gVertOk[c]) return n;   // skip spike triangles
    out[n++]=gVerts[a]; out[n++]=gVerts[b]; out[n++]=gVerts[c];
    return n;
}

static int expandTriangles(RhiTexVertex* out, int cap, int nverts, u8 prim) {
    int n = 0;
    if (prim == GX_TRIANGLES) {
        for (int i = 0; i+3 <= nverts; i += 3) n = emitTri(out,n,cap,i,i+1,i+2);
    } else if (prim == GX_TRIANGLESTRIP) {
        for (int i = 2; i < nverts; ++i)
            n = (i&1) ? emitTri(out,n,cap,i-1,i-2,i) : emitTri(out,n,cap,i-2,i-1,i);
    } else if (prim == GX_TRIANGLEFAN) {
        for (int i = 2; i < nverts; ++i) n = emitTri(out,n,cap,0,i-1,i);
    } else if (prim == GX_QUADS) {
        for (int i = 0; i+4 <= nverts; i += 4) {
            n = emitTri(out,n,cap,i,i+1,i+2);
            n = emitTri(out,n,cap,i,i+2,i+3);
        }
    }
    return n;
}

// Decode nverts (per gDesc + VAT[fmt]) from `src`, expand `prim` to triangles, draw.
// Returns bytes consumed from `src`.
static int decodePrim(const unsigned char* src, GXVtxFmt fmt, u8 prim, int nverts, int maxBytes) {
    if (nverts > GX_MAX_BATCH) nverts = GX_MAX_BATCH;
    int c = 0, haveTex = 0;
    int havePnmtx = (gDesc[GX_VA_PNMTXIDX] != GX_NONE) || gHaveJointOff;
    for (int vi = 0; vi < nverts; ++vi) {
        if (maxBytes > 0 && c >= maxBytes) { nverts = vi; break; }  // don't read past the DL
        RhiTexVertex out; out.x=out.y=out.z=0; out.rgba=0xFFFFFFFFu; out.u=out.v=0;
        int pnmtx = 0, posOk = 1;   // posOk=0 -> POS index was out of bounds, drop this vertex
        if (gForceStride > 0) {  // explicit POS-only layout (autodetected skinned fallback)
            const unsigned char* vp = src + c;
            if (gHaveJointOff) pnmtx = vp[0];
            unsigned idx = gForcePosSz==2 ? ((vp[gForcePosOff]<<8)|vp[gForcePosOff+1]) : vp[gForcePosOff];
            const ArrayDef* ar = &gArray[GX_VA_POS];
            int inBounds = 0;
            if (ar->base && ar->stride) {
                if (!gSrcLo) inBounds = 1;
                else if (ar->base >= gSrcLo && ar->base < gSrcHi) {
                    unsigned maxIdx = (unsigned)(gSrcHi - ar->base) / (unsigned)ar->stride;
                    inBounds = (idx < maxIdx);
                }
            }
            if (inBounds) { int ac = 0; readAttrValue(GX_VA_POS, &gVat[fmt][GX_VA_POS], ar->base + idx*ar->stride, &ac, &out, &haveTex); }
            else posOk = 0;
            c += gForceStride;
            skinVertex(&out, pnmtx, havePnmtx);
            gVerts[vi] = out; gVertOk[vi] = (unsigned char)posOk;
            continue;
        }
        for (GXAttr a = 0; a < GX_VA_MAX_ATTR; ++a) {
            GXAttrType d = gDesc[a];
            if (d == GX_NONE) continue;
            if (a <= GX_VA_TEX7MTXIDX) { unsigned mb = rdU(src,&c,1); if (a == GX_VA_PNMTXIDX) pnmtx = (int)mb; continue; }
            VatAttr* v = &gVat[fmt][a];
            if (d == GX_INDEX8 || d == GX_INDEX16) {
                unsigned idx = rdU(src, &c, d==GX_INDEX8?1:2);
                const ArrayDef* ar = &gArray[a];
                // Bound the index against the source buffer BEFORE computing the element
                // pointer, so a bad index can't overflow past the guard into wild memory.
                int inBounds = 0;
                if (ar->base && ar->stride) {
                    if (!gSrcLo) inBounds = 1;
                    else if (ar->base >= gSrcLo && ar->base < gSrcHi) {
                        unsigned maxIdx = (unsigned)(gSrcHi - ar->base) / (unsigned)ar->stride;
                        inBounds = (idx < maxIdx);
                    }
                    // gSrcLo/gSrcHi bounds the POS vertex buffer (the spike guard). NRM/CLR/TEX
                    // arrays live in separate buffers outside that window, so the window check
                    // doesn't apply to them - allow the read rather than dropping their attribute
                    // (which left every skinned vertex with no UV -> haveTex=0 -> untextured).
                    else inBounds = 1;
                }
                if (inBounds) { int ac = 0; readAttrValue(a, v, ar->base + idx*ar->stride, &ac, &out, &haveTex); }
                else if (a == GX_VA_POS) posOk = 0;   // bad position -> drop vertex, not a spike
            } else { // GX_DIRECT: read inline from the stream
                readAttrValue(a, v, src, &c, &out, &haveTex);
            }
        }
        skinVertex(&out, pnmtx, havePnmtx);
        gVerts[vi] = out; gVertOk[vi] = (unsigned char)posOk;
    }
    if (gRhi) {
        float mvp[16];
        if (gRealSkin) { for (int i=0;i<4;++i) for (int j=0;j<4;++j) mvp[i*4+j]=gProj[i][j]; }
        else computeMVP(mvp);
        rhi_setColorTransform(gRhi, mvp);
        static RhiTexVertex tri[GX_MAX_BATCH*3];
        int n = expandTriangles(tri, (int)(sizeof(tri)/sizeof(tri[0])), nverts, prim);
        if (dbg()) fprintf(stderr, "[gxdraw] prim=%02x fmt=%d nv=%d tris=%d v0=(%.1f,%.1f,%.1f) rgba=%08x tex=%d\n",
                           prim, fmt, nverts, n/3, gVerts[0].x, gVerts[0].y, gVerts[0].z, gVerts[0].rgba, haveTex);
        if (n > 0) {
            if (gTex && haveTex) { rhi_setTexture(gRhi,0,gTex); rhi_drawTextured(gRhi,tri,(u32)n); }
            else { static RhiColorVertex col[GX_MAX_BATCH*3];
                   for (int i=0;i<n;++i){ col[i].x=tri[i].x; col[i].y=tri[i].y; col[i].z=tri[i].z; col[i].rgba=tri[i].rgba; }
                   rhi_drawColored(gRhi, col, (u32)n); }
        }
    }
    return c;
}

static void decodeAndDraw(void) { decodePrim(gFifo, gPrimFmt, gPrim, gPrimN, gExpectBytes); }

// --- GXCallDisplayList: replay a recorded GX command buffer -----------------
// A display list is a stream of primitive commands: an opcode byte (prim|fmt),
// a u16 vertex count, then that many vertices in the current descriptor layout.
// Non-draw opcodes (state loads) don't appear in SFA map block lists.
void GXCallDisplayList(void* listv, u32 nbytes) {
    const unsigned char* dl = (const unsigned char*)listv;
    unsigned i = 0;
    while (i < nbytes) {
        u8 op = dl[i++];
        if (op == 0x00) continue;              // NOP / padding
        u8 prim = op & 0xF8, fmt = op & 0x07;
        if (prim==GX_QUADS||prim==GX_TRIANGLES||prim==GX_TRIANGLESTRIP||prim==GX_TRIANGLEFAN||
            prim==GX_LINES||prim==GX_LINESTRIP||prim==GX_POINTS) {
            if (i+2 > nbytes) break;
            int nverts = (int)((dl[i]<<8)|dl[i+1]); i += 2;
            i += (unsigned)decodePrim(dl + i, (GXVtxFmt)fmt, prim, nverts, (int)(nbytes - i));
        } else {
            if (dbg()) fprintf(stderr, "[gxdraw] DL unknown op %02x at %u\n", op, i-1);
            break; // unknown opcode: stop rather than misparse
        }
    }
}

// --- GXBegin ---------------------------------------------------------------
static int vertexStride(GXVtxFmt fmt) {
    int s = 0;
    for (GXAttr a = 0; a < GX_VA_MAX_ATTR; ++a) s += attrSize(fmt, a);
    return s;
}

void GXBegin(GXPrimitive type, GXVtxFmt vtxfmt, u16 nverts) {
    gPrim = (u8)type; gPrimFmt = vtxfmt; gPrimN = nverts;
    int stride = vertexStride(vtxfmt);
    gExpectBytes = stride * nverts;
    fifoReset();
    gCollecting = (gExpectBytes > 0 && nverts > 0);
    if (dbg()) fprintf(stderr, "[gxdraw] GXBegin prim=%02x fmt=%d n=%d stride=%d expect=%d rhi=%p\n",
                       gPrim, vtxfmt, nverts, stride, gExpectBytes, (void*)gRhi);
    if (!gCollecting) decodeAndDraw(); // degenerate: nothing to read
}

// Called from each FIFO push: once a full primitive's bytes have landed, draw it.
static void fifoMaybeFlush(void) {
    if (gCollecting && gFifoLen >= gExpectBytes) { gCollecting = 0; decodeAndDraw(); }
}

// --- GX render-state + texture-object API used by intersect_render.c's 2D/text draw.
// Loose enum types (int/u8) match by symbol name (the GX enums are int-sized), as in
// gx_shim.c. Minimal bodies to link + a sane scissor; wire Z/TEV/texture state into the
// RHI when text-render CORRECTNESS is tackled (GXInitTexObj especially, for glyph textures).
void GXGetScissor(u32* left, u32* top, u32* wd, u32* ht) {
    if (left) *left = 0; if (top) *top = 0; if (wd) *wd = 640; if (ht) *ht = 480;
}
void GXSetZMode(u8 compare_enable, int func, u8 update_enable) {
    (void)compare_enable; (void)func; (void)update_enable;
}
void GXSetZCompLoc(u8 before_tex) { (void)before_tex; }
void GXSetTevSwapModeTable(int table, int r, int g, int b, int a) {
    (void)table; (void)r; (void)g; (void)b; (void)a;
}
// GXTexObj is an opaque 0x20-byte record (dolphin GXStruct.h: u32 dummy[8]); the
// game hands us its address (e.g. inside a Texture header) and only ever reads it
// back through the GX accessors below, so we overlay our own fields on it. Just
// enough to (a) round-trip fmt/width/height for initLoadingScreenTextures' arena
// sizing and (b) lazily decode + cache the GC image into an RHI texture on load.
typedef struct PortTexObj {
    void*    data;      // GC-format image bytes (tiled, big-endian texels)
    u16      w, h;
    u32      fmt;       // GXTexFmt
    void*    rhiTex;    // decoded RhiTexture*, NULL until first GXLoadTexObj
    void*    userData;
} PortTexObj;

void GXInitTexObj(void* obj, void* image, u16 width, u16 height, int fmt,
                  int wrap_s, int wrap_t, u8 mipmap) {
    PortTexObj* t = (PortTexObj*)obj;
    (void)wrap_s; (void)wrap_t; (void)mipmap;
    if (!t) return;
    t->data = image; t->w = width; t->h = height; t->fmt = (u32)fmt;
    t->rhiTex = NULL; t->userData = NULL;
}

void GXInitTexObjLOD(void* obj, int min_filt, int mag_filt, float min_lod,
                     float max_lod, float lod_bias, u8 bias_clamp, u8 do_edge_lod,
                     int max_aniso) {
    (void)obj; (void)min_filt; (void)mag_filt; (void)min_lod; (void)max_lod;
    (void)lod_bias; (void)bias_clamp; (void)do_edge_lod; (void)max_aniso;
}

void GXInitTexObjUserData(void* obj, void* user_data) {
    PortTexObj* t = (PortTexObj*)obj;
    if (t) t->userData = user_data;
}

int GXGetTexObjFmt(const void* obj) {
    const PortTexObj* t = (const PortTexObj*)obj;
    return t ? (int)t->fmt : 0;
}
u16 GXGetTexObjWidth(const void* obj) {
    const PortTexObj* t = (const PortTexObj*)obj;
    return t ? t->w : 0;
}
u16 GXGetTexObjHeight(const void* obj) {
    const PortTexObj* t = (const PortTexObj*)obj;
    return t ? t->h : 0;
}

// GC textures are stored as fixed-size tiles; a level's buffer is
// ceil(w/tileW) * ceil(h/tileH) tiles, each a fixed byte count. This reproduces
// the real GXGetTexBufferSize exactly for the base level (verified: IA8 376x104 ->
// 0x13180, RGB5A3 128x160 -> 0xA000, CMPR 180x92 -> 0x2280), so
// initLoadingScreenTextures' arenaHi advance lands each record at its true offset.
static void texTileDims(u32 fmt, int* tw, int* th, int* bytes) {
    switch (fmt) {
    case GX_TF_I4:     *tw = 8; *th = 8; *bytes = 32; break;
    case GX_TF_I8:     *tw = 8; *th = 4; *bytes = 32; break;
    case GX_TF_IA4:    *tw = 8; *th = 4; *bytes = 32; break;
    case GX_TF_IA8:    *tw = 4; *th = 4; *bytes = 32; break;
    case GX_TF_RGB565: *tw = 4; *th = 4; *bytes = 32; break;
    case GX_TF_RGB5A3: *tw = 4; *th = 4; *bytes = 32; break;
    case GX_TF_RGBA8:  *tw = 4; *th = 4; *bytes = 64; break;
    case GX_TF_CMPR:   *tw = 8; *th = 8; *bytes = 32; break;
    default:           *tw = 4; *th = 4; *bytes = 32; break;
    }
}

u32 GXGetTexBufferSize(u16 width, u16 height, u32 format, u8 mipmap, u8 max_lod) {
    int tw, th, bytes;
    u32 total = 0;
    int w = width, h = height, levels = (mipmap ? (max_lod ? max_lod : 1) : 1), i;
    texTileDims(format, &tw, &th, &bytes);
    for (i = 0; i < levels; ++i) {
        int tilesW = (w + tw - 1) / tw;
        int tilesH = (h + th - 1) / th;
        total += (u32)(tilesW * tilesH * bytes);
        if (w == 1 && h == 1) break;
        w = w > 1 ? w >> 1 : 1;
        h = h > 1 ? h >> 1 : 1;
    }
    return total;
}

// Decode the GC image to RGBA8 once, upload it to an RHI texture, cache it on the
// GXTexObj, and bind it for the next primitive (the same slot gx_draw_setTexture
// uses). This is what makes hudDrawColored/drawTexture (and glyph draws) actually
// sample a texture instead of rendering untextured.
void GXLoadTexObj(void* obj, int mapId) {
    PortTexObj* t = (PortTexObj*)obj;
    (void)mapId;
    if (!t || !gRhi) return;
    if (!t->rhiTex && t->data) {
        u8* rgba = gxTexDecode((int)t->fmt, t->w, t->h, (const u8*)t->data);
        if (rgba) {
            t->rhiTex = rhi_createTexture(gRhi, t->w, t->h, 1, t->fmt, rgba);
            free(rgba);
        }
    }
    gTex = (RhiTexture*)t->rhiTex;
}
