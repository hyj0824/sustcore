    .extern linux_ss_main

    .text
    .global _start
    .global linux_subsystem_entry
    .type _start, @function
    .type linux_subsystem_entry, @function
_start:
linux_subsystem_entry:
    addi.d $sp, $sp, -(13 * 8)
    st.d $ra, $sp, 0
    st.d $tp, $sp, 8
    st.d $t0, $sp, 16
    st.d $fp, $sp, 24
    st.d $s0, $sp, 32
    st.d $s1, $sp, 40
    st.d $s2, $sp, 48
    st.d $s3, $sp, 56
    st.d $s4, $sp, 64
    st.d $s5, $sp, 72
    st.d $s6, $sp, 80
    st.d $s7, $sp, 88
    st.d $s8, $sp, 96

    bl linux_ss_main

    ld.d $ra, $sp, 0
    ld.d $tp, $sp, 8
    ld.d $t0, $sp, 16
    ld.d $fp, $sp, 24
    ld.d $s0, $sp, 32
    ld.d $s1, $sp, 40
    ld.d $s2, $sp, 48
    ld.d $s3, $sp, 56
    ld.d $s4, $sp, 64
    ld.d $s5, $sp, 72
    ld.d $s6, $sp, 80
    ld.d $s7, $sp, 88
    ld.d $s8, $sp, 96
    addi.d $sp, $sp, (13 * 8)
    jr $t0
