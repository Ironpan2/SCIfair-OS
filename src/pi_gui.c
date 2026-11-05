/*
 * pi_gui.c - the "fancy GUI" pi calculator.
 *
 * Layout of the 320x200 screen:
 *
 *   +-- Pi Calculator ------------------------ o o o -+
 *   | +--------------------------+     .-~~-.         |
 *   | | 3.14159265358979323846.. |    /  pi  \  ring  |
 *   | | 26433832795028841971...  |    \      /        |
 *   | |  (digits panel)          |     '-..-'         |
 *   | +--------------------------+      42%           |
 *   | [=========progress bar=========            ]    |
 *   | Digit 420 of 1000                   123 ms      |
 *   +-------------------------------------------------+
 *
 * Every time a new digit is ready we: draw the digit, update the progress
 * bar, the rainbow progress ring, the percentage and the status line, then
 * copy the whole frame to the screen. That's what makes a GUI pretty - and
 * also what makes it slower!
 */
#include "kernel.h"

/* window */
#define WX 6
#define WY 4
#define WW 308
#define WH 192
/* digits panel */
#define DP_COLS 26
#define DP_ROWS 8
#define DP_X 14
#define DP_Y 28
#define DP_W (DP_COLS * 8)
#define DP_H (DP_ROWS * 16)
/* progress ring */
#define RC_X 268
#define RC_Y 70
#define R_OUT 30
#define R_IN 19
#define RING_D (2 * R_OUT + 1)
/* progress bar */
#define PB_X 14
#define PB_Y 163
#define PB_W 292
#define PB_H 10
/* status line */
#define ST_Y 177

static uint32_t g_n;
static int g_col, g_row;
static uint64_t g_t0;
static int16_t ring_angle[RING_D * RING_D];   /* -1 = not part of the ring */
static int last_pct = -1;

/* Angle of the point (x, y) measured clockwise from 12 o'clock, 0..255.
   Uses a simple straight-line approximation instead of real trigonometry. */
static int clock_angle(int dx, int dy)
{
    int x = dx, y = -dy;                       /* screen y points down */
    int ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    int a;
    if (ax == 0 && ay == 0)
        return 0;
    if (ax >= ay)
        a = ay * 32 / ax;                      /* 0..32  = 0..45 degrees */
    else
        a = 64 - ax * 32 / ay;                 /* 32..64 = 45..90 degrees */
    if (x < 0) a = 128 - a;
    if (y < 0) a = 256 - a;
    return (64 - a) & 255;                     /* convert to clockwise */
}

static void draw_ring(uint32_t done)
{
    int fill = (int)((uint64_t)done * 256 / g_n);
    for (int y = 0; y < RING_D; y++) {
        for (int x = 0; x < RING_D; x++) {
            int ang = ring_angle[y * RING_D + x];
            if (ang < 0)
                continue;
            uint8_t c = ang < fill ? (uint8_t)(C_HUE + (ang >> 2)) : C_RINGBG;
            gfx_bb[(RC_Y - R_OUT + y) * GFX_W + (RC_X - R_OUT + x)] = c;
        }
    }
}

static void draw_progress(uint32_t done)
{
    int w = (int)((uint64_t)done * (PB_W - 2) / g_n);
    gfx_fill(PB_X + 1, PB_Y + 1, PB_W - 2, PB_H - 2, C_PANEL);
    for (int x = 0; x < w; x++)
        gfx_fill(PB_X + 1 + x, PB_Y + 1, 1, PB_H - 2,
                 (uint8_t)(C_PROG + x * 32 / (PB_W - 2)));
}

static void draw_status(uint32_t done)
{
    char buf[40];
    uint64_t us = cycles_to_us(rdtsc() - g_t0);

    gfx_fill(PB_X, ST_Y, PB_W, 16, C_WIN);
    ksnprintf(buf, sizeof(buf), "Digit %u of %u", done, g_n);
    gfx_text(PB_X, ST_Y, buf, C_INK, -1);
    ksnprintf(buf, sizeof(buf), "%llu ms", us / 1000);
    gfx_text(PB_X + PB_W - (int)strlen(buf) * 8, ST_Y, buf, C_DIGIT, -1);

    int pct = (int)((uint64_t)done * 100 / g_n);
    if (pct != last_pct) {
        last_pct = pct;
        ksnprintf(buf, sizeof(buf), "%d%%", pct);
        gfx_fill(RC_X - 32, RC_Y + R_OUT + 6, 64, 16, C_WIN);
        gfx_text_center(RC_X, RC_Y + R_OUT + 6, buf, C_INK, -1);
    }
}

/* Put one character in the digits panel, scrolling it up when it's full. */
static void panel_putc(char c)
{
    if (g_col == DP_COLS) {
        g_col = 0;
        g_row++;
    }
    if (g_row == DP_ROWS) {
        for (int y = DP_Y; y < DP_Y + DP_H - 16; y++)
            memcpy(&gfx_bb[y * GFX_W + DP_X], &gfx_bb[(y + 16) * GFX_W + DP_X], DP_W);
        gfx_fill(DP_X, DP_Y + DP_H - 16, DP_W, 16, C_PANEL);
        g_row = DP_ROWS - 1;
    }
    gfx_char(DP_X + g_col * 8, DP_Y + g_row * 16, c,
             c == '3' && g_row == 0 && g_col == 0 ? C_RED : C_DIGIT, -1);
    g_col++;
}

void pigui_begin(uint32_t n, const char *subtitle, uint64_t t0)
{
    g_n = n;
    g_col = g_row = 0;
    g_t0 = t0;
    last_pct = -1;

    /* work out which pixels form the ring, and their angles */
    for (int y = 0; y < RING_D; y++) {
        for (int x = 0; x < RING_D; x++) {
            int dx = x - R_OUT, dy = y - R_OUT, d2 = dx * dx + dy * dy;
            bool in = d2 <= R_OUT * R_OUT + R_OUT && d2 >= R_IN * R_IN;
            ring_angle[y * RING_D + x] = (int16_t)(in ? clock_angle(dx, dy) : -1);
        }
    }

    gfx_desktop();
    gfx_window(WX, WY, WW, WH, "\xE3 Pi Calculator - GUI");

    /* sunken white panel for the digits */
    gfx_fill(DP_X - 3, DP_Y - 3, DP_W + 6, DP_H + 6, C_PANEL);
    gfx_bevel(DP_X - 4, DP_Y - 4, DP_W + 8, DP_H + 8, C_LOWLITE, C_HILITE);

    /* ring with a big pi symbol in the middle */
    draw_ring(0);
    gfx_char2x(RC_X - 8, RC_Y - 16, '\xE3', C_DIGIT);
    if (subtitle)
        gfx_text_center(RC_X, RC_Y + R_OUT + 26, subtitle, C_DIGIT, -1);
    else
        gfx_text_center(RC_X, RC_Y + R_OUT + 26, "Working", C_DIGIT, -1);

    /* progress bar frame */
    gfx_bevel(PB_X, PB_Y, PB_W, PB_H, C_LOWLITE, C_HILITE);
    draw_progress(0);
    draw_status(0);
    gfx_present();
}

void pigui_digit(uint32_t index, uint8_t digit)
{
    uint32_t done = index + 1;
    panel_putc((char)('0' + digit));
    if (index == 0)
        panel_putc('.');
    draw_progress(done);
    draw_ring(done);
    draw_status(done);
    gfx_present();               /* show the new frame */
}

void pigui_end(uint32_t n, uint64_t us, bool interactive)
{
    char buf[40], ms[24];

    /* mark the run as finished under the ring */
    gfx_fill(RC_X - 38, RC_Y + R_OUT + 26, 76, 16, C_WIN);
    gfx_text_center(RC_X, RC_Y + R_OUT + 26, "Done!", C_GREEN, -1);

    /* a little "Finished!" dialog box */
    int dx = 50, dy = 62, dw = 220, dh = 82;
    gfx_fill(dx + 3, dy + 3, dw, dh, C_SHADOW);
    gfx_fill(dx, dy, dw, dh, C_WIN);
    gfx_rect(dx, dy, dw, dh, C_BORDER);
    gfx_hgrad(dx + 1, dy + 1, dw - 2, 17, C_TITLE, 32);
    gfx_text(dx + 8, dy + 2, "Finished!", C_WHITE, -1);
    ksnprintf(buf, sizeof(buf), "%u digits of pi", n);
    gfx_text_center(dx + dw / 2, dy + 22, buf, C_INK, -1);
    fmt_ms(ms, sizeof(ms), us);
    ksnprintf(buf, sizeof(buf), "in %s", ms);
    gfx_text_center(dx + dw / 2, dy + 40, buf, C_DIGIT, -1);
    gfx_text_center(dx + dw / 2, dy + 60,
                    interactive ? "Press any key" : "Next run...", C_DGRAY, -1);
    gfx_present();

    if (interactive) {
        kb_flush();
        kb_getkey();
    } else {
        sleep_ms(500);
    }
}
