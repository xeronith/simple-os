/*
 * shell.c - a small line-oriented monitor on the serial console.
 */

#include "shell.h"
#include "heap.h"
#include "lib.h"
#include "matrix.h"
#include "platform.h"
#include "timer.h"
#include "uart.h"

#include <stdint.h>

#define LINE_MAX 160
#define TIMER_HZ 100UL

static char line_buffer[LINE_MAX];

extern char vectors[];
extern char __image_start[];
extern char __kernel_end[];
extern char __bss_start[];
extern char __bss_end[];

static void prompt(void)
{
    uart_puts("\x1b[1;32msimple-os>\x1b[0m ");
}

static void read_line(char *buffer, int capacity)
{
    int length = 0;
    buffer[0] = '\0';

    for (;;) {
        char c = uart_getc();

        if (c == '\r' || c == '\n') {
            uart_puts("\n");
            buffer[length] = '\0';
            return;
        }
        if (c == 0x7F || c == 0x08) {
            if (length > 0) {
                length--;
                uart_puts("\b \b");
            }
            continue;
        }
        if (c == 0x03) { /* Ctrl-C */
            uart_puts("^C\n");
            length = 0;
            buffer[0] = '\0';
            return;
        }
        if (c == 0x0C) { /* Ctrl-L */
            uart_puts("\x1b[2J\x1b[H");
            prompt();
            for (int i = 0; i < length; i++) {
                uart_putc(buffer[i]);
            }
            continue;
        }
        if (c >= 32 && c < 127 && length < capacity - 1) {
            buffer[length++] = c;
            uart_putc(c);
        }
    }
}

static char *next_token(char **cursor)
{
    char *p = *cursor;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (!*p) {
        *cursor = p;
        return 0;
    }

    char *token = p;
    while (*p && *p != ' ' && *p != '\t') {
        p++;
    }
    if (*p) {
        *p++ = '\0';
    }
    *cursor = p;
    return token;
}

static int parse_hex(const char *text, unsigned long *out)
{
    if (!text || !*text) {
        return 0;
    }
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        text += 2;
    }

    unsigned long value = 0;
    int any = 0;
    while (*text) {
        int digit;
        if (*text >= '0' && *text <= '9') {
            digit = *text - '0';
        } else if (*text >= 'a' && *text <= 'f') {
            digit = *text - 'a' + 10;
        } else if (*text >= 'A' && *text <= 'F') {
            digit = *text - 'A' + 10;
        } else {
            return 0;
        }
        value = (value << 4) | (unsigned long)digit;
        any = 1;
        text++;
    }

    *out = value;
    return any;
}

static int parse_dec(const char *text, unsigned long *out)
{
    if (!text || !*text) {
        return 0;
    }

    unsigned long value = 0;
    while (*text) {
        if (*text < '0' || *text > '9') {
            return 0;
        }
        value = value * 10 + (unsigned long)(*text - '0');
        text++;
    }

    *out = value;
    return 1;
}

static void hexdump(unsigned long address, unsigned long length)
{
    for (unsigned long offset = 0; offset < length; offset += 16) {
        uart_printf("  %016lx  ", address + offset);

        for (int i = 0; i < 16; i++) {
            if (offset + (unsigned long)i < length) {
                unsigned char byte =
                    *(volatile unsigned char *)(address + offset + (unsigned long)i);
                uart_printf("%02x ", byte);
            } else {
                uart_puts("   ");
            }
            if (i == 7) {
                uart_putc(' ');
            }
        }

        uart_putc(' ');
        for (int i = 0; i < 16; i++) {
            if (offset + (unsigned long)i < length) {
                unsigned char byte =
                    *(volatile unsigned char *)(address + offset + (unsigned long)i);
                uart_putc((byte >= 32 && byte < 127) ? (char)byte : '.');
            } else {
                uart_putc(' ');
            }
        }
        uart_puts("\n");
    }
}

static void command_help(void)
{
    uart_puts("  commands:\n"
              "    help                this text\n"
              "    info                cpu, timer and kernel information\n"
              "    uptime              time since boot\n"
              "    heap                bump-allocator usage\n"
              "    alloc <bytes>       allocate memory\n"
              "    mem <addr> [len]    hex dump (address and length in hex)\n"
              "    peek <addr>         read a 64-bit word\n"
              "    poke <addr> <val>   write a 64-bit word\n"
              "    echo <text>         print text\n"
              "    sleep <ms>          sleep using the platform timer\n"
              "    clear               clear the screen\n"
              "    matrix [seconds]    Matrix digital rain (any key stops)\n"
              "    panic               trigger a kernel panic (demo)\n"
              "    reboot              reset the machine\n"
              "    halt                stop the CPU\n");
}

static void command_info(void)
{
    unsigned long midr, mpidr, currentel, frequency;

    __asm__ volatile("mrs %0, midr_el1" : "=r"(midr));
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(currentel));
    frequency = timer_frequency();

    uart_printf("  machine     %s\n", platform_machine_name());
    uart_printf("  MIDR_EL1    0x%016lx  implementer 0x%02lx  part 0x%03lx  revision %lu\n",
                midr, (midr >> 24) & 0xFF, (midr >> 4) & 0xFFF, midr & 0xF);
    uart_printf("  MPIDR_EL1   0x%016lx\n", mpidr);
    uart_printf("  CurrentEL   EL%lu\n", (currentel >> 2) & 3);
    uart_printf("  timer       %lu Hz, scheduler tick %lu Hz\n", frequency, TIMER_HZ);
    uart_printf("  kernel      0x%016lx - 0x%016lx\n",
                (unsigned long)__image_start, (unsigned long)__kernel_end);
    uart_printf("  bss         0x%016lx - 0x%016lx\n",
                (unsigned long)__bss_start, (unsigned long)__bss_end);
    uart_printf("  vectors     0x%016lx\n", (unsigned long)vectors);
    uart_printf("  heap        %lu / %lu bytes used\n", heap_used(), heap_capacity());
}

static void command_uptime(void)
{
    unsigned long ticks = timer_ticks_now();
    unsigned long milliseconds = ticks * (1000UL / TIMER_HZ);
    unsigned long seconds = milliseconds / 1000UL;
    unsigned long centiseconds = (milliseconds % 1000UL) / 10UL;

    uart_printf("  uptime %lu.%02lu s (%lu ticks)\n", seconds, centiseconds, ticks);
}

static void command_heap(void)
{
    unsigned long used = heap_used();
    unsigned long capacity = heap_capacity();
    unsigned long percent = capacity ? (used * 100UL) / capacity : 0;

    uart_printf("  heap %lu / %lu bytes used (%lu%%), %lu bytes free\n",
                used, capacity, percent, capacity - used);
}

static void command_alloc(char *args)
{
    unsigned long bytes;

    if (!parse_dec(args, &bytes) || bytes == 0) {
        uart_puts("  usage: alloc <bytes>\n");
        return;
    }
    if (bytes > 1024UL * 1024UL) {
        bytes = 1024UL * 1024UL;
        uart_puts("  (clamped to 1 MiB)\n");
    }

    void *block = kmalloc(bytes);
    if (!block) {
        uart_puts("  out of memory\n");
        return;
    }
    uart_printf("  allocated %lu bytes at 0x%016lx\n", bytes, (unsigned long)block);
}

static void command_mem(char *args)
{
    char *cursor = args;
    char *address_text = next_token(&cursor);
    char *length_text = next_token(&cursor);
    unsigned long address;
    unsigned long length = 64;

    if (!address_text || !parse_hex(address_text, &address)) {
        uart_puts("  usage: mem <hex-addr> [hex-len]\n");
        return;
    }
    if (length_text) {
        if (!parse_hex(length_text, &length)) {
            uart_puts("  bad length\n");
            return;
        }
    }
    if (length > 512) {
        length = 512;
        uart_puts("  (length clamped to 0x200)\n");
    }
    hexdump(address, length);
}

static void command_peek(char *args)
{
    unsigned long address;
    if (!parse_hex(args, &address)) {
        uart_puts("  usage: peek <hex-addr>\n");
        return;
    }
    uart_printf("  [0x%016lx] = 0x%016lx\n", address,
                *(volatile unsigned long *)address);
}

static void command_poke(char *args)
{
    char *cursor = args;
    char *address_text = next_token(&cursor);
    char *value_text = next_token(&cursor);
    unsigned long address, value;

    if (!address_text || !value_text || !parse_hex(address_text, &address) ||
        !parse_hex(value_text, &value)) {
        uart_puts("  usage: poke <hex-addr> <hex-value>\n");
        return;
    }

    *(volatile unsigned long *)address = value;
    uart_printf("  [0x%016lx] <- 0x%016lx\n", address, value);
}

static void command_sleep(char *args)
{
    unsigned long milliseconds;
    if (!parse_dec(args, &milliseconds)) {
        uart_puts("  usage: sleep <ms>\n");
        return;
    }
    if (milliseconds > 60000) {
        milliseconds = 60000;
    }
    timer_wait_ms((unsigned)milliseconds);
}

static void command_matrix(char *args)
{
    char *cursor = args;
    char *seconds_text = next_token(&cursor);
    unsigned long seconds = 0;

    if (seconds_text && !parse_dec(seconds_text, &seconds)) {
        uart_puts("  usage: matrix [seconds]\n");
        return;
    }
    if (seconds > 3600) {
        seconds = 3600;
    }
    matrix_run(seconds);
}

static void command_panic(void)
{
    uart_puts("  triggering a kernel panic...\n");
    platform_panic();
}

static void command_reboot(void)
{
    uart_puts("  requesting system reset...\n");
    platform_reboot();

    uart_puts("  reset returned; halting.\n");
    platform_halt();
}

static void command_halt(void)
{
    uart_puts("  halted.\n");
    platform_halt();
}

static void execute(char *text)
{
    char *cursor = text;
    char *command = next_token(&cursor);

    if (!command) {
        return;
    }

    if (strcmp(command, "help") == 0) {
        command_help();
    } else if (strcmp(command, "info") == 0) {
        command_info();
    } else if (strcmp(command, "uptime") == 0) {
        command_uptime();
    } else if (strcmp(command, "heap") == 0) {
        command_heap();
    } else if (strcmp(command, "alloc") == 0) {
        command_alloc(cursor);
    } else if (strcmp(command, "mem") == 0) {
        command_mem(cursor);
    } else if (strcmp(command, "peek") == 0) {
        command_peek(cursor);
    } else if (strcmp(command, "poke") == 0) {
        command_poke(cursor);
    } else if (strcmp(command, "echo") == 0) {
        uart_printf("  %s\n", cursor);
    } else if (strcmp(command, "sleep") == 0) {
        command_sleep(cursor);
    } else if (strcmp(command, "clear") == 0) {
        uart_puts("\x1b[2J\x1b[H");
    } else if (strcmp(command, "matrix") == 0) {
        command_matrix(cursor);
    } else if (strcmp(command, "panic") == 0) {
        command_panic();
    } else if (strcmp(command, "reboot") == 0) {
        command_reboot();
    } else if (strcmp(command, "halt") == 0) {
        command_halt();
    } else {
        uart_printf("  unknown command: %s (try 'help')\n", command);
    }
}

void shell_run(void)
{
    for (;;) {
        prompt();
        read_line(line_buffer, LINE_MAX);
        execute(line_buffer);
    }
}
