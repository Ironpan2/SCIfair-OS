/*
 * bench.c - the science experiment.
 *
 *   bench [digits] [trials]
 *
 * Runs all three pi programs several times each, then shows a
 * table with the average, fastest and slowest time for each mode and how
 * much slower the text and GUI versions were compared to no display.
 *
 * Fair-test measures:
 *  - a warm-up run of each mode first (not counted), so no run is "cold"
 *  - every run "settles" the CPU at full speed first (see pi.c)
 *  - the order rotates every trial (counterbalancing), so no mode always
 *    gets to go first or last
 *  - every run must produce the same digits (checksum), proving all three
 *    did exactly the same work
 *
 * Results are also written to the serial port as CSV lines, so they can be
 * saved to a file and opened in a spreadsheet (see README.md).
 */
#include "kernel.h"

#define MAX_TRIALS 9

static struct {
    bool     valid;
    uint32_t digits, trials;
    uint64_t us[MODE_COUNT][MAX_TRIALS];
    uint64_t avg[MODE_COUNT], min[MODE_COUNT], max[MODE_COUNT];
    bool     all_match;
    uint32_t checksum;
} last;

/* "2.35x" from a ratio a/b */
static void fmt_ratio(char *buf, size_t size, uint64_t a, uint64_t b)
{
    uint64_t r = b ? a * 100 / b : 0;
    ksnprintf(buf, size, "%llu.%02llux", r / 100, r % 100);
}

/* "+135.2%" from (a-b)/b */
static void fmt_pct(char *buf, size_t size, uint64_t a, uint64_t b)
{
    if (!b) {
        ksnprintf(buf, size, "?");
        return;
    }
    if (a >= b) {
        uint64_t p = (a - b) * 1000 / b;
        ksnprintf(buf, size, "+%llu.%llu%%", p / 10, p % 10);
    } else {
        uint64_t p = (b - a) * 1000 / b;
        ksnprintf(buf, size, "-%llu.%llu%%", p / 10, p % 10);
    }
}

static bool parse_args(int argc, char **argv, uint32_t *digits, uint32_t *trials)
{
    if (argc > 1 && (!parse_uint(argv[1], digits) || *digits < 1 || *digits > PI_MAX_DIGITS)) {
        kprintf("Digits must be a number from 1 to %u.\n", PI_MAX_DIGITS);
        return false;
    }
    if (argc > 2 && (!parse_uint(argv[2], trials) || *trials < 1 || *trials > MAX_TRIALS)) {
        kprintf("Trials must be a number from 1 to %u.\n", MAX_TRIALS);
        return false;
    }
    return true;
}

static void print_report(void)
{
    char a[24], b[24], c[24], r[16], p[16];

    term_setcolor(VGA_LIGHT_CYAN, VGA_BLACK);
    kprintf("=== EXPERIMENT RESULTS: %u digits of pi, %u trial%s per mode ===\n\n",
            last.digits, last.trials, last.trials == 1 ? "" : "s");
    term_setcolor(VGA_WHITE, VGA_BLACK);
    kprintf("%-15s %13s %13s %13s  %s\n", "Display mode", "Average", "Fastest",
            "Slowest", "vs. no display");
    term_setcolor(VGA_DARK_GREY, VGA_BLACK);
    kprintf("--------------- ------------- ------------- -------------  ---------------\n");
    static const uint8_t colors[MODE_COUNT] = { VGA_LIGHT_GREEN, VGA_YELLOW, VGA_LIGHT_MAGENTA };
    for (int m = 0; m < MODE_COUNT; m++) {
        fmt_ms(a, sizeof(a), last.avg[m]);
        fmt_ms(b, sizeof(b), last.min[m]);
        fmt_ms(c, sizeof(c), last.max[m]);
        fmt_ratio(r, sizeof(r), last.avg[m], last.avg[MODE_BASE]);
        fmt_pct(p, sizeof(p), last.avg[m], last.avg[MODE_BASE]);
        term_setcolor(colors[m], VGA_BLACK);
        kprintf("%-15s %13s %13s %13s  %s (%s)\n", pi_mode_name(m), a, b, c, r, p);
    }

    term_setcolor(VGA_LIGHT_GREY, VGA_BLACK);
    kprintf("\nEvery trial, in ms:\n");
    for (int m = 0; m < MODE_COUNT; m++) {
        kprintf("  %-14s", pi_mode_name(m));
        for (uint32_t t = 0; t < last.trials; t++) {
            uint64_t v = (last.us[m][t] + 5) / 10;          /* round to 0.01 ms */
            kprintf(" %llu.%02llu", v / 100, v % 100);
        }
        kprintf("\n");
    }

    kprintf("\n");
    if (last.all_match) {
        term_setcolor(VGA_LIGHT_GREEN, VGA_BLACK);
        kprintf("Check: all %u runs calculated the same digits (checksum %08x).\n",
                last.trials * MODE_COUNT, last.checksum);
    } else {
        term_setcolor(VGA_LIGHT_RED, VGA_BLACK);
        kprintf("WARNING: the runs did not all calculate the same digits!\n");
    }
    term_setcolor(VGA_LIGHT_GREY, VGA_BLACK);

    /* plain-English summary */
    fmt_ratio(r, sizeof(r), last.avg[MODE_TEXT], last.avg[MODE_BASE]);
    fmt_ratio(p, sizeof(p), last.avg[MODE_GUI], last.avg[MODE_BASE]);
    fmt_ratio(a, sizeof(a), last.avg[MODE_GUI], last.avg[MODE_TEXT]);
    kprintf("Showing text took %s the time of no display; the GUI took %s.\n", r, p);
    kprintf("The GUI version took %s as long as the text version.\n", a);
    term_setcolor(VGA_DARK_GREY, VGA_BLACK);
    kprintf("\nType 'graph' for a bar chart, or 'results' to see every run.\n");
    term_setcolor(VGA_LIGHT_GREY, VGA_BLACK);
}

void cmd_bench(int argc, char **argv)
{
    uint32_t digits = 1000, trials = 3;
    char sub[24];

    if (!parse_args(argc, argv, &digits, &trials))
        return;

    kprintf("Experiment: %u digits of pi, %u trials of each mode.\n", digits, trials);
    kprintf("The order rotates each trial so no mode always goes first.  Starting...\n");
    serial_printf("\n# SciFair-OS benchmark: %u digits, %u trials, %llu cycles/ms\n",
                  digits, trials, tsc_per_ms);
    serial_printf("TRIAL,trial,mode,digits,time_us,cycles,checksum\n");
    sleep_ms(1200);

    /* Warm-up: run each mode once without counting it. The first time any
       code runs it is "cold" (not yet in the CPU's caches) and slower. */
    for (int m = 0; m < MODE_COUNT; m++)
        pi_run(m, digits, PI_QUIET | PI_NORECORD | PI_CLEAR, "Warm-up");

    last.valid = false;
    last.digits = digits;
    last.trials = trials;
    last.all_match = true;
    for (uint32_t t = 0; t < trials; t++) {
        ksnprintf(sub, sizeof(sub), "Trial %u/%u", t + 1, trials);
        /* Counterbalancing: trial 1 runs none,text,gui; trial 2 runs
           text,gui,none; trial 3 runs gui,none,text; and so on. */
        for (int k = 0; k < MODE_COUNT; k++) {
            int m = (int)((t + (uint32_t)k) % MODE_COUNT);
            run_result_t res = pi_run(m, digits, PI_QUIET | PI_CLEAR | PI_NORECORD, sub);
            last.us[m][t] = res.us;
            if (t == 0 && k == 0)
                last.checksum = res.checksum;
            else if (res.checksum != last.checksum)
                last.all_match = false;
            serial_printf("TRIAL,%u,%s,%u,%llu,%llu,%08x\n", t + 1, pi_mode_short(m),
                          digits, res.us, res.cycles, res.checksum);
            if (m == MODE_TEXT)
                sleep_ms(300);   /* let people see the digits for a moment */
        }
    }

    for (int m = 0; m < MODE_COUNT; m++) {
        uint64_t sum = 0, mn = ~(uint64_t)0, mx = 0;
        for (uint32_t t = 0; t < trials; t++) {
            uint64_t v = last.us[m][t];
            sum += v;
            if (v < mn) mn = v;
            if (v > mx) mx = v;
        }
        last.avg[m] = sum / trials;
        last.min[m] = mn;
        last.max[m] = mx;
        serial_printf("SUMMARY,%s,%u,%u,%llu,%llu,%llu\n", pi_mode_short(m), digits,
                      trials, last.avg[m], mn, mx);
    }
    last.valid = true;

    term_setcolor(VGA_LIGHT_GREY, VGA_BLACK);
    term_clear();
    print_report();
}

/* Show every run saved since boot (from pitext, pigui, pibase and bench). */
void cmd_results(int argc, char **argv)
{
    const run_result_t *h;
    char t[24];

    if (argc > 1 && strcmp(argv[1], "clear") == 0) {
        pi_history_clear();
        kprintf("Results history cleared.\n");
        return;
    }
    int n = pi_history(&h);
    if (n == 0 && !last.valid) {
        kprintf("No results yet. Try: pitext, pigui, pibase or bench\n");
        return;
    }
    if (n) {
        term_setcolor(VGA_WHITE, VGA_BLACK);
        kprintf("  #  Mode            Digits          Time      CPU cycles  Checksum\n");
        term_setcolor(VGA_LIGHT_GREY, VGA_BLACK);
        int start = n > 15 ? n - 15 : 0;
        if (start)
            kprintf("  (showing the last 15 of %d runs)\n", n);
        for (int i = start; i < n; i++) {
            fmt_ms(t, sizeof(t), h[i].us);
            kprintf("%3d  %-14s %7u %13s %15llu  %08x\n", i + 1,
                    pi_mode_name(h[i].mode), h[i].digits, t, h[i].cycles,
                    h[i].checksum);
        }
    }
    if (last.valid) {
        kprintf("\n");
        print_report();
    }
}

/* Draw the last benchmark as a bar chart in graphics mode. */
void cmd_graph(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    char buf[40];

    if (!last.valid) {
        kprintf("Run 'bench' first, then 'graph' will chart the results.\n");
        return;
    }

    gfx_enter();
    gfx_desktop();
    gfx_window(6, 4, 308, 192, "Experiment Results");
    ksnprintf(buf, sizeof(buf), "Avg time, %u digits, %u trials", last.digits, last.trials);
    gfx_text_center(160, 26, buf, C_INK, -1);

    /* chart area */
    const int ax = 44, ay = 48, aw = 256, ah = 104;
    gfx_fill(ax, ay, aw, ah, C_PANEL);
    gfx_bevel(ax - 1, ay - 1, aw + 2, ah + 2, C_LOWLITE, C_HILITE);

    const int bar_max = ah - 22;          /* leave room for the value label */
    uint64_t top = 1;
    for (int m = 0; m < MODE_COUNT; m++)
        if (last.avg[m] > top) top = last.avg[m];

    /* grid lines at 25% steps, labelled in ms */
    for (int i = 0; i <= 4; i++) {
        int y = ay + ah - 1 - bar_max * i / 4;
        for (int x = ax; x < ax + aw; x += 3)
            gfx_pixel(x, y, C_RINGBG);
        uint64_t v = top * (uint64_t)i / 4 / 1000;
        ksnprintf(buf, sizeof(buf), "%llu", v);
        gfx_text(ax - 4 - (int)strlen(buf) * 8, y - 8, buf, C_INK, -1);
    }

    static const char *labels[MODE_COUNT] = { "None", "Text", "GUI" };
    static const uint8_t base_col[MODE_COUNT] = { C_HUE + 22, C_HUE + 8, C_HUE + 52 };
    int bw = 52, gap = (aw - MODE_COUNT * bw) / (MODE_COUNT + 1);
    for (int m = 0; m < MODE_COUNT; m++) {
        int bh = (int)(last.avg[m] * (uint64_t)bar_max / top);
        if (bh < 1) bh = 1;
        int bx = ax + gap + m * (bw + gap), by = ay + ah - bh;
        gfx_fill(bx + 3, by + 3, bw, bh - 3, C_LOWLITE);           /* shadow */
        gfx_fill(bx, by, bw, bh, base_col[m]);
        gfx_bevel(bx, by, bw, bh, C_WHITE, C_BORDER);
        uint64_t us = last.avg[m];
        ksnprintf(buf, sizeof(buf), "%llu.%llu", us / 1000, (us % 1000) / 100);
        gfx_text_center(bx + bw / 2, by - 17, buf, C_INK, -1);
        gfx_text_center(bx + bw / 2, ay + ah + 4, labels[m], C_INK, -1);
    }
    gfx_text(10, 30, "ms", C_DGRAY, -1);
    gfx_text_center(160, 176, "Press any key to go back", C_DGRAY, -1);
    gfx_present();

    kb_flush();
    kb_getkey();
    gfx_leave();
}
