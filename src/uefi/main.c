/*
 * main.c - UEFI AArch64 entry point for simple-os.
 *
 * This is a PE/COFF EFI application. UEFI loads it from the EFI system
 * partition, calls efi_main, and the shared shell runs on top of the firmware's
 * text console, timer and reset services.
 */

#include "uefi.h"
#include "heap.h"
#include "platform.h"
#include "shell.h"
#include "timer.h"
#include "uart.h"

EFI_SYSTEM_TABLE *gST;
EFI_BOOT_SERVICES *gBS;
EFI_RUNTIME_SERVICES *gRT;

/* The shared info command prints a vector-table address. UEFI applications do
   not install one, but keeping a symbol avoids platform-specific shell code. */
char vectors[1];

/* Force at least one absolute pointer into the image. This gives the PE loader
   a valid base-relocation table even though the rest of the code is entirely
   PC-relative. */
void *reloc_anchor = &reloc_anchor;

const char *platform_machine_name(void)
{
    return "UEFI firmware (AArch64)";
}

void platform_reboot(void)
{
    if (gRT && gRT->ResetSystem) {
        gRT->ResetSystem(EFI_RESET_COLD, EFI_SUCCESS, 0, 0);
    }
}

void platform_panic(void)
{
    uart_puts("\n  panic requested; halting.\n");
    platform_halt();
}

void platform_halt(void)
{
    for (;;) {
        if (gBS && gBS->Stall) {
            gBS->Stall(1000000);
        }
    }
}

EFI_STATUS efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table)
{
    (void)image_handle;

    gST = system_table;
    gBS = system_table->BootServices;
    gRT = system_table->RuntimeServices;

    if (gBS && gBS->SetWatchdogTimer) {
        gBS->SetWatchdogTimer(0, 0, 0, 0);
    }

    uart_init();
    if (gST->ConOut && gST->ConOut->ClearScreen) {
        gST->ConOut->ClearScreen(gST->ConOut);
    }

    timer_init();
    heap_init();

    uart_puts("\n");
    uart_puts("  +----------------------------------------------+\n");
    uart_puts("  |  simple-os 0.1  |  UEFI AArch64 toy OS       |\n");
    uart_puts("  +----------------------------------------------+\n");
    uart_puts("\n");
    uart_puts("  [boot] firmware   UEFI\n");
    uart_puts("  [boot] console    EFI simple text output\n");
    uart_puts("  [boot] heap       1 MiB static bump allocator\n");
    uart_puts("\n  ready. type 'help' for a list of commands.\n\n");

    shell_run();
    return EFI_SUCCESS;
}
