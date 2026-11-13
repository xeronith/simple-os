/*
 * console.c - UEFI simple text output/input backend for the shared shell.
 *
 * The shell emits a few ANSI escape sequences for color and screen clearing.
 * UEFI's text console does not interpret those, so they are filtered here and
 * translated into ClearScreen / SetCursorPosition calls.
 */

#include "uefi.h"
#include "uart.h"

#include <stdarg.h>
#include <stdint.h>

static int ansi_state;

static void output(const CHAR16 *text)
{
    if (gST && gST->ConOut) {
        gST->ConOut->OutputString(gST->ConOut, (CHAR16 *)text);
    }
}

static void output_char(char c)
{
    CHAR16 buffer[3];

    if (c == '\n') {
        buffer[0] = '\r';
        buffer[1] = '\n';
        buffer[2] = 0;
    } else {
        buffer[0] = (CHAR16)(unsigned char)c;
        buffer[1] = 0;
    }
    output(buffer);
}

void uart_init(void)
{
    ansi_state = 0;
}

void uart_putc(char c)
{
    if (ansi_state == 0) {
        if (c == 0x1b) {
            ansi_state = 1;
            return;
        }
        output_char(c);
        return;
    }

    if (ansi_state == 1) {
        ansi_state = (c == '[') ? 2 : 0;
        return;
    }

    /* CSI sequence: ignore parameters, act on the final byte. */
    if ((c >= '0' && c <= '9') || c == ';') {
        return;
    }
    if (c == 'J' && gST && gST->ConOut && gST->ConOut->ClearScreen) {
        gST->ConOut->ClearScreen(gST->ConOut);
    } else if (c == 'H' && gST && gST->ConOut && gST->ConOut->SetCursorPosition) {
        gST->ConOut->SetCursorPosition(gST->ConOut, 0, 0);
    }
    ansi_state = 0;
}

void uart_puts(const char *s)
{
    while (*s) {
        uart_putc(*s++);
    }
}

char uart_getc(void)
{
    for (;;) {
        EFI_INPUT_KEY key;

        if (gST && gST->ConIn &&
            gST->ConIn->ReadKeyStroke(gST->ConIn, &key) == EFI_SUCCESS) {
            CHAR16 ch = key.UnicodeChar;
            if ((ch >= 0x20 && ch < 0x7f) || ch == '\r' || ch == '\n' ||
                ch == 0x08 || ch == 0x7f || ch == 0x03 || ch == 0x0c) {
                return (char)ch;
            }
        }

        if (gBS && gBS->Stall) {
            gBS->Stall(1000);
        }
    }
}

int uart_trygetc(void)
{
    EFI_INPUT_KEY key;

    if (gST && gST->ConIn &&
        gST->ConIn->ReadKeyStroke(gST->ConIn, &key) == EFI_SUCCESS) {
        CHAR16 ch = key.UnicodeChar;
        if (ch < 0x80) {
            return (int)ch;
        }
    }
    return -1;
}

static void print_number(unsigned long long value, unsigned base, int upper,
                         int width, char pad, int negative)
{
    static const char lower_digits[] = "0123456789abcdef";
    static const char upper_digits[] = "0123456789ABCDEF";
    const char *digits = upper ? upper_digits : lower_digits;
    char tmp[24];
    int count = 0;

    if (value == 0) {
        tmp[count++] = '0';
    } else {
        while (value) {
            tmp[count++] = digits[value % base];
            value /= base;
        }
    }
    if (negative) {
        tmp[count++] = '-';
    }

    for (int i = count; i < width; i++) {
        uart_putc(pad);
    }
    while (count) {
        uart_putc(tmp[--count]);
    }
}

void uart_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {
            uart_putc(*p);
            continue;
        }

        p++;
        if (!*p) {
            uart_putc('%');
            break;
        }

        char pad = ' ';
        int width = 0;
        int length = 0;

        if (*p == '0') {
            pad = '0';
            p++;
        }
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            p++;
        }
        while (*p == 'l') {
            length++;
            p++;
        }
        if (*p == 'z') {
            length = 1;
            p++;
        }

        switch (*p) {
        case 'd':
        case 'i': {
            long long signed_value = (length > 0) ? va_arg(ap, long)
                                                  : (long long)va_arg(ap, int);
            unsigned long long magnitude;
            int negative = signed_value < 0;
            if (negative) {
                magnitude = (unsigned long long)(-(signed_value + 1)) + 1ULL;
            } else {
                magnitude = (unsigned long long)signed_value;
            }
            print_number(magnitude, 10, 0, width, pad, negative);
            break;
        }
        case 'u':
            if (length > 0) {
                print_number(va_arg(ap, unsigned long), 10, 0, width, pad, 0);
            } else {
                print_number(va_arg(ap, unsigned int), 10, 0, width, pad, 0);
            }
            break;
        case 'x':
        case 'X':
            if (length > 0) {
                print_number(va_arg(ap, unsigned long), 16, *p == 'X', width, pad, 0);
            } else {
                print_number(va_arg(ap, unsigned int), 16, *p == 'X', width, pad, 0);
            }
            break;
        case 'b':
            if (length > 0) {
                print_number(va_arg(ap, unsigned long), 2, 0, width, pad, 0);
            } else {
                print_number(va_arg(ap, unsigned int), 2, 0, width, pad, 0);
            }
            break;
        case 'p': {
            uintptr_t pointer = (uintptr_t)va_arg(ap, void *);
            uart_puts("0x");
            print_number(pointer, 16, 0, 16, '0', 0);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            uart_puts(s ? s : "(null)");
            break;
        }
        case 'c':
            uart_putc((char)va_arg(ap, int));
            break;
        case '%':
            uart_putc('%');
            break;
        default:
            uart_putc('%');
            uart_putc(*p);
            break;
        }
    }

    va_end(ap);
}
