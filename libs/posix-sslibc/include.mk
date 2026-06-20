sources += syscall.cpp

riscv64-sources += arch/riscv64/crt0.S arch/riscv64/crti.S \
	arch/riscv64/crtn.S arch/riscv64/asm_syscall.S
