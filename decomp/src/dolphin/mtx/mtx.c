#include "dolphin/mtx.h"
#include "PowerPC_EABI_Support/Msl/MSL_C/MSL_Common/math.h"

static f32 Unit01[] = {0.0f, 1.0f};

__declspec(section ".sdata2") const f32 lbl_803E7618 = 1.0f;
#pragma explicit_zero_data on
__declspec(section ".sdata2") const f32 lbl_803E761C = 0.0f;
#pragma explicit_zero_data off
__declspec(section ".sdata2") const f32 lbl_803E7620 = 2.0f;
__declspec(section ".sdata2") const f32 lbl_803E7624 = -1.0f;
__declspec(section ".sdata2") const f32 lbl_803E7628 = 0.5f;
__declspec(section ".sdata2") const f32 lbl_803E762C = 0.017453292f;

#define Mtx_00                  0x0
#define Mtx_01                  0x4
#define Mtx_02                  0x8
#define Mtx_03                  0xc
#define Mtx_10                  0x10
#define Mtx_11                  0x14
#define Mtx_12                  0x18
#define Mtx_13                  0x1c
#define Mtx_20                  0x20
#define Mtx_21                  0x24
#define Mtx_22                  0x28
#define Mtx_23                  0x2c
#define qr0                     0
#define LOAD_SDATA2_FLOAT(name) (*(const volatile f32*)&(name))

#ifdef GEKKO
void PSMTXIdentity(register Mtx m) {
    register f32 c_zero = LOAD_SDATA2_FLOAT(lbl_803E761C);
    register f32 c_one = LOAD_SDATA2_FLOAT(lbl_803E7618);
    register f32 c_01;
    register f32 c_10;

    asm {
        psq_st c_zero, Mtx_02(m), 0, qr0
        ps_merge01 c_01, c_zero, c_one
        psq_st c_zero, Mtx_12(m), 0, qr0
        ps_merge10 c_10, c_one, c_zero
        psq_st c_zero, Mtx_20(m), 0, qr0
        psq_st c_01, Mtx_10(m), 0, qr0
        psq_st c_10, Mtx_00(m), 0, qr0
        psq_st c_10, Mtx_22(m), 0, qr0
    }
}

// clang-format off
asm void PSMTXCopy(const register Mtx src, register Mtx dst)
{
    nofralloc
    psq_l f0, Mtx_00(src), 0, qr0
    psq_st f0, Mtx_00(dst), 0, qr0
    psq_l f1, Mtx_02(src), 0, qr0
    psq_st f1, Mtx_02(dst), 0, qr0
    psq_l f2, Mtx_10(src), 0, qr0
    psq_st f2, Mtx_10(dst), 0, qr0
    psq_l f3, Mtx_12(src), 0, qr0
    psq_st f3, Mtx_12(dst), 0, qr0
    psq_l f4, Mtx_20(src), 0, qr0
    psq_st f4, Mtx_20(dst), 0, qr0
    psq_l f5, Mtx_22(src), 0, qr0
    psq_st f5, Mtx_22(dst), 0, qr0
    blr
}
// clang-format on

// clang-format off
asm void PSMTXConcat(const register Mtx mA, const register Mtx mB, register Mtx mAB)
{
    nofralloc
    stwu    sp, -64(sp)
    psq_l   fp0, Mtx_00(mA), 0, qr0
    stfd    fp14, 8(sp)
    psq_l   fp6, Mtx_00(mB), 0, qr0
    lis     r6, Unit01@ha
    psq_l   fp7, Mtx_02(mB), 0, qr0
    stfd    fp15, 16(sp)
    addi    r6, r6, Unit01@l
    stfd    fp31, 40(sp)
    psq_l   fp8, Mtx_10(mB), 0, qr0
    ps_muls0 fp12, fp6, fp0
    psq_l   fp2, Mtx_10(mA), 0, qr0
    ps_muls0 fp13, fp7, fp0
    psq_l   fp31, 0(r6), 0, qr0
    ps_muls0 fp14, fp6, fp2
    psq_l   fp9, Mtx_12(mB), 0, qr0
    ps_muls0 fp15, fp7, fp2
    psq_l   fp1, Mtx_02(mA), 0, qr0
    ps_madds1 fp12, fp8, fp0, fp12
    psq_l   fp3, Mtx_12(mA), 0, qr0
    ps_madds1 fp14, fp8, fp2, fp14
    psq_l   fp10, Mtx_20(mB), 0, qr0
    ps_madds1 fp13, fp9, fp0, fp13
    psq_l   fp11, Mtx_22(mB), 0, qr0
    ps_madds1 fp15, fp9, fp2, fp15
    psq_l   fp4, Mtx_20(mA), 0, qr0
    psq_l   fp5, Mtx_22(mA), 0, qr0
    ps_madds0 fp12, fp10, fp1, fp12
    ps_madds0 fp13, fp11, fp1, fp13
    ps_madds0 fp14, fp10, fp3, fp14
    ps_madds0 fp15, fp11, fp3, fp15
    psq_st  fp12, Mtx_00(mAB), 0, qr0
    ps_muls0 fp2, fp6, fp4
    ps_madds1 fp13, fp31, fp1, fp13
    ps_muls0 fp0, fp7, fp4
    psq_st  fp14, Mtx_10(mAB), 0, qr0
    ps_madds1 fp15, fp31, fp3, fp15
    psq_st  fp13, Mtx_02(mAB), 0, qr0
    ps_madds1 fp2, fp8, fp4, fp2
    ps_madds1 fp0, fp9, fp4, fp0
    ps_madds0 fp2, fp10, fp5, fp2
    lfd    fp14, 8(sp)
    psq_st  fp15, Mtx_12(mAB), 0, qr0
    ps_madds0 fp0, fp11, fp5, fp0
    psq_st  fp2, Mtx_20(mAB), 0, qr0
    ps_madds1 fp0, fp31, fp5, fp0
    lfd    fp15, 16(sp)
    psq_st  fp0, Mtx_22(mAB), 0, qr0
    lfd    fp31, 40(sp)
    addi   sp, sp, 64
    blr
}
// clang-format on

void PSMTXTranspose(const register Mtx src, register Mtx xPose) {
    register f32 c_zero = LOAD_SDATA2_FLOAT(lbl_803E761C);
    register f32 row0a;
    register f32 row1a;
    register f32 row0b;
    register f32 row1b;
    register f32 trns0;
    register f32 trns1;
    register f32 trns2;

    asm {
        psq_l row0a, Mtx_00(src), 0, qr0
        stfs c_zero, Mtx_23(xPose)
        psq_l row1a, Mtx_10(src), 0, qr0
        ps_merge00 trns0, row0a, row1a
        psq_l row0b, Mtx_02(src), 1, qr0
        ps_merge11 trns1, row0a, row1a
        psq_l row1b, Mtx_12(src), 1, qr0
        psq_st trns0, Mtx_00(xPose), 0, qr0
        psq_l row0a, Mtx_20(src), 0, qr0
        ps_merge00 trns2, row0b, row1b
        psq_st trns1, Mtx_10(xPose), 0, qr0
        ps_merge00 trns0, row0a, c_zero
        psq_st trns2, Mtx_20(xPose), 0, qr0
        ps_merge10 trns1, row0a, c_zero
        psq_st trns0, Mtx_02(xPose), 0, qr0
        lfs row0b, Mtx_22(src)
        psq_st trns1, Mtx_12(xPose), 0, qr0
        stfs row0b, Mtx_22(xPose)
    }
}

// clang-format off
asm u32 PSMTXInverse(const register Mtx src, register Mtx inv)
{
    nofralloc
    psq_l       fp0, Mtx_00(src), 1, qr0
    psq_l       fp1, Mtx_01(src), 0, qr0
    psq_l       fp2, Mtx_10(src), 1, qr0
    ps_merge10  fp6, fp1, fp0
    psq_l       fp3, Mtx_11(src), 0, qr0
    psq_l       fp4, Mtx_20(src), 1, qr0
    ps_merge10  fp7, fp3, fp2
    psq_l       fp5, Mtx_21(src), 0, qr0
    ps_mul      fp11, fp3, fp6
    ps_mul      fp13, fp5, fp7
    ps_merge10  fp8, fp5, fp4
    ps_msub     fp11, fp1, fp7, fp11
    ps_mul      fp12, fp1, fp8
    ps_msub     fp13, fp3, fp8, fp13
    ps_mul      fp10, fp3, fp4
    ps_msub     fp12, fp5, fp6, fp12
    ps_mul      fp9,  fp0, fp5
    ps_mul      fp8,  fp1, fp2
    ps_sub      fp6, fp6, fp6
    ps_msub     fp10, fp2, fp5, fp10
    ps_mul      fp7, fp0, fp13
    ps_msub     fp9,  fp1, fp4, fp9
    ps_madd     fp7, fp2, fp12, fp7
    ps_msub     fp8,  fp0, fp3, fp8
    ps_madd     fp7, fp4, fp11, fp7
    ps_cmpo0    cr0, fp7, fp6
    bne         _regular
    li          r3, 0
    blr
_regular:
    fres        fp0, fp7
    ps_add      fp6, fp0, fp0
    ps_mul      fp5, fp0, fp0
    ps_nmsub    fp0, fp7, fp5, fp6
    lfs         fp1, Mtx_03(src)
    ps_muls0    fp13, fp13, fp0
    lfs         fp2, Mtx_13(src)
    ps_muls0    fp12, fp12, fp0
    lfs         fp3, Mtx_23(src)
    ps_muls0    fp11, fp11, fp0
    ps_merge00  fp5, fp13, fp12
    ps_muls0    fp10, fp10, fp0
    ps_merge11  fp4, fp13, fp12
    ps_muls0    fp9,  fp9,  fp0
    psq_st      fp5,  Mtx_00(inv), 0, qr0
    ps_mul      fp6, fp13, fp1
    psq_st      fp4,  Mtx_10(inv), 0, qr0
    ps_muls0    fp8,  fp8,  fp0
    ps_madd     fp6, fp12, fp2, fp6
    psq_st      fp10, Mtx_20(inv), 1, qr0
    ps_nmadd    fp6, fp11, fp3, fp6
    psq_st      fp9,  Mtx_21(inv), 1, qr0
    ps_mul      fp7, fp10, fp1
    ps_merge00  fp5, fp11, fp6
    psq_st      fp8,  Mtx_22(inv), 1, qr0
    ps_merge11  fp4, fp11, fp6
    psq_st      fp5,  Mtx_02(inv), 0, qr0
    ps_madd     fp7, fp9,  fp2, fp7
    psq_st      fp4,  Mtx_12(inv), 0, qr0
    ps_nmadd    fp7, fp8,  fp3, fp7
    li          r3, 1
    psq_st      fp7,  Mtx_23(inv), 1, qr0
    blr
}

asm void PSMTXRotRad(Mtx m, char axis, f32 rad)
{
    nofralloc
    mflr r0
    stw r0, 0x4(r1)
    stwu r1, -0x28(r1)
    stfd f31, 0x20(r1)
    stw r31, 0x1c(r1)
    stw r30, 0x18(r1)
    fmr f31, f1
    mr r30, r3
    mr r31, r4
    fmr f1, f31
    bl sinf
    fmr f0, f1
    fmr f1, f31
    fmr f31, f0
    bl cosf
    fmr f0, f1
    mr r3, r30
    fmr f1, f31
    mr r4, r31
    fmr f2, f0
    bl PSMTXRotTrig
    lwz r0, 0x2c(r1)
    lfd f31, 0x20(r1)
    lwz r31, 0x1c(r1)
    mtlr r0
    lwz r30, 0x18(r1)
    addi r1, r1, 0x28
    blr
}
// clang-format on

void PSMTXRotTrig(register Mtx m, register char axis, register f32 sinA, register f32 cosA) {
    register f32 fc0 = LOAD_SDATA2_FLOAT(lbl_803E761C);
    register f32 fc1 = LOAD_SDATA2_FLOAT(lbl_803E7618);
    register f32 nsinA;
    register f32 fw0;
    register f32 fw1;
    register f32 fw2;
    register f32 fw3;

    asm {
        ori axis, axis, 0x20
        ps_neg nsinA, sinA
        cmplwi axis, 'x'
        beq axis_x
        cmplwi axis, 'y'
        beq axis_y
        cmplwi axis, 'z'
        beq axis_z
        b epilogue
    axis_x:
        psq_st fc1, Mtx_00(m), 1, qr0
        psq_st fc0, Mtx_01(m), 0, qr0
        ps_merge00 fw0, sinA, cosA
        psq_st fc0, Mtx_03(m), 0, qr0
        ps_merge00 fw1, cosA, nsinA
        psq_st fc0, Mtx_13(m), 0, qr0
        psq_st fc0, Mtx_23(m), 1, qr0
        psq_st fw0, Mtx_21(m), 0, qr0
        psq_st fw1, Mtx_11(m), 0, qr0
        b epilogue
    axis_y:
        ps_merge00 fw0, cosA, fc0
        ps_merge00 fw1, fc0, fc1
        psq_st fc0, Mtx_12(m), 0, qr0
        psq_st fw0, Mtx_00(m), 0, qr0
        ps_merge00 fw2, nsinA, fc0
        ps_merge00 fw3, sinA, fc0
        psq_st fw0, Mtx_22(m), 0, qr0
        psq_st fw1, Mtx_10(m), 0, qr0
        psq_st fw3, Mtx_02(m), 0, qr0
        psq_st fw2, Mtx_20(m), 0, qr0
        b epilogue
    axis_z:
        psq_st fc0, Mtx_02(m), 0, qr0
        ps_merge00 fw0, sinA, cosA
        ps_merge00 fw2, cosA, nsinA
        psq_st fc0, Mtx_12(m), 0, qr0
        psq_st fc0, Mtx_20(m), 0, qr0
        ps_merge00 fw1, fc1, fc0
        psq_st fw0, Mtx_10(m), 0, qr0
        psq_st fw2, Mtx_00(m), 0, qr0
        psq_st fw1, Mtx_22(m), 0, qr0
    epilogue:
    }
}

// clang-format off
asm void PSMTXRotAxisRad(register Mtx m, const Vec *axis, register f32 rad)
{
    nofralloc
    mflr r0
    stw r0, 0x4(r1)
    stwu r1, -0x58(r1)
    stfd f31, 0x50(r1)
    stfd f30, 0x48(r1)
    stfd f29, 0x40(r1)
    stfd f28, 0x38(r1)
    stfd f27, 0x30(r1)
    stw r31, 0x2c(r1)
    stw r30, 0x28(r1)
    stw r29, 0x24(r1)
    fmr f27, f1
    mr r29, r3
    mr r30, r4
    fmr f1, f27
    lfs f28, lbl_803E761C(r2)
    addi r31, r1, 0x14
    bl sinf
    fmr f30, f1
    fmr f1, f27
    bl cosf
    fmr f31, f1
    lfs f0, lbl_803E7618(r2)
    mr r3, r30
    mr r4, r31
    fsubs f29, f0, f31
    bl PSVECNormalize
    psq_l f27, 0x0(r31), 0, 0
    lfs f1, 0x1c(r1)
    ps_merge00 f0, f31, f31
    ps_muls0 f4, f27, f29
    ps_muls0 f5, f1, f29
    ps_muls1 f3, f4, f27
    ps_muls0 f2, f4, f27
    ps_muls0 f27, f27, f30
    ps_muls0 f4, f4, f1
    fnmsubs f6, f1, f30, f3
    fmadds f7, f1, f30, f3
    ps_neg f9, f27
    ps_sum0 f8, f4, f28, f27
    ps_sum0 f2, f2, f6, f0
    ps_sum1 f3, f0, f7, f3
    ps_sum0 f6, f9, f28, f4
    ps_sum0 f9, f4, f4, f9
    psq_st f8, 0x8(r29), 0, 0
    ps_muls0 f5, f5, f1
    psq_st f2, 0x0(r29), 0, 0
    ps_sum1 f4, f27, f9, f4
    psq_st f3, 0x10(r29), 0, 0
    ps_sum0 f5, f5, f28, f0
    psq_st f6, 0x18(r29), 0, 0
    psq_st f4, 0x20(r29), 0, 0
    psq_st f5, 0x28(r29), 0, 0
    lwz r0, 0x5c(r1)
    lfd f31, 0x50(r1)
    lfd f30, 0x48(r1)
    mtlr r0
    lfd f29, 0x40(r1)
    lfd f28, 0x38(r1)
    lfd f27, 0x30(r1)
    lwz r31, 0x2c(r1)
    lwz r30, 0x28(r1)
    lwz r29, 0x24(r1)
    addi r1, r1, 0x58
    blr
}
// clang-format on

void PSMTXTrans(register Mtx m, register f32 xT, register f32 yT, register f32 zT) {
    register f32 c0 = LOAD_SDATA2_FLOAT(lbl_803E761C);
    register f32 c1 = LOAD_SDATA2_FLOAT(lbl_803E7618);

    asm {
        stfs xT, Mtx_03(m)
        stfs yT, Mtx_13(m)
        psq_st c0, Mtx_01(m), 0, qr0
        psq_st c0, Mtx_20(m), 0, qr0
        stfs c0, Mtx_10(m)
        stfs c1, Mtx_11(m)
        stfs c0, Mtx_12(m)
        stfs c1, Mtx_22(m)
        stfs zT, Mtx_23(m)
        stfs c1, Mtx_00(m)
    }
}

void PSMTXScale(register Mtx m, register f32 xS, register f32 yS, register f32 zS) {
    register f32 c0 = LOAD_SDATA2_FLOAT(lbl_803E761C);

    asm {
        stfs xS, Mtx_00(m)
        psq_st c0, Mtx_01(m), 0, qr0
        psq_st c0, Mtx_03(m), 0, qr0
        stfs yS, Mtx_11(m)
        psq_st c0, Mtx_12(m), 0, qr0
        psq_st c0, Mtx_20(m), 0, qr0
        stfs zS, Mtx_22(m)
        stfs c0, Mtx_23(m)
    }
}

// clang-format off
asm void C_MTXLightPerspective(Mtx m, f32 fovY, f32 aspect, float scaleS, float scaleT, float transS, float transT)
{
    nofralloc
    mflr r0
    stw r0, 0x4(r1)
    stwu r1, -0x58(r1)
    stfd f31, 0x50(r1)
    stfd f30, 0x48(r1)
    stfd f29, 0x40(r1)
    stfd f28, 0x38(r1)
    stfd f27, 0x30(r1)
    stw r31, 0x2c(r1)
    fmr f27, f2
    mr r31, r3
    fmr f28, f3
    fmr f29, f4
    fmr f30, f5
    fmr f31, f6
    lfs f2, lbl_803E7628(r2)
    lfs f0, lbl_803E762C(r2)
    fmuls f1, f2, f1
    fmuls f1, f0, f1
    bl tanf
    lfs f3, lbl_803E7618(r2)
    fneg f2, f30
    fneg f0, f31
    fdivs f4, f3, f1
    fdivs f1, f4, f27
    fmuls f3, f28, f1
    fmuls f1, f4, f29
    stfs f3, 0x0(r31)
    lfs f3, lbl_803E761C(r2)
    stfs f3, 0x4(r31)
    stfs f2, 0x8(r31)
    stfs f3, 0xc(r31)
    stfs f3, 0x10(r31)
    stfs f1, 0x14(r31)
    stfs f0, 0x18(r31)
    stfs f3, 0x1c(r31)
    stfs f3, 0x20(r31)
    stfs f3, 0x24(r31)
    lfs f0, lbl_803E7624(r2)
    stfs f0, 0x28(r31)
    stfs f3, 0x2c(r31)
    lwz r0, 0x5c(r1)
    lfd f31, 0x50(r1)
    lfd f30, 0x48(r1)
    mtlr r0
    lfd f29, 0x40(r1)
    lfd f28, 0x38(r1)
    lfd f27, 0x30(r1)
    lwz r31, 0x2c(r1)
    addi r1, r1, 0x58
    blr
}

asm void C_MTXLightOrtho(Mtx m, f32 t, f32 b, f32 l, f32 r, float scaleS, float scaleT, float transS, float transT)
{
    nofralloc
    fsubs f10, f4, f3
    lfs f11, lbl_803E7618(r2)
    fsubs f0, f1, f2
    lfs f9, lbl_803E7620(r2)
    fadds f3, f4, f3
    fdivs f12, f11, f10
    fdivs f10, f11, f0
    fmuls f4, f9, f12
    fneg f3, f3
    fadds f0, f1, f2
    fmuls f1, f4, f5
    fmuls f2, f12, f3
    fneg f0, f0
    stfs f1, 0x0(r3)
    fmuls f1, f9, f10
    fmuls f2, f5, f2
    lfs f3, lbl_803E761C(r2)
    fmuls f0, f10, f0
    stfs f3, 0x4(r3)
    fadds f2, f7, f2
    fmuls f1, f1, f6
    stfs f3, 0x8(r3)
    fmuls f0, f6, f0
    stfs f2, 0xc(r3)
    stfs f3, 0x10(r3)
    fadds f0, f8, f0
    stfs f1, 0x14(r3)
    stfs f3, 0x18(r3)
    stfs f0, 0x1c(r3)
    stfs f3, 0x20(r3)
    stfs f3, 0x24(r3)
    stfs f3, 0x28(r3)
    stfs f11, 0x2c(r3)
    blr
}
// clang-format on

#endif
