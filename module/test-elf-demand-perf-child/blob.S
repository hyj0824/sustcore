.section .rodata.elf_demand_perf_blob,"a",@progbits
.balign 4096
.global elf_demand_perf_blob
.global elf_demand_perf_blob_end
elf_demand_perf_blob:
.rept 256
    .byte 0x5a
    .zero 4095
.endr
elf_demand_perf_blob_end:
