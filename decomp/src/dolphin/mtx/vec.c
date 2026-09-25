#include "dolphin/mtx/vec.h"

__declspec(section ".sdata2") const float lbl_803E7648 = 0.5f;
__declspec(section ".sdata2") const float lbl_803E764C = 3.0f;
__declspec(section ".sdata2") const float lbl_803E7650 = 2.0f;
#pragma explicit_zero_data on
__declspec(section ".sdata2") const float lbl_803E7654 = 0.0f;
#pragma explicit_zero_data off

#define qr0                     0
#define LOAD_SDATA2_FLOAT(name) (*(const volatile f32*)&(name))

asm void PSVECAdd(const register Vec* vec1, const register Vec* vec2, register Vec* ret) {
#ifdef __MWERKS__ // clang-format off
	nofralloc;
	psq_l     f2, Vec.x(vec1), 0, qr0;
	psq_l     f4, Vec.x(vec2), 0, qr0;
	ps_add    f6, f2, f4;
	psq_st    f6, Vec.x(ret), 0, qr0;
	psq_l     f3, Vec.z(vec1), 1, qr0;
	psq_l     f5, Vec.z(vec2), 1, qr0;
	ps_add    f7, f3, f5;
	psq_st    f7, Vec.z(ret), 1, qr0;
	blr
#endif // clang-format on
}

asm void PSVECSubtract(const register Vec* vec1, const register Vec* vec2, register Vec* ret) {
#ifdef __MWERKS__ // clang-format off
	nofralloc;
	psq_l     f2, Vec.x(vec1), 0, qr0;
	psq_l     f4, Vec.x(vec2), 0, qr0;
	ps_sub    f6, f2, f4;
	psq_st    f6, Vec.x(ret), 0, qr0;
	psq_l     f3, Vec.z(vec1), 1, qr0;
	psq_l     f5, Vec.z(vec2), 1, qr0;
	ps_sub    f7, f3, f5;
	psq_st    f7, Vec.z(ret), 1, qr0;
	blr
#endif // clang-format on
}

void PSVECScale(register const Vec* src, register Vec* dst, register f32 scale) {
    register f32 vxy;
    register f32 vz;
    register f32 rxy;
    register f32 rz;

#ifdef __MWERKS__ // clang-format off
	asm {
		psq_l vxy, Vec.x(src), 0, qr0
		psq_l vz, Vec.z(src), 1, qr0
		ps_muls0 rxy, vxy, scale
		psq_st rxy, Vec.x(dst), 0, qr0
		ps_muls0 rz, vz, scale
		psq_st rz, Vec.z(dst), 1, qr0
	}
#endif // clang-format on
}

void PSVECNormalize(const register Vec* vec1, register Vec* ret) {
    register f32 c_half = LOAD_SDATA2_FLOAT(lbl_803E7648);
    register f32 c_three = LOAD_SDATA2_FLOAT(lbl_803E764C);
    register f32 v1_xy;
    register f32 v1_z;
    register f32 xx_zz;
    register f32 xx_yy;
    register f32 sqsum;
    register f32 rsqrt;
    register f32 nwork0;
    register f32 nwork1;

#ifdef __MWERKS__ // clang-format off
	asm {
		psq_l v1_xy, Vec.x(vec1), 0, qr0
		ps_mul xx_yy, v1_xy, v1_xy
		psq_l v1_z, Vec.z(vec1), 1, qr0
		ps_madd xx_zz, v1_z, v1_z, xx_yy
		ps_sum0 sqsum, xx_zz, v1_z, xx_yy
		frsqrte rsqrt, sqsum
		fmuls nwork0, rsqrt, rsqrt
		fmuls nwork1, rsqrt, c_half
		fnmsubs nwork0, nwork0, sqsum, c_three
		fmuls rsqrt, nwork0, nwork1
		ps_muls0 v1_xy, v1_xy, rsqrt
		psq_st v1_xy, Vec.x(ret), 0, qr0
		ps_muls0 v1_z, v1_z, rsqrt
		psq_st v1_z, Vec.z(ret), 1, qr0
	}
#endif // clang-format on
}

f32 PSVECSquareMag(register const Vec* v) {
    register f32 vxy;
    register f32 vzz;
    register f32 sqmag;

#ifdef __MWERKS__ // clang-format off
	asm {
		psq_l vxy, Vec.x(v), 0, qr0
		ps_mul vxy, vxy, vxy
		lfs vzz, Vec.z(v)
		ps_madd sqmag, vzz, vzz, vxy
		ps_sum0 sqmag, sqmag, vxy, vxy
	}
#endif // clang-format on
    return sqmag;
}

f32 PSVECMag(const register Vec* v) {
    register f32 vxy;
    register f32 vzz;
    register f32 sqmag;
    register f32 rmag;
    register f32 nwork0;
    register f32 nwork1;
    register f32 c_three;
    register f32 c_half;

#ifdef __MWERKS__ // clang-format off
	asm {
		psq_l vxy, Vec.x(v), 0, qr0
		ps_mul vxy, vxy, vxy
		lfs vzz, Vec.z(v)
		ps_madd sqmag, vzz, vzz, vxy
	}
	c_half = LOAD_SDATA2_FLOAT(lbl_803E7648);
	asm {
		ps_sum0 sqmag, sqmag, vxy, vxy
		frsqrte rmag, sqmag
	}
	c_three = LOAD_SDATA2_FLOAT(lbl_803E764C);
	asm {
		fmuls nwork0, rmag, rmag
		fmuls nwork1, rmag, c_half
		fnmsubs nwork0, nwork0, sqmag, c_three
		fmuls rmag, nwork0, nwork1
		fsel rmag, rmag, rmag, sqmag
		fmuls sqmag, sqmag, rmag
	}
#endif // clang-format on
    return sqmag;
}

asm f32 PSVECDotProduct(const register Vec* vec1, const register Vec* vec2) {
#ifdef __MWERKS__ // clang-format off
	nofralloc;
    psq_l      f2, Vec.y(vec1), 0, qr0
    psq_l      f3, Vec.y(vec2), 0, qr0
    ps_mul     f2, f2, f3
    psq_l      f5, Vec.x(vec1), 0, qr0
    psq_l      f4, Vec.x(vec2), 0, qr0
    ps_madd    f3, f5, f4, f2
    ps_sum0    f1, f3, f2, f2
    blr
#endif // clang-format on
}

asm void PSVECCrossProduct(register const Vec* a, register const Vec* b, register Vec* axb) {
#ifdef __MWERKS__ // clang-format off
	nofralloc
    psq_l          f1, Vec.x(b), 0, qr0
    lfs            f2, Vec.z(a)
    psq_l          f0, Vec.x(a), 0, qr0
    ps_merge10     f6, f1, f1
    lfs            f3, Vec.z(b)
    ps_mul         f4, f1, f2
    ps_muls0       f7, f1, f0
    ps_msub        f5, f0, f3, f4
    ps_msub        f8, f0, f6, f7
    ps_merge11     f9, f5, f5
    ps_merge01     f10, f5, f8
    psq_st         f9, Vec.x(axb), 1, qr0
    ps_neg         f10, f10
    psq_st         f10, Vec.y(axb), 0, qr0
    blr
#endif // clang-format on
}

// clang-format off
asm void C_VECHalfAngle(const Vec *a, const Vec *b, Vec *half)
{
    nofralloc
    mflr r0
    stw r0, 0x4(r1)
    stwu r1, -0x38(r1)
    stw r31, 0x34(r1)
    stw r30, 0x30(r1)
    mr r30, r4
    mr r31, r5
    lfs f0, 0x0(r3)
    fneg f0, f0
    stfs f0, 0x20(r1)
    lfs f0, 0x4(r3)
    fneg f0, f0
    stfs f0, 0x24(r1)
    lfs f0, 0x8(r3)
    addi r3, r1, 0x20
    mr r4, r3
    fneg f0, f0
    stfs f0, 0x28(r1)
    bl PSVECNormalize
    mr r3, r30
    addi r4, r1, 0x14
    bl PSVECNormalize
    addi r3, r1, 0x20
    addi r4, r1, 0x14
    bl PSVECDotProduct
    lfs f3, lbl_803E7650(r0)
    mr r3, r31
    lfs f2, 0x14(r1)
    mr r4, r31
    lfs f0, 0x20(r1)
    fmuls f2, f3, f2
    fmuls f2, f2, f1
    fsubs f0, f2, f0
    stfs f0, 0x0(r31)
    lfs f2, 0x18(r1)
    lfs f0, 0x24(r1)
    fmuls f2, f3, f2
    fmuls f2, f2, f1
    fsubs f0, f2, f0
    stfs f0, 0x4(r31)
    lfs f2, 0x1c(r1)
    lfs f0, 0x28(r1)
    fmuls f2, f3, f2
    fmuls f1, f2, f1
    fsubs f0, f1, f0
    stfs f0, 0x8(r31)
    bl PSVECNormalize
    lwz r0, 0x3c(r1)
    lwz r31, 0x34(r1)
    lwz r30, 0x30(r1)
    mtlr r0
    addi r1, r1, 0x38
    blr
} // clang-format on

f32 PSVECSquareDistance(register const Vec* a, register const Vec* b) {
    register f32 v0yz;
    register f32 v1yz;
    register f32 v0xy;
    register f32 v1xy;
    register f32 dyz;
    register f32 dxy;
    register f32 sqdist;

#ifdef __MWERKS__ // clang-format off
	asm {
		psq_l v0yz, Vec.y(a), 0, qr0
		psq_l v1yz, Vec.y(b), 0, qr0
		ps_sub dyz, v0yz, v1yz
		psq_l v0xy, Vec.x(a), 0, qr0
		psq_l v1xy, Vec.x(b), 0, qr0
		ps_mul dyz, dyz, dyz
		ps_sub dxy, v0xy, v1xy
		ps_madd sqdist, dxy, dxy, dyz
		ps_sum0 sqdist, sqdist, dyz, dyz
	}
#endif // clang-format on
    return sqdist;
}

f32 PSVECDistance(register const Vec* a, register const Vec* b) {
    register f32 v0yz;
    register f32 v1yz;
    register f32 v0xy;
    register f32 v1xy;
    register f32 dyz;
    register f32 dxy;
    register f32 sqdist;
    register f32 rdist;
    register f32 dist;
    register f32 nwork0;
    register f32 nwork1;
    register f32 c_half;
    register f32 c_three;

#ifdef __MWERKS__ // clang-format off
	asm {
		psq_l v0yz, Vec.y(a), 0, qr0
		psq_l v1yz, Vec.y(b), 0, qr0
		ps_sub dyz, v0yz, v1yz
		psq_l v0xy, Vec.x(a), 0, qr0
		psq_l v1xy, Vec.x(b), 0, qr0
		ps_mul dyz, dyz, dyz
		ps_sub dxy, v0xy, v1xy
	}
	c_half = LOAD_SDATA2_FLOAT(lbl_803E7648);
	asm {
		ps_madd sqdist, dxy, dxy, dyz
		ps_sum0 sqdist, sqdist, dyz, dyz
	}
	c_three = LOAD_SDATA2_FLOAT(lbl_803E764C);
	asm {
		frsqrte rdist, sqdist
		fmuls nwork0, rdist, rdist
		fmuls nwork1, rdist, c_half
		fnmsubs nwork0, nwork0, sqdist, c_three
		fmuls rdist, nwork0, nwork1
		fsel rdist, rdist, rdist, sqdist
		fmuls dist, sqdist, rdist
	}
#endif // clang-format on
    return dist;
}
