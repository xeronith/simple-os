/*
 * kernel.c - boot banner, fault reporting and kmain.
 */

#include "kernel.h"
#include "heap.h"
#include "lib.h"
#include "shell.h"
#include "timer.h"
#include "uart.h"

extern char vectors[];
extern char __image_start[];
extern char __image_end[];
extern char __bss_start[];
extern char __bss_end[];
extern char __kernel_end[];

static const char *const vector_names[16] = {
    "sync (EL1t)",    "irq (EL1t)",     "fiq (EL1t)",     "serror (EL1t)",
    "sync (EL1h)",    "irq (EL1h)",     "fiq (EL1h)",     "serror (EL1h)",
    "sync (EL0 64)",  "irq (EL0 64)",   "fiq (EL0 64)",   "serror (EL0 64)",
    "sync (EL0 32)",  "irq (EL0 32)",   "fiq (EL0 32)",   "serror (EL0 32)",
};

static const char *exception_class(unsigned long ec)
{
    switch (ec) {
    case 0x00: return "unknown reason";
    case 0x01: return "WFI/WFE trapped";
    case 0x03: return "CP15 trapped";
    case 0x07: return "FP/SIMD trapped";
    case 0x0E: return "illegal execution state";
    case 0x15: return "SVC from AArch64";
    case 0x18: return "MSR/MRS trapped";
    case 0x20: return "instruction abort (same EL)";
    case 0x21: return "instruction abort (lower EL)";
    case 0x22: return "PC alignment fault";
    case 0x24: return "data abort (same EL)";
    case 0x25: return "data abort (lower EL)";
    case 0x26: return "SP alignment fault";
    case 0x2C: return "FP exception";
    case 0x3C: return "BRK instruction";
    default:   return "unclassified";
    }
}

static const char *fault_status(unsigned long fsc)
{
    switch (fsc) {
    case 0x04: return "translation fault, level 0";
    case 0x05: return "translation fault, level 1";
    case 0x06: return "translation fault, level 2";
    case 0x07: return "translation fault, level 3";
    case 0x09: return "access flag fault, level 1";
    case 0x0A: return "access flag fault, level 2";
    case 0x0B: return "access flag fault, level 3";
    case 0x0D: return "permission fault, level 1";
    case 0x0E: return "permission fault, level 2";
    case 0x0F: return "permission fault, level 3";
    case 0x10: return "synchronous external abort";
    case 0x21: return "alignment fault";
    default:   return "unknown fault status";
    }
}

void exception_report(unsigned long kind, unsigned long esr, unsigned long far,
                      unsigned long elr, unsigned long spsr)
{
    unsigned long ec = (esr >> 26) & 0x3F;
    unsigned long iss = esr & 0x1FFFFFF;

    uart_printf("\n\n*** KERNEL EXCEPTION ***\n");
    uart_printf("  vector   %s\n", kind < 16 ? vector_names[kind] : "unknown");
    uart_printf("  ESR_EL1  0x%016lx\n", esr);
    uart_printf("  EC       0x%02lx  %s\n", ec, exception_class(ec));
    uart_printf("  ISS      0x%07lx\n", iss);

    if (ec == 0x20 || ec == 0x21 || ec == 0x24 || ec == 0x25) {
        uart_printf("  DFSC     0x%02lx  %s\n", esr & 0x3F, fault_status(esr & 0x3F));
        if (ec == 0x24 || ec == 0x25) {
            uart_printf("  access   %s\n", (esr & (1UL << 6)) ? "write" : "read");
        }
    }

    uart_printf("  FAR_EL1  0x%016lx\n", far);
    uart_printf("  ELR_EL1  0x%016lx\n", elr);
    uart_printf("  SPSR_EL1 0x%016lx\n", spsr);
    uart_printf("\n  fatal: kernel halted. reset the machine to reboot.\n");

    /* The assembly fault path spins in WFI after this returns. */
}

void kmain(void)
{
    uart_init();
    uart_printf("\x1b[2J\x1b[H");

    uart_printf("\n");
    uart_printf("  +----------------------------------------------+\n");
    uart_printf("  |  simple-os 0.1  |  a tiny AArch64 kernel     |\n");
    uart_printf("  +----------------------------------------------+\n");
    uart_printf("\n");

    uart_printf("  [boot] cpu        QEMU virt / Cortex-A57\n");
    uart_printf("  [boot] exceptions vector table at 0x%016lx\n", (unsigned long)vectors);
    uart_printf("  [boot] console    PL011 UART\n");

    timer_init();
    uart_printf("  [boot] timer      GICv2 + generic timer, %lu Hz counter, 100 Hz tick\n",
                timer_frequency());

    heap_init();
    uart_printf("  [boot] heap       %lu MiB bump allocator\n",
                heap_capacity() / (1024UL * 1024UL));

    uart_printf("  [boot] kernel     0x%016lx - 0x%016lx\n",
                (unsigned long)__image_start, (unsigned long)__kernel_end);
    uart_printf("\n  ready. type 'help' for a list of commands.\n\n");

    shell_run();
}
