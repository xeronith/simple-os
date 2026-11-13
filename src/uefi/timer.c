/*
 * timer.c - UEFI timer services for the shared shell.
 *
 * There is no interrupt-driven clock in the UEFI application yet; Stall()
 * provides sleeps and GetTime() gives a wall-clock uptime.
 */

#include "timer.h"
#include "uefi.h"

volatile unsigned long timer_ticks;

static EFI_TIME start_time;
static int have_start;

static long seconds_of_day(const EFI_TIME *time)
{
    return (long)time->Hour * 3600L + (long)time->Minute * 60L +
           (long)time->Second;
}

void timer_init(void)
{
    if (gRT && gRT->GetTime &&
        gRT->GetTime(&start_time, 0) == EFI_SUCCESS) {
        have_start = 1;
    }
}

void timer_wait_ms(unsigned ms)
{
    if (ms && gBS && gBS->Stall) {
        gBS->Stall((UINTN)ms * 1000U);
    }
}

unsigned long timer_frequency(void)
{
    /* Stall() takes microseconds, so this is the useful resolution. */
    return 1000000UL;
}

unsigned long timer_ticks_now(void)
{
    EFI_TIME now;
    long elapsed;

    if (!have_start || !gRT || !gRT->GetTime ||
        gRT->GetTime(&now, 0) != EFI_SUCCESS) {
        return 0;
    }

    elapsed = seconds_of_day(&now) - seconds_of_day(&start_time);
    if (elapsed < 0) {
        elapsed += 24L * 3600L;
    }

    return (unsigned long)elapsed * 100UL +
           (unsigned long)(now.Nanosecond / 10000000UL);
}
