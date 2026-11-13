/*
 * uart.c - PL011 UART console and a small printf implementation.
 *
 * The QEMU "virt" machine exposes UART0 at 0x09000000. With -nographic this
 * is wired to the terminal's stdin/stdout.
 */

#include "uart.h"
#include "lib.h"

#include <stdarg.h>
#include <stdint.h>

#define UART0_BASE 0x09000000UL

#define UART_DR   (*(volatile uint32_t *)(UART0_BASE + 0x00))
#define UART_FR   (*(volatile uint32_t *)(UART0_BASE + 0x18))
#define UART_IBRD (*(volatile uint32_t *)(UART0_BASE + 0x24))
#define UART_FBRD (*(volatile uint32_t *)(UART0_BASE + 0x28))
#define UART_LCRH (*(volatile uint32_t *)(UART0_BASE + 0x2C))
#define UART_CR   (*(volatile uint32_t *)(UART0_BASE + 0x30))
#define UART_ICR  (*(volatile uint32_t *)(UART0_BASE + 0x44))

#define UART_FR_TXFF (1u << 5)
#define UART_FR_RXFE (1u << 4)

void uart_init(void)
{
    UART_CR = 0;

    /* 115200 8n1 assuming the usual 24 MHz reference. QEMU ignores these. */
    UART_IBRD = 13;
    UART_FBRD = 1;

    /* 8-bit words, FIFOs enabled. */
    UART_LCRH = (3u << 5) | (1u << 4);

    UART_ICR = 0x7FF;
    UART_CR = (1u << 0) | (1u << 8) | (1u << 9); /* UARTEN | TXE | RXE */
}

void uart_putc(char c)
{
    while (UART_FR & UART_FR_TXFF) {
        /* wait for room in the transmit FIFO */
    }
    UART_DR = (uint32_t)(unsigned char)c;
}

void uart_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') {
            uart_putc('\r');
        }
        uart_putc(*s++);
    }
}

char uart_getc(void)
{
    while (UART_FR & UART_FR_RXFE) {
        /* wait for a character */
    }
    return (char)(UART_DR & 0xFF);
}

int uart_trygetc(void)
{
    if (UART_FR & UART_FR_RXFE) {
        return -1;
    }
    return (int)(UART_DR & 0xFF);
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
            if (*p == '\n') {
                uart_putc('\r');
            }
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
