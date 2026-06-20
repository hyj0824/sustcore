    .text
    .global _start
    .type _start, @function
    .extern test_posix_main

_start:
    b test_posix_main
