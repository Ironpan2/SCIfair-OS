/*
 * SciFair-OS - a tiny 32-bit hobby operating system written in C.
 *
 * kernel.h: one shared header with every type and function the kernel uses.
 * (Keeping it in one file makes the project easier to read.)
 */
#ifndef KERNEL_H
#define KERNEL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>

#define OS_NAME    "SciFair-OS"
#define OS_VERSION "1.0"

/* ------------------------------------------------------------------ */
/* Talking to hardware: I/O ports and special CPU instructions         */
/* ------------------------------------------------------------------ */
static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline void outw(uint16_t port, uint16_t val)
{
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port)
{
    uint8_t r;
    __asm__ volatile ("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}
static inline void io_wait(void) { outb(0x80, 0); }
static inline void cpu_cli(void) { __asm__ volatile ("cli" ::: "memory"); }
static inline void cpu_sti(void) { __asm__ volatile ("sti" ::: "memory"); }
static inline void cpu_hlt(void) { __asm__ volatile ("hlt" ::: "memory"); }

/* Read the CPU's Time Stamp Counter: it counts CPU clock cycles. */
static inline uint64_t rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static inline void cpuid(uint32_t leaf, uint32_t *a, uint32_t *b,
                         uint32_t *c, uint32_t *d)
{
    __asm__ volatile ("cpuid"
                      : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                      : "a"(leaf), "c"(0));
}

/* ------------------------------------------------------------------ */
/* lib.c - tiny C library (we can't use the normal one inside an OS)   */
/* ------------------------------------------------------------------ */
void  *memset(void *dst, int c, size_t n);
void  *memcpy(void *dst, const void *src, size_t n);
void  *memmove(void *dst, const void *src, size_t n);
size_t strlen(const char *s);
int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);
bool   parse_uint(const char *s, uint32_t *out);
int    kvformat(void (*sink)(char, void *), void *ctx, const char *fmt, va_list ap);
int    ksnprintf(char *buf, size_t size, const char *fmt, ...);
/* "12.345 ms" style formatting of a microsecond count */
void   fmt_ms(char *buf, size_t size, uint64_t us);

/* ------------------------------------------------------------------ */
/* terminal.c - 80x25 VGA text console                                 */
/* ------------------------------------------------------------------ */
enum vga_color {
    VGA_BLACK = 0, VGA_BLUE, VGA_GREEN, VGA_CYAN, VGA_RED, VGA_MAGENTA,
    VGA_BROWN, VGA_LIGHT_GREY, VGA_DARK_GREY, VGA_LIGHT_BLUE,
    VGA_LIGHT_GREEN, VGA_LIGHT_CYAN, VGA_LIGHT_RED, VGA_LIGHT_MAGENTA,
    VGA_YELLOW, VGA_WHITE
};
extern bool serial_mirror;   /* copy console output to the serial port? */
void    term_init(void);
void    term_clear(void);
void    term_putc(char c);
void    term_write(const char *s);
void    term_backspace(void);
void    term_setcolor(uint8_t fg, uint8_t bg);
uint8_t term_getattr(void);
void    term_setattr(uint8_t attr);
void    term_save(uint16_t *buf);           /* 80*25 cells */
void    term_restore(const uint16_t *buf);
void    term_sync_cursor(void);
void    kprintf(const char *fmt, ...);

/* ------------------------------------------------------------------ */
/* serial.c - COM1 serial port (used to save results to a file)        */
/* ------------------------------------------------------------------ */
void serial_init(void);
bool serial_present(void);
void serial_putc(char c);
void serial_write(const char *s);
void serial_printf(const char *fmt, ...);
bool serial_has_char(void);
char serial_getc(void);

/* ------------------------------------------------------------------ */
/* cpu.c - GDT, IDT, PIC and interrupt handling                        */
/* ------------------------------------------------------------------ */
typedef struct regs {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags;
} regs_t;

typedef void (*irq_handler_t)(regs_t *r);
void gdt_init(void);
void idt_init(void);
void pic_init(void);
void irq_install(int irq, irq_handler_t handler);

/* ------------------------------------------------------------------ */
/* timer.c - PIT timer (1000 ticks per second) and cycle counting      */
/* ------------------------------------------------------------------ */
extern uint64_t tsc_per_ms;               /* CPU cycles per millisecond */
void     timer_init(void);
void     tsc_calibrate(void);
uint32_t timer_ticks(void);               /* milliseconds since boot */
void     sleep_ms(uint32_t ms);           /* rests the CPU (hlt) */
void     busy_wait_ms(uint32_t ms);       /* keeps the CPU busy */
uint64_t cycles_to_us(uint64_t cycles);

/* ------------------------------------------------------------------ */
/* keyboard.c - PS/2 keyboard (+ serial input)                         */
/* ------------------------------------------------------------------ */
#define KEY_UP     0x100
#define KEY_DOWN   0x101
#define KEY_LEFT   0x102
#define KEY_RIGHT  0x103
#define KEY_CTRL_C 0x104
#define KEY_CTRL_L 0x105
void kb_init(void);
int  kb_getkey(void);      /* waits for a key */
bool kb_haskey(void);
void kb_flush(void);

/* ------------------------------------------------------------------ */
/* gfx.c - VGA 320x200 256-colour graphics mode                        */
/* ------------------------------------------------------------------ */
#define GFX_W 320
#define GFX_H 200

/* palette layout (see gfx_set_palette in gfx.c) */
#define C_BLACK      0
#define C_BLUE       1
#define C_GREEN      2
#define C_CYAN       3
#define C_RED        4
#define C_MAGENTA    5
#define C_BROWN      6
#define C_LGRAY      7
#define C_DGRAY      8
#define C_LBLUE      9
#define C_LGREEN    10
#define C_LCYAN     11
#define C_LRED      12
#define C_LMAGENTA  13
#define C_YELLOW    14
#define C_WHITE     15
#define C_DESK      16   /* 32 shades: desktop background gradient */
#define C_TITLE     48   /* 32 shades: window title bar gradient    */
#define C_PROG      80   /* 32 shades: progress bar gradient        */
#define C_GRAY     112   /* 32 shades: black -> white               */
#define C_HUE      144   /* 64 shades: rainbow                      */
#define C_WIN      208   /* window body                             */
#define C_PANEL    209   /* white text panel                        */
#define C_SHADOW   210   /* window drop shadow                      */
#define C_BORDER   211   /* dark window border                      */
#define C_INK      212   /* dark text                               */
#define C_DIGIT    213   /* colour of the pi digits                 */
#define C_RINGBG   214   /* empty part of the progress ring         */
#define C_HILITE   215   /* bevel highlight                         */
#define C_LOWLITE  216   /* bevel shadow                            */
#define C_STAR     217   /* little stars on the desktop             */

extern uint8_t gfx_bb[GFX_W * GFX_H];     /* back buffer we draw into */
void gfx_init(void);                      /* call once at boot */
void gfx_enter(void);                     /* switch to graphics mode */
void gfx_leave(void);                     /* back to text mode */
bool gfx_active(void);
void gfx_present(void);                   /* copy back buffer to screen */
void gfx_clear(uint8_t c);
void gfx_pixel(int x, int y, uint8_t c);
void gfx_fill(int x, int y, int w, int h, uint8_t c);
void gfx_rect(int x, int y, int w, int h, uint8_t c);
void gfx_bevel(int x, int y, int w, int h, uint8_t light, uint8_t dark);
void gfx_vgrad(int x, int y, int w, int h, uint8_t base, int shades);
void gfx_hgrad(int x, int y, int w, int h, uint8_t base, int shades);
void gfx_circle(int cx, int cy, int r, uint8_t c);
void gfx_line(int x0, int y0, int x1, int y1, uint8_t c);
void gfx_char(int x, int y, char ch, uint8_t fg, int bg);   /* bg < 0: transparent */
void gfx_char2x(int x, int y, char ch, uint8_t fg);
void gfx_text(int x, int y, const char *s, uint8_t fg, int bg);
void gfx_text_center(int cx, int y, const char *s, uint8_t fg, int bg);
void gfx_desktop(void);                                    /* gradient + stars */
void gfx_window(int x, int y, int w, int h, const char *title);

/* ------------------------------------------------------------------ */
/* pi.c / pi_gui.c - the three pi programs                             */
/* ------------------------------------------------------------------ */
#define PI_MAX_DIGITS 10000
enum { MODE_BASE = 0, MODE_TEXT = 1, MODE_GUI = 2, MODE_COUNT = 3 };

#define PI_INTERACTIVE 0x01   /* wait for a key at the end (GUI)          */
#define PI_QUIET       0x02   /* don't print a summary afterwards         */
#define PI_NORECORD    0x04   /* don't save in the results history        */
#define PI_CLEAR       0x08   /* clear the screen first (text mode)       */

typedef struct {
    int      mode;
    uint32_t digits;
    uint64_t cycles;
    uint64_t us;
    uint32_t checksum;
} run_result_t;

const char  *pi_mode_name(int mode);
const char  *pi_mode_short(int mode);
run_result_t pi_run(int mode, uint32_t digits, int flags, const char *subtitle);
const char  *pi_last_digits(void);        /* "31415926..." from last run */
int          pi_history(const run_result_t **out);
void         pi_history_clear(void);

void pigui_begin(uint32_t n, const char *subtitle, uint64_t t0);
void pigui_digit(uint32_t index, uint8_t digit);
void pigui_end(uint32_t n, uint64_t us, bool interactive);

/* ------------------------------------------------------------------ */
/* bench.c and shell.c                                                 */
/* ------------------------------------------------------------------ */
void cmd_bench(int argc, char **argv);
void cmd_results(int argc, char **argv);
void cmd_graph(int argc, char **argv);
void shell_run(void);

/* kernel.c */
extern uint32_t mem_total_kb;
void panic(const char *msg);

#endif
