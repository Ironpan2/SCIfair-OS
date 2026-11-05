/*
 * pi.c - the three pi programs used in the experiment.
 *
 * All three run EXACTLY the same calculation (pi_spigot in pi_core.c). The
 * only thing that changes is what happens each time a digit is ready:
 *
 *   MODE_BASE  "No display": the digit is only saved in memory. This is the
 *              control group - the pure cost of the maths.
 *   MODE_TEXT  "Text":       the digit is also printed to the text console.
 *   MODE_GUI   "GUI":        the digit is drawn in a graphical window with a
 *              progress bar, progress ring and status text, and the screen
 *              is redrawn.
 *
 * So:  (time for TEXT) - (time for BASE) = cost of showing text
 *      (time for GUI)  - (time for BASE) = cost of the graphical interface
 *
 * Before every run the CPU is kept busy for 250 ms ("settling") so that power
 * saving can't make one run start slower than another.
 *
 * Time is measured with the CPU's cycle counter, from just before the first
 * digit is calculated until just after the last one is shown. Switching the
 * screen into graphics mode is NOT counted (it happens before the timer
 * starts), but drawing the window IS counted, because that's part of what a
 * GUI program has to do.
 */
#include "kernel.h"
#include "pi_core.h"

#define PI_SETTLE_MS 250

static uint32_t work[PI_WORK_CELLS(PI_MAX_DIGITS)];
static char     digits[PI_MAX_DIGITS + 1];
static uint32_t checksum;

#define HISTORY_MAX 200
static run_result_t history[HISTORY_MAX];
static int history_count;

const char *pi_mode_name(int mode)
{
    switch (mode) {
    case MODE_BASE: return "No display";
    case MODE_TEXT: return "Text console";
    case MODE_GUI:  return "Graphical GUI";
    }
    return "?";
}

const char *pi_mode_short(int mode)
{
    switch (mode) {
    case MODE_BASE: return "none";
    case MODE_TEXT: return "text";
    case MODE_GUI:  return "gui";
    }
    return "?";
}

const char *pi_last_digits(void) { return digits; }

int pi_history(const run_result_t **out)
{
    *out = history;
    return history_count;
}

void pi_history_clear(void) { history_count = 0; }

/* Every mode does this same bookkeeping for each digit, so it's fair. */
static inline void store_digit(uint32_t index, uint8_t d)
{
    digits[index] = (char)('0' + d);
    checksum = checksum * 31u + d + 1u;
}

static void emit_base(uint32_t index, uint8_t d)
{
    store_digit(index, d);
}

static void emit_text(uint32_t index, uint8_t d)
{
    store_digit(index, d);
    term_putc((char)('0' + d));
    if (index == 0)
        term_putc('.');
}

static void emit_gui(uint32_t index, uint8_t d)
{
    store_digit(index, d);
    pigui_digit(index, d);
}

run_result_t pi_run(int mode, uint32_t n, int flags, const char *subtitle)
{
    run_result_t res = { mode, n, 0, 0, 0 };
    pi_emit_fn emit = mode == MODE_TEXT ? emit_text
                    : mode == MODE_GUI  ? emit_gui : emit_base;
    char buf[32];

    if (n == 0 || n > PI_MAX_DIGITS)
        return res;

    /* ---- set-up (not timed) ---- */
    if (mode == MODE_TEXT) {
        if (flags & PI_CLEAR)
            term_clear();
        uint8_t old = term_getattr();
        term_setcolor(VGA_YELLOW, VGA_BLACK);
        if (subtitle)
            kprintf("%s - ", subtitle);
        kprintf("Calculating %u digits of pi (TEXT mode)\n", n);
        term_setattr(old);
    } else if (mode == MODE_BASE && !(flags & PI_QUIET)) {
        kprintf("Calculating %u digits of pi (NO DISPLAY mode)...\n", n);
    }

    bool mirror = serial_mirror;
    serial_mirror = false;           /* keep the serial port out of the timing */
    digits[0] = '\0';
    checksum = 0;

    if (mode == MODE_GUI)
        gfx_enter();                 /* switching video modes isn't timed */

    /* Settle: keep the CPU busy for a moment so it is running at full speed
       when the timer starts (otherwise a run that starts right after a pause
       is unfairly slow - we found this during testing!). */
    busy_wait_ms(PI_SETTLE_MS);

    /* ---- the timed part ---- */
    uint64_t t0 = rdtsc();
    if (mode == MODE_GUI)
        pigui_begin(n, subtitle, t0);
    pi_spigot(n, work, emit);
    uint64_t t1 = rdtsc();

    /* ---- clean-up (not timed) ---- */
    digits[n] = '\0';
    res.cycles   = t1 - t0;
    res.us       = cycles_to_us(res.cycles);
    res.checksum = checksum;

    if (mode == MODE_GUI) {
        pigui_end(n, res.us, (flags & PI_INTERACTIVE) != 0);
        gfx_leave();
    }
    serial_mirror = mirror;

    if (!(flags & PI_NORECORD)) {
        if (history_count == HISTORY_MAX) {          /* full: drop oldest */
            memmove(history, history + 1, sizeof(history[0]) * (HISTORY_MAX - 1));
            history_count--;
        }
        history[history_count++] = res;
        serial_printf("RESULT,%s,%u,%llu,%llu,%08x\n", pi_mode_short(mode), n,
                      res.us, res.cycles, res.checksum);
    }

    if (!(flags & PI_QUIET)) {
        if (mode == MODE_TEXT)
            kprintf("\n");
        if (mode != MODE_TEXT) {
            kprintf("pi = %c.", digits[0]);
            for (uint32_t i = 1; i < n && i < 61; i++)
                kprintf("%c", digits[i]);
            kprintf("%s\n", n > 61 ? "..." : "");
        }
        fmt_ms(buf, sizeof(buf), res.us);
        term_setcolor(VGA_LIGHT_GREEN, VGA_BLACK);
        kprintf("Done: %u digits in %s  (%llu CPU cycles) [%s]\n", n, buf,
                res.cycles, pi_mode_name(mode));
        term_setcolor(VGA_LIGHT_GREY, VGA_BLACK);
    }
    return res;
}
