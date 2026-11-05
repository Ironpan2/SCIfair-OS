/*
 * terminal.c - the text console.
 *
 * In VGA text mode the screen is 80 columns x 25 rows. The video card reads
 * the characters straight out of memory at address 0xB8000: two bytes per
 * cell, one for the character and one for its colour. To print something we
 * just write into that memory.
 */
#include "kernel.h"

#define VGA_W 80
#define VGA_H 25

static volatile uint16_t *const vga = (volatile uint16_t *)0xB8000;
static int cur_x, cur_y;
static uint8_t cur_attr = 0x07;
bool serial_mirror = true;

/* Move the blinking hardware cursor (this talks to the VGA chip's ports). */
static void hw_cursor(void)
{
    uint16_t pos = (uint16_t)(cur_y * VGA_W + cur_x);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)(pos >> 8));
}

void term_sync_cursor(void) { hw_cursor(); }

void term_setcolor(uint8_t fg, uint8_t bg) { cur_attr = (uint8_t)(fg | (bg << 4)); }
uint8_t term_getattr(void) { return cur_attr; }
void term_setattr(uint8_t attr) { cur_attr = attr; }

void term_clear(void)
{
    for (int i = 0; i < VGA_W * VGA_H; i++)
        vga[i] = (uint16_t)(' ' | (cur_attr << 8));
    cur_x = cur_y = 0;
    hw_cursor();
}

void term_init(void)
{
    cur_attr = 0x07;
    term_clear();
}

static void scroll(void)
{
    for (int i = 0; i < VGA_W * (VGA_H - 1); i++)
        vga[i] = vga[i + VGA_W];
    for (int i = VGA_W * (VGA_H - 1); i < VGA_W * VGA_H; i++)
        vga[i] = (uint16_t)(' ' | (cur_attr << 8));
    cur_y = VGA_H - 1;
}

static void put_raw(char c)
{
    switch (c) {
    case '\n':
        cur_x = 0;
        cur_y++;
        break;
    case '\r':
        cur_x = 0;
        break;
    case '\t':
        cur_x = (cur_x + 8) & ~7;
        break;
    case '\b':
        if (cur_x > 0) cur_x--;
        break;
    default:
        vga[cur_y * VGA_W + cur_x] = (uint16_t)((uint8_t)c | (cur_attr << 8));
        cur_x++;
        break;
    }
    if (cur_x >= VGA_W) {
        cur_x = 0;
        cur_y++;
    }
    if (cur_y >= VGA_H)
        scroll();
}

void term_putc(char c)
{
    if (serial_mirror) {
        if (c == '\n')
            serial_putc('\r');
        serial_putc(c);
    }
    put_raw(c);
    hw_cursor();
}

void term_write(const char *s)
{
    while (*s)
        term_putc(*s++);
}

/* Erase the character before the cursor (used by the shell's line editor). */
void term_backspace(void)
{
    if (serial_mirror)
        serial_write("\b \b");
    if (cur_x > 0) {
        cur_x--;
    } else if (cur_y > 0) {
        cur_y--;
        cur_x = VGA_W - 1;
    }
    vga[cur_y * VGA_W + cur_x] = (uint16_t)(' ' | (cur_attr << 8));
    hw_cursor();
}

void term_save(uint16_t *buf)
{
    for (int i = 0; i < VGA_W * VGA_H; i++)
        buf[i] = vga[i];
}

void term_restore(const uint16_t *buf)
{
    for (int i = 0; i < VGA_W * VGA_H; i++)
        vga[i] = buf[i];
    hw_cursor();
}

static void term_sink(char c, void *ctx)
{
    (void)ctx;
    term_putc(c);
}

void kprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    kvformat(term_sink, 0, fmt, ap);
    va_end(ap);
}
