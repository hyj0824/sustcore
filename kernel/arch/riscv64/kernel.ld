/* kernel.ld - 确保内核被加载到 OpenSBI 跳转后的地址 */
ENTRY(_start)
SECTIONS
{
    . = 0x80200000;
    .text : {
        *(.text.entry)
        *(.text .text.*)
    }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .sdata : { *(.sdata .sdata.*) }
    .bss : { *(.bss .bss.*) }
    .comment : { *(.comment .comment.*) }
    .riscv.attributes : { *(.riscv.attributes .riscv.attributes.*) }
    .vector_table ALIGN(4) : { *(.vector_table .vector_table.*) }
}