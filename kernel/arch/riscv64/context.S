.section .text

CTX_RA      = 0
CTX_SP      = 8
CTX_S0      = 56
CTX_S1      = 64
CTX_S2      = 136
CTX_S3      = 144
CTX_S4      = 152
CTX_S5      = 160
CTX_S6      = 168
CTX_S7      = 176
CTX_S8      = 184
CTX_S9      = 192
CTX_S10     = 200
CTX_S11     = 208
CTX_SEPC    = 248

.macro save_kernel_context ctx, resume
    sd ra, CTX_RA(\ctx)
    sd sp, CTX_SP(\ctx)
    sd s0, CTX_S0(\ctx)
    sd s1, CTX_S1(\ctx)
    sd s2, CTX_S2(\ctx)
    sd s3, CTX_S3(\ctx)
    sd s4, CTX_S4(\ctx)
    sd s5, CTX_S5(\ctx)
    sd s6, CTX_S6(\ctx)
    sd s7, CTX_S7(\ctx)
    sd s8, CTX_S8(\ctx)
    sd s9, CTX_S9(\ctx)
    sd s10, CTX_S10(\ctx)
    sd s11, CTX_S11(\ctx)
    la t0, \resume
    sd t0, CTX_SEPC(\ctx)
.endm

.macro restore_kernel_context ctx
    ld ra, CTX_RA(\ctx)
    ld sp, CTX_SP(\ctx)
    ld s0, CTX_S0(\ctx)
    ld s1, CTX_S1(\ctx)
    ld s2, CTX_S2(\ctx)
    ld s3, CTX_S3(\ctx)
    ld s4, CTX_S4(\ctx)
    ld s5, CTX_S5(\ctx)
    ld s6, CTX_S6(\ctx)
    ld s7, CTX_S7(\ctx)
    ld s8, CTX_S8(\ctx)
    ld s9, CTX_S9(\ctx)
    ld s10, CTX_S10(\ctx)
    ld s11, CTX_S11(\ctx)
    ld t0, CTX_SEPC(\ctx)
    jr t0
.endm

.globl riscv64_kernel_switch
.type riscv64_kernel_switch, @function
riscv64_kernel_switch:
    save_kernel_context a0, 1f
    restore_kernel_context a1
1:
    ret

.globl riscv64_kernel_switch_to_user
.type riscv64_kernel_switch_to_user, @function
riscv64_kernel_switch_to_user:
    save_kernel_context a0, 1f
    mv a0, a1
    j isr_restore_user
1:
    ret
