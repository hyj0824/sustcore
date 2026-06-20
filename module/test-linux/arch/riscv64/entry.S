    .text
    .global _start
    .type _start, @function
    .extern test_linux_main

_start:
    .option push
    .option norelax
1:
    auipc gp, %pcrel_hi(__global_pointer$)
    addi gp, gp, %pcrel_lo(1b)
    .option pop
    j test_linux_main
