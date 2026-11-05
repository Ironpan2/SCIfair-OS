/*
 * keyboard.c - reading the keyboard.
 *
 * Every time a key is pressed or released, the keyboard sends a "scan code"
 * and fires IRQ 1. We turn scan codes into letters and put them in a small
 * queue that the shell reads from.
 *
 * Characters typed into the serial port are accepted too, which lets you
 * drive the OS from your computer's terminal when using QEMU.
 */
#include "kernel.h"

/* US keyboard layout, scan code set 1 */
static const char sc_normal[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ', 0,
};
static const char sc_shift[128] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' ', 0,
};

#define KBUF 64
static volatile uint16_t kbuf[KBUF];
static volatile uint8_t khead, ktail;
static bool shift, ctrl, caps, e0;

static void kb_push(uint16_t k)
{
    uint8_t next = (uint8_t)((khead + 1) % KBUF);
    if (next != ktail) {
        kbuf[khead] = k;
        khead = next;
    }
}

static void kb_irq(regs_t *r)
{
    (void)r;
    uint8_t sc = inb(0x60);

    if (sc == 0xE0) {          /* extended key (arrows etc.) follows */
        e0 = true;
        return;
    }
    bool release = sc & 0x80;
    uint8_t code = sc & 0x7F;

    if (e0) {
        e0 = false;
        if (code == 0x1D) { ctrl = !release; return; }
        if (release) return;
        switch (code) {
        case 0x48: kb_push(KEY_UP); break;
        case 0x50: kb_push(KEY_DOWN); break;
        case 0x4B: kb_push(KEY_LEFT); break;
        case 0x4D: kb_push(KEY_RIGHT); break;
        case 0x1C: kb_push('\n'); break;     /* keypad Enter */
        }
        return;
    }

    switch (code) {
    case 0x2A: case 0x36: shift = !release; return;
    case 0x1D: ctrl = !release; return;
    case 0x3A: if (!release) caps = !caps; return;
    }
    if (release)
        return;

    char c = shift ? sc_shift[code] : sc_normal[code];
    if (caps && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
        c ^= 0x20;                           /* flip upper/lower case */
    if (ctrl) {
        if (c == 'c' || c == 'C') kb_push(KEY_CTRL_C);
        if (c == 'l' || c == 'L') kb_push(KEY_CTRL_L);
        return;
    }
    if (c)
        kb_push((uint8_t)c);
}

void kb_init(void)
{
    while (inb(0x64) & 1)        /* throw away anything already waiting */
        (void)inb(0x60);
    irq_install(1, kb_irq);
}

/* Turn bytes from the serial port into keys (handles Enter, Backspace and
   the arrow-key escape sequences that terminals send). */
static int serial_key(char ch)
{
    static int esc;              /* 0 normal, 1 got ESC, 2 got ESC [ */
    static char last;
    uint8_t c = (uint8_t)ch;

    if (esc == 1) { esc = (c == '[') ? 2 : 0; return -1; }
    if (esc == 2) {
        esc = 0;
        switch (c) {
        case 'A': return KEY_UP;
        case 'B': return KEY_DOWN;
        case 'C': return KEY_RIGHT;
        case 'D': return KEY_LEFT;
        }
        return -1;
    }
    char prev = last;
    last = (char)c;
    if (c == 27) { esc = 1; return -1; }
    if (c == '\r') return '\n';
    if (c == '\n') return prev == '\r' ? -1 : '\n';
    if (c == 0x7F || c == 0x08) return '\b';
    if (c == 0x03) return KEY_CTRL_C;
    if (c == 0x0C) return KEY_CTRL_L;
    if (c >= 32 && c < 127) return c;
    if (c == '\t') return c;
    return -1;
}

bool kb_haskey(void)
{
    return ktail != khead || serial_has_char();
}

int kb_getkey(void)
{
    for (;;) {
        if (ktail != khead) {
            uint16_t k = kbuf[ktail];
            ktail = (uint8_t)((ktail + 1) % KBUF);
            return k;
        }
        if (serial_has_char()) {
            int k = serial_key(serial_getc());
            if (k >= 0)
                return k;
            continue;
        }
        cpu_hlt();               /* sleep until the next interrupt */
    }
}

void kb_flush(void)
{
    cpu_cli();
    ktail = khead;
    cpu_sti();
    while (serial_has_char())
        (void)serial_getc();
}
