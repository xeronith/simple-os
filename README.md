# simple-os

A tiny bare-metal AArch64 kernel that boots directly under QEMU and drops you
into a serial monitor. No bootloader, no firmware, no external libc, and no
GNU cross-toolchain.

```
  +----------------------------------------------+
  |  simple-os 0.1  |  a tiny AArch64 kernel     |
  +----------------------------------------------+

  [boot] cpu        QEMU virt / Cortex-A57
  [boot] exceptions vector table at 0x0000000040200800
  [boot] console    PL011 UART
  [boot] timer      GICv2 + generic timer, 62500000 Hz counter, 100 Hz tick
  [boot] heap       16 MiB bump allocator
  [boot] kernel     0x0000000040200000 - 0x00000000402080c0

  ready. type 'help' for a list of commands.

simple-os> info
  machine     QEMU virt, AArch64
  MIDR_EL1    0x00000000411fd070  implementer 0x41  part 0xd07  revision 0
  CurrentEL   EL1
  counter     62500000 Hz, scheduler tick 100 Hz
  vectors     0x0000000040200800
  heap        0 / 16777216 bytes used
```

## Features

- **Boots directly from an ELF.** QEMU's `virt` machine loads the kernel with
  `-kernel`; there is no boot sector, multiboot header, or GRUB stage.
- **Real exception handling.** A 2 KiB-aligned vector table covers all 16
  architectural entries. Synchronous faults print ESR, EC, ISS, FAR, ELR and
  SPSR with decoded fault classes before halting.
- **Interrupts.** GICv2 plus the ARM generic timer, programmed for a 100 Hz
  tick. `sleep` is driven by the timer interrupt.
- **Serial console.** PL011 UART driver at `0x09000000`, with a compact
  `printf` supporting `%s %c %d %u %x %X %p %b`, width and zero padding.
- **Memory management, to a point.** A 16 MiB bump allocator sits above the
  kernel; `alloc` and `heap` expose it from the shell.
- **Interactive monitor.** Line editing, backspace, Ctrl-C, Ctrl-L, memory
  peek/poke, hexdumps, uptime, CPU information, PSCI reboot and more.
- **Toolchain-light build.** Clang emits bare-metal AArch64 objects and a
  small included Python linker produces the final ELF. Only clang, Python,
  make and QEMU are required.

## Requirements

| Tool | Purpose |
| --- | --- |
| `clang` with the AArch64 target | Compiles C and assembly for `aarch64-none-elf` |
| `python3` | Runs the linker and the smoke test |
| `make` | Build orchestration |
| `qemu-system-aarch64` | Emulates the `virt` machine |

Upstream clang builds and the Xcode Command Line Tools both support the
required target. A GNU cross-compiler is not needed.

Typical installation commands:

```sh
# Debian / Ubuntu (qemu-system-arm contains qemu-system-aarch64)
sudo apt install clang make python3 qemu-system-arm

# Fedora
sudo dnf install clang make python3 qemu-system-aarch64

# Arch Linux
sudo pacman -S clang make python qemu-system-aarch64

# macOS
xcode-select --install
brew install qemu
```

On Windows, use WSL2 with the Debian/Ubuntu instructions; QEMU should run
inside WSL.

## Quick start

```sh
git clone https://github.com/xeronith/simple-os.git
cd simple-os
make
make run
```

SSH users can clone `git@github.com:xeronith/simple-os.git` instead.

`make run` starts QEMU in `-nographic` mode. Press **Ctrl-A X** to quit.

Run the scripted boot test without an interactive terminal:

```sh
make test
```

It boots the kernel with a Unix-socket serial port, waits for the shell, runs
`help`, `info`, `uptime`, `heap`, `alloc`, `poke`, `peek`, `sleep` and
`panic`, and verifies the output.

## Shell commands

| Command | Description |
| --- | --- |
| `help` | List commands |
| `info` | CPU, timer, kernel and heap information |
| `uptime` | Time since boot, in ticks and seconds |
| `heap` | Bump-allocator usage |
| `alloc <bytes>` | Allocate memory from the bump heap |
| `mem <addr> [len]` | Hex dump (address and length in hex) |
| `peek <addr>` | Read a 64-bit word |
| `poke <addr> <val>` | Write a 64-bit word |
| `echo <text>` | Print text |
| `sleep <ms>` | Sleep using the timer interrupt |
| `clear` | Clear the screen |
| `panic` | Raise a BRK exception to demonstrate fault reporting |
| `reboot` | Reset the machine through PSCI |
| `halt` | Stop the CPU |

Addresses accepted by `mem`, `peek` and `poke` are physical addresses and are
not validated. Pointing them at device memory can fault; that is what the
exception handler is for.

## How it works

### Boot and memory layout

QEMU jumps to `_start` at `0x40200000`. The first 2 MiB of RAM are left free
for QEMU's device tree and boot data. The boot path masks interrupts, drops
from EL2 to EL1 if necessary, installs the vector table in `VBAR_EL1`, sets up
a 16 KiB stack, clears `.bss`, and calls `kmain`.

```
0x40000000  RAM base (QEMU DTB / boot data)
0x40200000  kernel image and BSS
0x41000000  bump-allocator heap (16 MiB)
0x48000000  end of the default 128 MiB of RAM
```

The image is a single `PT_LOAD` segment. The linker emits section headers too,
so the result can be inspected with `objdump`, `readelf` or a debugger.

### Exceptions and interrupts

`src/exc.S` installs the architectural 16-entry vector table and saves all
general purpose registers around the IRQ handler. Fatal exceptions branch to a
common path that captures `ESR_EL1`, `FAR_EL1`, `ELR_EL1` and `SPSR_EL1` for
the C reporter.

QEMU's `virt` GICv2 is built without security extensions, so it behaves like a
single-security-state GICv1: `GICC_IAR`/`GICC_EOIR` are the acknowledge
registers and the timer uses PPI 30. The driver is in `src/timer.c`.

### The linker

Clang is a cross-compiler by construction: `--target=aarch64-none-elf` produces
ELF objects without needing a separate compiler installation. The remaining
problem is linking them, because not every clang installation ships a matching
bare-metal linker.

`tools/elflink.py` fills that gap. It merges the allocatable sections of the
input objects, applies the relocation types this kernel emits (`ADR_PREL_PG_HI21`,
`ADD_ABS_LO12_NC`, `CALL26`, and friends), and writes a one-segment
`ET_EXEC` ELF. It is intentionally small and reports unsupported relocations
rather than guessing.

## Project layout

```
src/boot.S           entry point, EL2 -> EL1, stack and BSS setup
src/exc.S            vector table, IRQ save/restore, fatal fault path
src/kernel.c         boot banner, fault decoding, kmain
src/uart.c           PL011 driver and printf
src/timer.c          GICv2 and generic timer
src/shell.c          serial monitor
src/heap.c           bump allocator
src/lib.c            memset/memcpy/str* implementations
src/*.h              public interfaces
tools/elflink.py     minimal ELF64/AArch64 linker
tools/smoke-test.py  scripted QEMU boot test
Makefile             build, run and test targets
```

## Limitations

This is a toy kernel, not a production operating system:

- The MMU and caches are off; all addresses are physical.
- One CPU, no SMP.
- No user mode, no system calls.
- No preemptive scheduler or threads (the timer tick is ready for one).
- No block devices, filesystems or networking.
- `mem`, `peek` and `poke` trust the caller completely.

Natural next steps are identity-mapped paging with a higher-half kernel, a
timer-driven scheduler, and a virtio block or framebuffer driver.

## License

Released under the [MIT License](LICENSE).
