    .extern linux_ss_main

    .text
    .global _start
    .global linux_subsystem_entry
    .type _start, @function
    .type linux_subsystem_entry, @function
_start:
linux_subsystem_entry:
// Save registers that may be used by the C code
    addi sp, sp, -(16 * 8)
    sd ra, 0(sp)
    sd gp, 8(sp)
    sd tp, 16(sp)
    sd t0, 24(sp)
    sd s0, 32(sp)
    sd s1, 40(sp)
    sd s2, 48(sp)
    sd s3, 56(sp)
    sd s4, 64(sp)
    sd s5, 72(sp)
    sd s6, 80(sp)
    sd s7, 88(sp)
    sd s8, 96(sp)
    sd s9, 104(sp)
    sd s10, 112(sp)
    sd s11, 120(sp)

    .option push
    .option norelax
1:
    auipc gp, %pcrel_hi(__global_pointer$)
    addi gp, gp, %pcrel_lo(1b)
    .option pop

    call linux_ss_main

// Restore registers and return to caller
    ld ra, 0(sp)
    ld gp, 8(sp)
    ld tp, 16(sp)
    ld t0, 24(sp)
    ld s0, 32(sp)
    ld s1, 40(sp)
    ld s2, 48(sp)
    ld s3, 56(sp)
    ld s4, 64(sp)
    ld s5, 72(sp)
    ld s6, 80(sp)
    ld s7, 88(sp)
    ld s8, 96(sp)
    ld s9, 104(sp)
    ld s10, 112(sp)
    ld s11, 120(sp)
    addi sp, sp, (16 * 8)
    jr t0
