#include <sustcore/syscall.h>

    .text

    .global sys_write_serial
    .type sys_write_serial, @function
sys_write_serial:
    move $a2, $a1
    move $a1, $a0
    move $a0, $zero
    li.d $a7, SYS_WRITE_SERIAL
    syscall 0
    ret

    .global sys_mem_resize
    .type sys_mem_resize, @function
sys_mem_resize:
    li.d $a7, SYS_MEM_RESIZE
    syscall 0
    ret
