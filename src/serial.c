/*
 * serial.c - the COM1 serial port.
 *
 * The serial port is an old-school way for computers to send text to each
 * other. In QEMU we can connect it to the terminal or to a file, which is a
 * handy way to save experiment results from inside the OS.
 */
#include "kernel.h"

#define COM1 0x3F8

static bool serial_ok;

void serial_init(void)
{
    outb(COM1 + 1, 0x00);    /* no interrupts */
    outb(COM1 + 3, 0x80);    /* set baud rate divisor... */
    outb(COM1 + 0, 0x01);    /* ...to 1 = 115200 baud */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);    /* 8 bits, no parity, one stop bit */
    outb(COM1 + 2, 0xC7);    /* enable FIFO */
    outb(COM1 + 4, 0x1E);    /* loopback mode to test the chip */
    outb(COM1 + 0, 0xAE);
    if (inb(COM1 + 0) != 0xAE) {
        serial_ok = false;   /* no working serial port */
        return;
    }
    outb(COM1 + 4, 0x0F);    /* normal operation */
    serial_ok = true;
}

bool serial_present(void) { return serial_ok; }

void serial_putc(char c)
{
    if (!serial_ok)
        return;
    for (int timeout = 100000; timeout > 0; timeout--)
        if (inb(COM1 + 5) & 0x20)
            break;
    outb(COM1, (uint8_t)c);
}

void serial_write(const char *s)
{
    while (*s)
        serial_putc(*s++);
}

static void serial_sink(char c, void *ctx)
{
    (void)ctx;
    if (c == '\n')
        serial_putc('\r');
    serial_putc(c);
}

void serial_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    kvformat(serial_sink, 0, fmt, ap);
    va_end(ap);
}

bool serial_has_char(void)
{
    return serial_ok && (inb(COM1 + 5) & 0x01);
}

char serial_getc(void)
{
    return (char)inb(COM1);
}
