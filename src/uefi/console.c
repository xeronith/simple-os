/*
 * console.c - UEFI simple text output/input backend for the shared shell.
 *
 * The shell emits ANSI escape sequences for colour, screen clearing and
 * cursor addressing. UEFI's text console does not interpret those, so they
 * are parsed here and translated into SetAttribute / ClearScreen /
 * SetCursorPosition calls.
 */

#include "uefi.h"
#include "uart.h"

#include <stdarg.h>
#include <stdint.h>

#define ANSI_PARAMS 8

/* UEFI text attributes: foreground in the low nibble, background high. */
#define EFI_BLACK       0x00
#define EFI_BLUE        0x01
#define EFI_GREEN       0x02
#define EFI_CYAN        0x03
#define EFI_RED         0x04
#define EFI_MAGENTA     0x05
#define EFI_BROWN       0x06
#define EFI_LIGHTGRAY   0x07
#define EFI_DARKGRAY    0x08
#define EFI_LIGHTBLUE   0x09
#define EFI_LIGHTGREEN  0x0A
#define EFI_LIGHTCYAN   0x0B
#define EFI_LIGHTRED    0x0C
#define EFI_LIGHTMAGENTA 0x0D
#define EFI_YELLOW      0x0E
#define EFI_WHITE       0x0F

static int ansi_state;
static int ansi_params[ANSI_PARAMS];
static int ansi_count;
static int ansi_value;
static int ansi_have_digit;
static int ansi_private;

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

static void set_attribute(unsigned int attribute)
{
    if (gST && gST->ConOut && gST->ConOut->SetAttribute) {
        gST->ConOut->SetAttribute(gST->ConOut, (UINTN)attribute);
    }
}

static void set_cursor(int row, int column)
{
    if (row < 0) {
        row = 0;
    }
    if (column < 0) {
        column = 0;
    }
    if (gST && gST->ConOut && gST->ConOut->SetCursorPosition) {
        /* UEFI takes (Column, Row); the shell's ANSI coordinates are (row, col). */
        gST->ConOut->SetCursorPosition(gST->ConOut, (UINTN)column, (UINTN)row);
    }
}

static void ansi_begin(void)
{
    ansi_count = 0;
    ansi_value = 0;
    ansi_have_digit = 0;
    ansi_private = 0;
}

static void ansi_push_param(void)
{
    if (ansi_count < ANSI_PARAMS) {
        ansi_params[ansi_count++] = ansi_have_digit ? ansi_value : 0;
    }
    ansi_value = 0;
    ansi_have_digit = 0;
}

static int ansi_param(int index, int fallback)
{
    if (index >= ansi_count || ansi_params[index] <= 0) {
        return fallback;
    }
    return ansi_params[index];
}

static void ansi_select_graphic_rendition(void)
{
    /* ANSI colour index -> UEFI attribute, with a black background. */
    static const unsigned char palette[16] = {
        EFI_BLACK,    EFI_RED,     EFI_GREEN,     EFI_BROWN,
        EFI_BLUE,     EFI_MAGENTA, EFI_CYAN,      EFI_LIGHTGRAY,
        EFI_DARKGRAY, EFI_LIGHTRED, EFI_LIGHTGREEN, EFI_YELLOW,
        EFI_LIGHTBLUE, EFI_LIGHTMAGENTA, EFI_LIGHTCYAN, EFI_WHITE,
    };
    int foreground = 7;
    int bold = 0;

    if (ansi_count == 0) {
        ansi_push_param(); /* ESC[m means reset */
    }

    for (int i = 0; i < ansi_count; i++) {
        int code = ansi_params[i];

        if (code == 0) {
            foreground = 7;
            bold = 0;
        } else if (code == 1) {
            bold = 1;
        } else if (code == 22) {
            bold = 0;
        } else if (code >= 30 && code <= 37) {
            foreground = code - 30;
        } else if (code == 39) {
            foreground = 7;
        } else if (code >= 90 && code <= 97) {
            foreground = code - 90 + 8;
        }
    }

    if (bold && foreground < 8) {
        foreground += 8;
    }
    set_attribute(palette[foreground & 15]);
}

void uart_init(void)
{
    ansi_state = 0;
    ansi_begin();
    set_attribute(EFI_LIGHTGRAY | (EFI_BLACK << 4));
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
        if (ansi_state == 2) {
            ansi_begin();
        }
        return;
    }

    /* CSI sequence: collect decimal parameters, then act on the final byte. */
    if (c >= '0' && c <= '9') {
        ansi_value = ansi_value * 10 + (c - '0');
        ansi_have_digit = 1;
        return;
    }
    if (c == ';') {
        ansi_push_param();
        return;
    }
    if (c == '?' || c == '>' || c == '<' || c == '=') {
        ansi_private = 1;
        return;
    }
    if (c < 0x40 || c > 0x7e) {
        return;
    }

    ansi_push_param();
    if (!ansi_private) {
        if (c == 'J' && gST && gST->ConOut && gST->ConOut->ClearScreen) {
            gST->ConOut->ClearScreen(gST->ConOut);
        } else if ((c == 'H' || c == 'f')) {
            set_cursor(ansi_param(0, 1) - 1, ansi_param(1, 1) - 1);
        } else if (c == 'm') {
            ansi_select_graphic_rendition();
        }
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
