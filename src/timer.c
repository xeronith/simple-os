/*
 * timer.c - GICv2 + ARM generic timer.
 *
 * QEMU's virt machine gives us GICv2 at 0x08000000 (distributor) and
 * 0x08010000 (CPU interface). The EL1 physical timer raises PPI 30; we program
 * it for a 100 Hz tick and enable IRQs.
 */

#include "timer.h"
#include "kernel.h"

#include <stdint.h>

#define GICD_BASE 0x08000000UL
#define GICC_BASE 0x08010000UL

#define GICD_CTLR       (*(volatile uint32_t *)(GICD_BASE + 0x000))
#define GICD_IGROUPR0   (*(volatile uint32_t *)(GICD_BASE + 0x080))
#define GICD_ISENABLER0 (*(volatile uint32_t *)(GICD_BASE + 0x100))
#define GICD_IPRIORITY  ((volatile uint8_t *)(GICD_BASE + 0x400))

#define GICC_CTLR (*(volatile uint32_t *)(GICC_BASE + 0x000))
#define GICC_PMR  (*(volatile uint32_t *)(GICC_BASE + 0x004))
#define GICC_IAR  (*(volatile uint32_t *)(GICC_BASE + 0x00C))
#define GICC_EOIR (*(volatile uint32_t *)(GICC_BASE + 0x010))

#define TIMER_IRQ   30u
#define TIMER_HZ    100u

volatile unsigned long timer_ticks;

static unsigned long timer_period;

static inline unsigned long read_cntfrq(void)
{
    unsigned long value;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(value));
    return value;
}

static inline void write_cntp_tval(unsigned long value)
{
    __asm__ volatile("msr cntp_tval_el0, %0" ::"r"(value));
}

static inline void write_cntp_ctl(unsigned long value)
{
    __asm__ volatile("msr cntp_ctl_el0, %0" ::"r"(value));
}

void timer_init(void)
{
    unsigned long frequency = read_cntfrq();
    if (frequency == 0) {
        frequency = 62500000UL; /* QEMU's usual counter frequency */
    }
    timer_period = frequency / TIMER_HZ;

    /*
     * virt's GICv2 is built without security extensions (GICD_TYPER bit 10 is
     * clear). It therefore behaves like the single-security-state GICv1: all
     * interrupts are Group 0 and are acknowledged with GICC_IAR / GICC_EOIR.
     */
    GICD_CTLR = 1; /* Enable */
    GICD_IGROUPR0 = 0;
    GICD_IPRIORITY[TIMER_IRQ] = 0xA0;
    GICD_ISENABLER0 = 1u << TIMER_IRQ;

    GICC_PMR = 0xFF;
    GICC_CTLR = 1; /* Enable */

    write_cntp_tval(timer_period);
    write_cntp_ctl(1);

    /* Unmask IRQs. */
    __asm__ volatile("msr daifclr, #2" ::: "memory");
}

void irq_handler(void)
{
    uint32_t iar = GICC_IAR;
    uint32_t id = iar & 0x3FF;

    if (id == TIMER_IRQ) {
        timer_ticks++;
        write_cntp_tval(timer_period);
    }

    if (id < 1020) {
        GICC_EOIR = iar;
    }
}

void timer_wait_ms(unsigned ms)
{
    if (ms == 0) {
        return;
    }

    unsigned long want = (ms + (1000 / TIMER_HZ) - 1) / (1000 / TIMER_HZ);
    unsigned long start = timer_ticks;
    while ((timer_ticks - start) < want) {
        __asm__ volatile("wfi");
    }
}

unsigned long timer_frequency(void)
{
    return read_cntfrq();
}
