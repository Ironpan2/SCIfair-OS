/*
 * timer.c - keeping time.
 *
 * Two clocks are used:
 *  1. The PIT chip: we program it to interrupt us 1000 times a second, so
 *     "ticks" counts milliseconds since boot.
 *  2. The CPU's Time Stamp Counter (TSC): counts every CPU clock cycle, so it
 *     is MUCH more precise. We measure how many cycles happen per millisecond
 *     at boot ("calibration") so we can turn cycles into real time.
 *
 * The experiment uses the TSC for its measurements.
 */
#include "kernel.h"

#define PIT_HZ     1193182u    /* the PIT's input clock frequency */
#define TICK_HZ    1000u
#define PIT_DIV    (PIT_HZ / TICK_HZ)

static volatile uint32_t ticks;
uint64_t tsc_per_ms = 1000000;   /* replaced by tsc_calibrate() */

static void timer_irq(regs_t *r)
{
    (void)r;
    ticks++;
}

void timer_init(void)
{
    outb(0x43, 0x36);                          /* channel 0, square wave */
    outb(0x40, (uint8_t)(PIT_DIV & 0xFF));
    outb(0x40, (uint8_t)(PIT_DIV >> 8));
    irq_install(0, timer_irq);
}

uint32_t timer_ticks(void) { return ticks; }

void sleep_ms(uint32_t ms)
{
    uint32_t start = ticks;
    while (ticks - start < ms)
        cpu_hlt();
}

void tsc_calibrate(void)
{
    const uint32_t N = 200;                    /* measure over 200 ticks */
    uint32_t t = ticks;
    while (ticks == t)                         /* line up with a tick edge */
        cpu_hlt();
    uint64_t a = rdtsc();
    t = ticks;
    while (ticks - t < N)
        cpu_hlt();
    uint64_t b = rdtsc();

    /* One tick is really PIT_DIV / PIT_HZ seconds (0.99985 ms), so:
       cycles per ms = cycles * PIT_HZ / (N * PIT_DIV * 1000). */
    uint64_t per_ms = (b - a) * PIT_HZ / ((uint64_t)N * PIT_DIV * 1000u);
    tsc_per_ms = per_ms ? per_ms : 1;
}

/* Wait WITHOUT letting the CPU rest. Modern CPUs slow down to save power
   when they're idle and take a little while to speed back up, so we keep
   the CPU busy right before a measurement to make every run start at the
   same full speed. */
void busy_wait_ms(uint32_t ms)
{
    uint32_t start = ticks;
    while (ticks - start < ms)
        __asm__ volatile ("pause");
}

uint64_t cycles_to_us(uint64_t cycles)
{
    return cycles * 1000u / tsc_per_ms;
}
