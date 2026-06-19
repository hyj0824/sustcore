# 权益之计
qemu-args-riscv64 ?= -bios default \
 			 -name "Sustcore-rv64" \
			 -machine virt \
			 -kernel $(path-kernel)

qemu-args-loongarch64 ?= -name "Sustcore-la64" \
			 -machine virt \
			 -kernel $(path-kernel)

qemu-attached-args-riscv64 ?=
qemu-attached-args-loongarch64 ?=
qemu-attached-args ?=

ifeq ($(architecture), riscv64)
	qemu-args := $(qemu-args-riscv64)
	qemu-attached-args += $(qemu-attached-args-riscv64)
endif
ifeq ($(architecture), loongarch64)
	qemu-args := $(qemu-args-loongarch64)
	qemu-attached-args += $(qemu-attached-args-loongarch64)
endif

qemu-memory-args ?= -m size=256m,maxmem=256m

qemu-serial-args ?= -nographic

qemu-rtc-args ?= -rtc base=localtime

qemu-debug-args ?= -s -S

qemu-run-command = $(qemu) $(qemu-memory-args) $(qemu-args) $(qemu-serial-args) $(qemu-rtc-args) $(qemu-attached-args)
qemu-dbg-command = $(qemu-run-command) $(qemu-debug-args)

.PHONY: run-only
run-only:
	$(qemu-run-command)

.PHONY: dbg-only
dbg-only:
	$(qemu-dbg-command)
