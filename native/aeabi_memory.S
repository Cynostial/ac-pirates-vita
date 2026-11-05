/* Android ARM RTABI adapters. Original code, MIT.
 * RTABI32 5.3.4 permits only the usual volatile integer registers to change.
 * Calling plain libc memcpy/memmove/memset does not meet that contract.
 */
.syntax unified
.arch armv7-a
.fpu neon
.thumb
.text
.align 2

.macro memory_helper name, target, reverse=0, move=0
.global \name
.type \name, %function
.thumb_func
\name:
    .if !\reverse && !\move
        /* Common 4x4 matrix transfer: integer registers avoid spilling all
         * volatile FP registers merely to call native memcpy for 64 bytes. */
        cmp r2, #64
        bne 7f
        orr r3, r0, r1
        tst r3, #3
        bne 9f
        push {r0, r4-r7, lr}
        ldmia r1!, {r2-r7, r12, lr}
        stmia r0!, {r2-r7, r12, lr}
        ldmia r1!, {r2-r7, r12, lr}
        stmia r0!, {r2-r7, r12, lr}
        pop {r0, r4-r7, pc}
    7:
    .endif
    /* Structure copies are commonly aligned and 12-32 bytes long. Integer
     * word transfers preserve every FP register without an FP spill/call.
     * Retain byte accesses for unaligned small inputs and libc for large ones. */
    .if \reverse
        cmp r1, #32
        bhi 9f
        cbz r1, 8f
        tst r0, #3
        bne 6f
        mov r3, r0
        and r12, r2, #255
        orr r12, r12, r12, lsl #8
        orr r12, r12, r12, lsl #16
    3:
        cmp r1, #4
        blo 1f
        str r12, [r3], #4
        subs r1, #4
        bne 3b
        bx lr
    6:
        cmp r1, #32
        bhi 9f
        mov r3, r0
    1:
        cbz r1, 8f
        strb r2, [r3], #1
        subs r1, #1
        bne 1b
        bx lr
    .else
        cmp r2, #32
        bhi 9f
        cbz r2, 8f
        orr r3, r0, r1
        tst r3, #3
        bne 6f
        mov r12, r0
        .if \move
            cmp r0, r1
            bls 3f
            add r12, r2
            add r1, r2
        4:
            tst r2, #3
            beq 5f
            ldrb r3, [r1, #-1]!
            strb r3, [r12, #-1]!
            subs r2, #1
            bne 4b
            bx lr
        5:
            ldr r3, [r1, #-4]!
            str r3, [r12, #-4]!
            subs r2, #4
            bne 5b
            bx lr
        .endif
    3:
        cmp r2, #4
        blo 1f
        ldr r3, [r1], #4
        str r3, [r12], #4
        subs r2, #4
        bne 3b
        bx lr
    6:
        cmp r2, #32
        bhi 9f
        mov r12, r0
        .if \move
            cmp r0, r1
            bls 1f
            add r12, r2
            add r1, r2
        2:
            ldrb r3, [r1, #-1]!
            strb r3, [r12, #-1]!
            subs r2, #1
            bne 2b
            bx lr
        .endif
    1:
        cbz r2, 8f
        ldrb r3, [r1], #1
        strb r3, [r12], #1
        subs r2, #1
        bne 1b
    .endif
8:
    bx lr
9:
    push {r4, lr}
    vmrs r4, fpscr
    vpush {d0-d7}
    vpush {d16-d31}
    .if \reverse
        mov r3, r1
        mov r1, r2
        mov r2, r3
    .endif
    bl \target
    vpop {d16-d31}
    vpop {d0-d7}
    vmsr fpscr, r4
    pop {r4, pc}
.size \name, .-\name
.endm

memory_helper acp_aeabi_memcpy, memcpy
memory_helper acp_aeabi_memmove, memmove, 0, 1
memory_helper acp_aeabi_memset, memset, 1
.section .note.GNU-stack,"",%progbits
