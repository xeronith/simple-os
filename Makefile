# simple-os build
#
# Clang is used as a cross-compiler for bare-metal AArch64. It emits ELF
# objects for aarch64-none-elf; tools/elflink.py turns them into a single ELF
# executable that QEMU can load with -kernel.

CC       := clang
CROSS    := --target=aarch64-none-elf
CFLAGS   := $(CROSS) -std=c11 -O2 -g -Wall -Wextra \
            -ffreestanding -fno-builtin -fno-common \
            -fno-stack-protector -fno-pic -mcmodel=small \
            -mno-outline-atomics -mgeneral-regs-only \
            -fno-asynchronous-unwind-tables -fno-unwind-tables \
            -Isrc
ASFLAGS  := $(CROSS) -g -Isrc

OBJS := build/boot.o build/exc.o build/lib.o build/uart.o \
        build/timer.o build/heap.o build/shell.o build/kernel.o

ELF := build/kernel.elf

# `make run` opens a native QEMU window with the serial console. On macOS the
# backend is cocoa; elsewhere QEMU's default display backend is used. Override
# with `make run QEMU_DISPLAY=gtk` (or sdl) when needed.
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
QEMU_DISPLAY ?= cocoa
else
QEMU_DISPLAY ?= default
endif

all: $(ELF)

build/%.o: src/%.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: src/%.S
	@mkdir -p build
	$(CC) $(ASFLAGS) -c $< -o $@

$(ELF): $(OBJS) tools/elflink.py
	@mkdir -p build
	python3 tools/elflink.py -o $@ -b 0x40200000 -e _start $(OBJS)

run: $(ELF)
	qemu-system-aarch64 -M virt,gic-version=2 -cpu cortex-a57 -m 128M \
		-display $(QEMU_DISPLAY) -serial vc -monitor none \
		-name "simple-os" -kernel $(ELF)

run-headless: $(ELF)
	qemu-system-aarch64 -M virt,gic-version=2 -cpu cortex-a57 -m 128M \
		-nographic -kernel $(ELF)

test: $(ELF)
	python3 tools/smoke-test.py

clean:
	rm -rf build

.PHONY: all run run-headless test clean
