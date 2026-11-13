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
EDK2_CODE ?= /opt/homebrew/share/qemu/edk2-aarch64-code.fd
EDK2_VARS ?= /opt/homebrew/share/qemu/edk2-arm-vars.fd
else
QEMU_DISPLAY ?= default
EDK2_CODE ?= $(firstword $(wildcard /usr/share/qemu/edk2-aarch64-code.fd \
                                 /usr/share/AAVMF/AAVMF_CODE.fd \
                                 /usr/share/edk2/aarch64/QEMU_EFI-pflash.raw))
EDK2_VARS ?= $(firstword $(wildcard /usr/share/qemu/edk2-arm-vars.fd \
                                 /usr/share/AAVMF/AAVMF_VARS.fd \
                                 /usr/share/edk2/aarch64/vars-template-pflash.raw))
endif

UEFI_CFLAGS := $(CROSS) -std=c11 -O2 -g -Wall -Wextra \
               -ffreestanding -fno-builtin -fno-common \
               -fno-stack-protector -fno-pic -mcmodel=small \
               -mno-outline-atomics -mgeneral-regs-only \
               -fno-asynchronous-unwind-tables -fno-unwind-tables \
               -fshort-wchar -Isrc -Isrc/uefi

UEFI_OBJS := build/uefi/main.o build/uefi/console.o build/uefi/timer.o \
             build/uefi/heap.o build/uefi/lib.o build/uefi/shell.o

UEFI_IMAGE := build/BOOTAA64.EFI

all: $(ELF)

# Keep the UEFI patterns above the generic build/%.o rules: make uses the
# first matching pattern rule for a target such as build/uefi/main.o.
build/uefi/%.o: src/uefi/%.c
	@mkdir -p build/uefi
	$(CC) $(UEFI_CFLAGS) -c $< -o $@

build/uefi/lib.o: src/lib.c
	@mkdir -p build/uefi
	$(CC) $(UEFI_CFLAGS) -c $< -o $@

build/uefi/shell.o: src/shell.c
	@mkdir -p build/uefi
	$(CC) $(UEFI_CFLAGS) -c $< -o $@

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

efi: $(UEFI_IMAGE)

$(UEFI_IMAGE): $(UEFI_OBJS) tools/peify.py
	@mkdir -p build
	python3 tools/peify.py -o $@ -e efi_main $(UEFI_OBJS)

# The first UEFI boot lands in EDK2's boot manager; press Enter once. The
# writable vars file keeps that choice, so later runs boot automatically.
run-efi: $(UEFI_IMAGE)
	@test -f $(EDK2_CODE) || { echo "EDK2 firmware not found: $(EDK2_CODE)"; exit 1; }
	@mkdir -p build/esp/EFI/BOOT
	cp $(UEFI_IMAGE) build/esp/EFI/BOOT/BOOTAA64.EFI
	@test -f build/efi-vars.fd || cp $(EDK2_VARS) build/efi-vars.fd
	qemu-system-aarch64 -M virt,gic-version=2 -cpu cortex-a57 -m 512M \
		-drive if=pflash,format=raw,unit=0,readonly=on,file=$(EDK2_CODE) \
		-drive if=pflash,format=raw,unit=1,file=build/efi-vars.fd \
		-drive if=none,id=esp,file=fat:rw:build/esp,format=raw \
		-device virtio-blk-pci,drive=esp \
		-display $(QEMU_DISPLAY) -serial vc -monitor none \
		-name "simple-os (UEFI)"

run-efi-headless: $(UEFI_IMAGE)
	@test -f $(EDK2_CODE) || { echo "EDK2 firmware not found: $(EDK2_CODE)"; exit 1; }
	@mkdir -p build/esp/EFI/BOOT
	cp $(UEFI_IMAGE) build/esp/EFI/BOOT/BOOTAA64.EFI
	@test -f build/efi-vars.fd || cp $(EDK2_VARS) build/efi-vars.fd
	qemu-system-aarch64 -M virt,gic-version=2 -cpu cortex-a57 -m 512M \
		-drive if=pflash,format=raw,unit=0,readonly=on,file=$(EDK2_CODE) \
		-drive if=pflash,format=raw,unit=1,file=build/efi-vars.fd \
		-drive if=none,id=esp,file=fat:rw:build/esp,format=raw \
		-device virtio-blk-pci,drive=esp \
		-nographic

test: $(ELF)
	python3 tools/smoke-test.py

clean:
	rm -rf build

.PHONY: all run run-headless efi run-efi run-efi-headless test clean
