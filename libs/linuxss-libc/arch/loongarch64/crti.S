    .section .init, "ax", @progbits
    .balign 4
    .global _init
    .type _init, @function
_init:

    .section .fini, "ax", @progbits
    .balign 4
    .global _fini
    .type _fini, @function
_fini:
