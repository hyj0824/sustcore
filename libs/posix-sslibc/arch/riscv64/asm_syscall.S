#include <sustcore/syscall.h>

    .text
    .global sys_write_serial
    .type   sys_write_serial, @function
sys_write_serial:
    mv a2, a1
    mv a1, a0
    li a0, 0
    li a7, SYS_WRITE_SERIAL
    ecall
    ret
