    .text
    .global _start
    .type _start, @function
    .extern test_linux_main

_start:
    b test_linux_main
