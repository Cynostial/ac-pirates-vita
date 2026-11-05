/* Ordered box/plane test from libSparkApp 2.9.1, ELF 0x79532c.
 * The layout/algorithm is also documented in ac-pirates-arm64 (MIT),
 * src/game/perf_patches.cpp. This implementation uses integer NEON masks
 * for corner selection, avoiding six serial FPSCR transfers per plane.
 * Arithmetic remains scalar VFP with the original multiply/add order:
 * NEON floating-point would silently flush subnormals even with FPSCR.FZ=0.
 * softfp ABI: r0=node (unused), r1=box, r2=six planes, r3=extent scale.
 * No callee-saved integer or VFP registers are modified. */
.syntax unified
.fpu neon
.thumb
.text
.align 2
.global acp_frustum
.type acp_frustum,%function
.thumb_func
acp_frustum:
    vmov s15,r3
    vldmia r1,{s0-s5}
    vmul.f32 s3,s15,s3
    vmul.f32 s4,s15,s4
    vmul.f32 s5,s15,s5
    vadd.f32 s8,s0,s3
    vadd.f32 s9,s1,s4
    vadd.f32 s10,s2,s5
    vsub.f32 s0,s0,s3
    vsub.f32 s1,s1,s4
    vsub.f32 s2,s2,s5
    vmov q8,q0
    vmov q9,q2
    /* FZ changes the comparison of subnormal plane normals to zero. */
    vmrs r12,fpscr
    movs r3,#0
    tst r12,#0x01000000
    itt ne
    movwne r3,#0xffff
    movtne r3,#0x7f
    vdup.32 q14,r3
    orr r3,r3,#0x80000000
    vdup.32 q12,r3
    mov r3,#0x7f800000
    vdup.32 q15,r3
    orr r3,r3,#0x80000000
    vdup.32 q13,r3
    movs r0,#0
    movs r1,#6
1:
    vld1.32 {d4-d5},[r2]!
    /* normal < 0: excludes -0 and negative NaNs. */
    vcgt.u32 q0,q2,q12
    vcge.u32 q10,q13,q2
    vand q0,q0,q10
    vbsl q0,q9,q8
    vmul.f32 s1,s1,s9
    vmla.f32 s1,s0,s8
    vmla.f32 s1,s2,s10
    vadd.f32 s1,s11,s1
    vcmpe.f32 s1,#0
    vmrs APSR_nzcv,fpscr
    bgt 3f
    /* normal > 0: excludes +0 and positive NaNs. */
    vcgt.u32 q0,q2,q14
    vcge.u32 q10,q15,q2
    vand q0,q0,q10
    vbsl q0,q9,q8
    vmul.f32 s1,s1,s9
    vmla.f32 s1,s0,s8
    vmla.f32 s1,s2,s10
    vadd.f32 s1,s11,s1
    vcmp.f32 s1,#0
    vmrs APSR_nzcv,fpscr
    it gt
    movgt r0,#1
    subs r1,r1,#1
    bne 1b
    bx lr
3:
    movs r0,#2
    bx lr
.size acp_frustum,.-acp_frustum
