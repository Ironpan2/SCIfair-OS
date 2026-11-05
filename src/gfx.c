/*
 * gfx.c - VGA graphics mode (320 x 200 pixels, 256 colours, "mode 13h").
 *
 * Normally you'd ask the BIOS to switch video modes, but the BIOS only works
 * in 16-bit real mode and our OS runs in 32-bit protected mode. So we program
 * the VGA card's registers directly, the same values the BIOS would use.
 *
 * Switching to graphics overwrites the video memory that holds the text-mode
 * font and screen, so we save those first and put them back when we leave.
 *
 * All drawing happens in a "back buffer" in normal memory, and gfx_present()
 * copies the finished picture to the screen in one go (double buffering).
 */
#include "kernel.h"

/* VGA register ports */
#define AC_INDEX     0x3C0
#define AC_WRITE     0x3C0
#define MISC_WRITE   0x3C2
#define SEQ_INDEX    0x3C4
#define SEQ_DATA     0x3C5
#define DAC_READ_IDX 0x3C7
#define DAC_WRITE_IDX 0x3C8
#define DAC_DATA     0x3C9
#define GC_INDEX     0x3CE
#define GC_DATA      0x3CF
#define CRTC_INDEX   0x3D4
#define CRTC_DATA    0x3D5
#define INSTAT_READ  0x3DA

/* Register values for the standard 80x25 text mode (BIOS mode 3). */
static uint8_t regs_80x25_text[] = {
    /* MISC */ 0x67,
    /* SEQ  */ 0x03, 0x00, 0x03, 0x00, 0x02,
    /* CRTC */ 0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F,
               0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x50,
               0x9C, 0x0E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3, 0xFF,
    /* GC   */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00, 0xFF,
    /* AC   */ 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
               0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
               0x0C, 0x00, 0x0F, 0x08, 0x00,
};

/* Register values for 320x200 with 256 colours (BIOS mode 13h). */
static uint8_t regs_320x200x256[] = {
    /* MISC */ 0x63,
    /* SEQ  */ 0x03, 0x01, 0x0F, 0x00, 0x0E,
    /* CRTC */ 0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F,
               0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
               0x9C, 0x0E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3, 0xFF,
    /* GC   */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F, 0xFF,
    /* AC   */ 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
               0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
               0x41, 0x00, 0x0F, 0x00, 0x00,
};

uint8_t gfx_bb[GFX_W * GFX_H];
static uint8_t  font[256 * 16];        /* 8x16 font copied from the VGA card */
static uint16_t saved_text[80 * 25];
static uint8_t  saved_dac[256 * 3];
static bool     active;

static void write_regs(uint8_t *regs)
{
    outb(MISC_WRITE, *regs++);
    for (uint8_t i = 0; i < 5; i++) {
        outb(SEQ_INDEX, i);
        outb(SEQ_DATA, *regs++);
    }
    /* unlock the CRTC registers so we can change them */
    outb(CRTC_INDEX, 0x03);
    outb(CRTC_DATA, inb(CRTC_DATA) | 0x80);
    outb(CRTC_INDEX, 0x11);
    outb(CRTC_DATA, inb(CRTC_DATA) & ~0x80);
    regs[0x03] |= 0x80;
    regs[0x11] &= ~0x80;
    for (uint8_t i = 0; i < 25; i++) {
        outb(CRTC_INDEX, i);
        outb(CRTC_DATA, *regs++);
    }
    for (uint8_t i = 0; i < 9; i++) {
        outb(GC_INDEX, i);
        outb(GC_DATA, *regs++);
    }
    for (uint8_t i = 0; i < 21; i++) {
        (void)inb(INSTAT_READ);
        outb(AC_INDEX, i);
        outb(AC_WRITE, *regs++);
    }
    (void)inb(INSTAT_READ);
    outb(AC_INDEX, 0x20);          /* turn the screen back on */
}

/* Where is VGA memory currently mapped? (depends on the mode) */
static uintptr_t vga_mem_base(void)
{
    outb(GC_INDEX, 6);
    switch ((inb(GC_DATA) >> 2) & 3) {
    case 0:
    case 1:  return 0xA0000;
    case 2:  return 0xB0000;
    default: return 0xB8000;
    }
}

/* The font lives in "plane 2" of video memory. Temporarily switch the card
   into flat addressing so we can read or write it. */
static void font_transfer(bool write)
{
    uint8_t seq2, seq4, gc4, gc5, gc6;

    outb(SEQ_INDEX, 2); seq2 = inb(SEQ_DATA);
    outb(SEQ_INDEX, 4); seq4 = inb(SEQ_DATA);
    outb(SEQ_DATA, seq4 | 0x04);              /* odd/even addressing off */
    outb(GC_INDEX, 4); gc4 = inb(GC_DATA);
    outb(GC_INDEX, 5); gc5 = inb(GC_DATA);
    outb(GC_DATA, gc5 & ~0x10);
    outb(GC_INDEX, 6); gc6 = inb(GC_DATA);
    outb(GC_DATA, gc6 & ~0x02);

    outb(GC_INDEX, 4); outb(GC_DATA, 2);      /* read from plane 2 */
    outb(SEQ_INDEX, 2); outb(SEQ_DATA, 4);    /* write to plane 2 */

    volatile uint8_t *mem = (volatile uint8_t *)vga_mem_base();
    for (int ch = 0; ch < 256; ch++) {
        for (int row = 0; row < 16; row++) {
            if (write)
                mem[ch * 32 + row] = font[ch * 16 + row];
            else
                font[ch * 16 + row] = mem[ch * 32 + row];
        }
    }

    outb(SEQ_INDEX, 2); outb(SEQ_DATA, seq2);
    outb(SEQ_INDEX, 4); outb(SEQ_DATA, seq4);
    outb(GC_INDEX, 4);  outb(GC_DATA, gc4);
    outb(GC_INDEX, 5);  outb(GC_DATA, gc5);
    outb(GC_INDEX, 6);  outb(GC_DATA, gc6);
}

void gfx_init(void)
{
    font_transfer(false);          /* keep a copy of the BIOS font */
}

bool gfx_active(void) { return active; }

/* ---------------- palette ----------------
 * In 256-colour mode each pixel is a number 0-255, and the "DAC" turns each
 * number into a real colour (red, green, blue each 0-63). We choose our own
 * colours to get nice gradients. */
static void pal_set(int i, int r, int g, int b)
{
    outb(DAC_WRITE_IDX, (uint8_t)i);
    outb(DAC_DATA, (uint8_t)r);
    outb(DAC_DATA, (uint8_t)g);
    outb(DAC_DATA, (uint8_t)b);
}

static void pal_ramp(int start, int n, int r0, int g0, int b0, int r1, int g1, int b1)
{
    for (int k = 0; k < n; k++)
        pal_set(start + k,
                r0 + (r1 - r0) * k / (n - 1),
                g0 + (g1 - g0) * k / (n - 1),
                b0 + (b1 - b0) * k / (n - 1));
}

static void gfx_set_palette(void)
{
    static const uint8_t ega[16][3] = {
        {0, 0, 0}, {0, 0, 42}, {0, 42, 0}, {0, 42, 42}, {42, 0, 0}, {42, 0, 42},
        {42, 21, 0}, {42, 42, 42}, {21, 21, 21}, {21, 21, 63}, {21, 63, 21},
        {21, 63, 63}, {63, 21, 21}, {63, 21, 63}, {63, 63, 21}, {63, 63, 63},
    };
    for (int i = 0; i < 16; i++)
        pal_set(i, ega[i][0], ega[i][1], ega[i][2]);

    pal_ramp(C_DESK,  32,  3,  5, 18,  28,  8, 34);   /* navy -> purple   */
    pal_ramp(C_TITLE, 32,  6, 16, 44,  16, 44, 63);   /* blue -> cyan     */
    pal_ramp(C_PROG,  32, 8, 46, 18,  60, 58, 10);    /* green -> yellow  */
    pal_ramp(C_GRAY,  32,  0,  0,  0,  63, 63, 63);   /* black -> white   */

    /* rainbow: red -> yellow -> green -> cyan -> blue -> magenta -> red */
    for (int k = 0; k < 64; k++) {
        int seg = k * 6 / 64, f = (k * 6 % 64) * 63 / 64, r = 0, g = 0, b = 0;
        switch (seg) {
        case 0: r = 63; g = f; break;
        case 1: r = 63 - f; g = 63; break;
        case 2: g = 63; b = f; break;
        case 3: g = 63 - f; b = 63; break;
        case 4: r = f; b = 63; break;
        default: r = 63; b = 63 - f; break;
        }
        pal_set(C_HUE + k, r, g, b);
    }

    pal_set(C_WIN,     52, 53, 57);
    pal_set(C_PANEL,   63, 63, 63);
    pal_set(C_SHADOW,   2,  2,  8);
    pal_set(C_BORDER,   8,  9, 16);
    pal_set(C_INK,      6,  8, 16);
    pal_set(C_DIGIT,    8, 18, 44);
    pal_set(C_RINGBG,  40, 41, 46);
    pal_set(C_HILITE,  62, 62, 63);
    pal_set(C_LOWLITE, 32, 33, 38);
    pal_set(C_STAR,    50, 50, 63);
}

/* ---------------- switching modes ---------------- */
void gfx_enter(void)
{
    if (active)
        return;
    term_save(saved_text);
    outb(DAC_READ_IDX, 0);
    for (int i = 0; i < 256 * 3; i++)
        saved_dac[i] = inb(DAC_DATA);

    write_regs(regs_320x200x256);
    gfx_set_palette();
    gfx_clear(C_BLACK);
    gfx_present();
    active = true;
}

void gfx_leave(void)
{
    if (!active)
        return;
    write_regs(regs_80x25_text);
    font_transfer(true);
    outb(DAC_WRITE_IDX, 0);
    for (int i = 0; i < 256 * 3; i++)
        outb(DAC_DATA, saved_dac[i]);
    term_restore(saved_text);
    active = false;
}

void gfx_present(void)
{
    /* copy 64000 bytes (16000 x 4) to video memory at 0xA0000 */
    void *dst = (void *)0xA0000;
    const void *src = gfx_bb;
    size_t n = GFX_W * GFX_H / 4;
    __asm__ volatile ("rep movsl" : "+D"(dst), "+S"(src), "+c"(n) : : "memory");
}

/* ---------------- drawing ---------------- */
void gfx_clear(uint8_t c)
{
    memset(gfx_bb, c, sizeof(gfx_bb));
}

void gfx_pixel(int x, int y, uint8_t c)
{
    if ((unsigned)x < GFX_W && (unsigned)y < GFX_H)
        gfx_bb[y * GFX_W + x] = c;
}

void gfx_fill(int x, int y, int w, int h, uint8_t c)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > GFX_W) w = GFX_W - x;
    if (y + h > GFX_H) h = GFX_H - y;
    if (w <= 0 || h <= 0)
        return;
    for (int row = y; row < y + h; row++)
        memset(&gfx_bb[row * GFX_W + x], c, (size_t)w);
}

void gfx_rect(int x, int y, int w, int h, uint8_t c)
{
    gfx_fill(x, y, w, 1, c);
    gfx_fill(x, y + h - 1, w, 1, c);
    gfx_fill(x, y, 1, h, c);
    gfx_fill(x + w - 1, y, 1, h, c);
}

/* 3D-looking edge: light on top/left, dark on bottom/right */
void gfx_bevel(int x, int y, int w, int h, uint8_t light, uint8_t dark)
{
    gfx_fill(x, y, w, 1, light);
    gfx_fill(x, y, 1, h, light);
    gfx_fill(x, y + h - 1, w, 1, dark);
    gfx_fill(x + w - 1, y, 1, h, dark);
}

void gfx_vgrad(int x, int y, int w, int h, uint8_t base, int shades)
{
    for (int r = 0; r < h; r++)
        gfx_fill(x, y + r, w, 1, (uint8_t)(base + r * shades / h));
}

void gfx_hgrad(int x, int y, int w, int h, uint8_t base, int shades)
{
    for (int c = 0; c < w; c++)
        gfx_fill(x + c, y, 1, h, (uint8_t)(base + c * shades / w));
}

void gfx_circle(int cx, int cy, int r, uint8_t c)
{
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            if (dx * dx + dy * dy <= r * r + r)
                gfx_pixel(cx + dx, cy + dy, c);
}

void gfx_line(int x0, int y0, int x1, int y1, uint8_t c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        gfx_pixel(x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void gfx_char(int x, int y, char ch, uint8_t fg, int bg)
{
    const uint8_t *glyph = &font[(uint8_t)ch * 16];
    for (int row = 0; row < 16; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col))
                gfx_pixel(x + col, y + row, fg);
            else if (bg >= 0)
                gfx_pixel(x + col, y + row, (uint8_t)bg);
        }
    }
}

void gfx_char2x(int x, int y, char ch, uint8_t fg)
{
    const uint8_t *glyph = &font[(uint8_t)ch * 16];
    for (int row = 0; row < 16; row++)
        for (int col = 0; col < 8; col++)
            if (glyph[row] & (0x80 >> col))
                gfx_fill(x + col * 2, y + row * 2, 2, 2, fg);
}

void gfx_text(int x, int y, const char *s, uint8_t fg, int bg)
{
    for (; *s; s++, x += 8)
        gfx_char(x, y, *s, fg, bg);
}

void gfx_text_center(int cx, int y, const char *s, uint8_t fg, int bg)
{
    gfx_text(cx - (int)strlen(s) * 4, y, s, fg, bg);
}

/* A gradient "desktop" with a few stars sprinkled on it. */
void gfx_desktop(void)
{
    gfx_vgrad(0, 0, GFX_W, GFX_H, C_DESK, 32);
    uint32_t seed = 12345;
    for (int i = 0; i < 90; i++) {
        seed = seed * 1103515245u + 12345u;
        int x = (int)((seed >> 8) % GFX_W);
        seed = seed * 1103515245u + 12345u;
        int y = (int)((seed >> 8) % GFX_H);
        gfx_pixel(x, y, (i % 7) ? C_STAR : C_WHITE);
    }
}

/* A window with a drop shadow, border, gradient title bar and the three
   little coloured buttons in the corner. */
void gfx_window(int x, int y, int w, int h, const char *title)
{
    gfx_fill(x + 4, y + 4, w, h, C_SHADOW);
    gfx_fill(x, y, w, h, C_WIN);
    gfx_rect(x, y, w, h, C_BORDER);
    gfx_hgrad(x + 1, y + 1, w - 2, 17, C_TITLE, 32);
    gfx_fill(x + 1, y + 18, w - 2, 1, C_BORDER);
    gfx_text(x + 8, y + 2, title, C_WHITE, -1);
    gfx_circle(x + w - 12, y + 9, 4, C_LRED);
    gfx_circle(x + w - 24, y + 9, 4, C_YELLOW);
    gfx_circle(x + w - 36, y + 9, 4, C_LGREEN);
}
