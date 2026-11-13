/*
 * matrix.c - Matrix digital rain for the text console.
 *
 * The shell only has a character console, so the effect is drawn with ANSI
 * cursor addressing and colours. Each column owns a falling head and a trail
 * that erases itself as it moves down; the whole frame is repainted about
 * twenty times a second.
 */

#include "matrix.h"
#include "lib.h"
#include "timer.h"
#include "uart.h"

#define MATRIX_WIDTH 80
#define MATRIX_HEIGHT 24
#define MATRIX_RAIN_ROWS (MATRIX_HEIGHT - 1)
#define MATRIX_FRAME_MS 50

#define TRAIL_MIN 7
#define TRAIL_MAX 20
#define TICKS_PER_SECOND 100UL

typedef struct {
    int head;  /* row of the leading glyph; negative while still above screen */
    int trail; /* number of rows of glyphs behind the head */
    int speed; /* frames between row advances */
    int phase;
} fall_t;

static unsigned char cells[MATRIX_RAIN_ROWS][MATRIX_WIDTH];
static fall_t falls[MATRIX_WIDTH];
static unsigned long random_state;

static unsigned long random_next(void)
{
    unsigned long value = random_state;

    value ^= value << 13;
    value ^= value >> 7;
    value ^= value << 17;
    random_state = value;
    return value;
}

static unsigned long random_below(unsigned long limit)
{
    return limit ? random_next() % limit : 0;
}

static char random_glyph(void)
{
    static const char glyphs[] =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ@#$%&*+=<>/\\|";

    return glyphs[random_below(sizeof(glyphs) - 1)];
}

static void column_reset(fall_t *fall)
{
    fall->head = -1 - (int)random_below(MATRIX_RAIN_ROWS);
    fall->trail = TRAIL_MIN + (int)random_below(TRAIL_MAX - TRAIL_MIN + 1);
    fall->speed = 1 + (int)random_below(2);
    fall->phase = 0;
}

static void advance_columns(void)
{
    for (int x = 0; x < MATRIX_WIDTH; x++) {
        fall_t *fall = &falls[x];
        int tail;

        if (++fall->phase < fall->speed) {
            continue;
        }
        fall->phase = 0;
        fall->head++;

        if (fall->head >= 0 && fall->head < MATRIX_RAIN_ROWS) {
            cells[fall->head][x] = (unsigned char)random_glyph();
        }

        tail = fall->head - fall->trail;
        if (tail >= 0 && tail < MATRIX_RAIN_ROWS) {
            cells[tail][x] = 0;
        }

        if (tail >= MATRIX_RAIN_ROWS) {
            column_reset(fall);
        }
    }
}

static void render(void)
{
    for (int y = 0; y < MATRIX_RAIN_ROWS; y++) {
        int color = -1;

        uart_printf("\x1b[%d;1H", y + 1);

        for (int x = 0; x < MATRIX_WIDTH; x++) {
            unsigned char glyph = cells[y][x];

            if (!glyph) {
                uart_putc(' ');
                continue;
            }

            /* The head is white, the next couple of glyphs bright green and
               the rest of the trail dark green. Only emit SGR when the band
               changes so a frame stays small on a serial console. */
            int distance = falls[x].head - y;
            int band = distance <= 0 ? 2 : (distance <= 2 ? 1 : 0);

            if (band != color) {
                if (band == 2) {
                    uart_puts("\x1b[1;97m");
                } else if (band == 1) {
                    uart_puts("\x1b[92m");
                } else {
                    uart_puts("\x1b[32m");
                }
                color = band;
            }
            uart_putc((char)glyph);
        }
    }

    uart_puts("\x1b[0m");
}

void matrix_run(unsigned long seconds)
{
    unsigned long deadline = 0;

    random_state = timer_ticks_now() * 2654435761UL + 0x9E3779B97F4A7C15UL;
    if (!random_state) {
        random_state = 0x123456789ABCDEFUL;
    }

    memset(cells, 0, sizeof(cells));
    for (int x = 0; x < MATRIX_WIDTH; x++) {
        column_reset(&falls[x]);
        /* Stagger the initial falls so the screen fills in gradually. */
        falls[x].head -= (int)random_below(MATRIX_RAIN_ROWS * 2);
    }

    uart_puts("\x1b[2J\x1b[H\x1b[?25l");
    uart_puts("\x1b[24;1H\x1b[92m  matrix  |  press any key to stop\x1b[0m");

    if (seconds) {
        deadline = timer_ticks_now() + seconds * TICKS_PER_SECOND;
    }

    for (;;) {
        advance_columns();
        render();

        if (uart_trygetc() >= 0) {
            break;
        }
        if (deadline && timer_ticks_now() >= deadline) {
            break;
        }
        timer_wait_ms(MATRIX_FRAME_MS);
    }

    uart_puts("\x1b[0m\x1b[?25h\x1b[2J\x1b[H");
}
