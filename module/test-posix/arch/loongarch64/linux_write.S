    .text
    .global linux_write
    .type linux_write, @function

    .equ LINUX_SYS_WRITE, 64

linux_write:
    li.w $a7, LINUX_SYS_WRITE
    syscall 0
    ret
