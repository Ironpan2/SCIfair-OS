/*
 * kernel.c - where the operating system starts.
 *
 * boot.s jumps here once the bootloader has loaded us. We set up each piece
 * of hardware one by one, then start the shell.
 */
#include "kernel.h"

uint32_t mem_total_kb;

struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;    /* KB of memory below 1 MB */
    uint32_t mem_upper;    /* KB of memory above 1 MB */
};

void panic(const char *msg)
{
    if (gfx_active())
        gfx_leave();
    term_setcolor(VGA_WHITE, VGA_RED);
    kprintf("\nKERNEL PANIC: %s\n", msg);
    for (;;) {
        cpu_cli();
        cpu_hlt();
    }
}

static void ok(const char *what)
{
    term_setcolor(VGA_LIGHT_GREY, VGA_BLACK);
    kprintf("  [");
    term_setcolor(VGA_LIGHT_GREEN, VGA_BLACK);
    kprintf(" OK ");
    term_setcolor(VGA_LIGHT_GREY, VGA_BLACK);
    kprintf("] %s\n", what);
}

static void banner(void)
{
    term_setcolor(VGA_LIGHT_CYAN, VGA_BLACK);
    kprintf("\n");
    kprintf("   ____       _ _____     _             ___  ____  \n");
    kprintf("  / ___|  ___(_)  ___|_ _(_)_ __       / _ \\/ ___| \n");
    kprintf("  \\___ \\ / __| | |_ / _` | | '__|____| | | \\___ \\ \n");
    kprintf("   ___) | (__| |  _| (_| | | | |_____| |_| |___) |\n");
    kprintf("  |____/ \\___|_|_|  \\__,_|_|_|        \\___/|____/ \n");
    term_setcolor(VGA_YELLOW, VGA_BLACK);
    kprintf("\n  Version %s  -  How much does a GUI slow a program down?\n\n", OS_VERSION);
}

void kmain(uint32_t magic, uint32_t mbi_addr)
{
    term_init();
    serial_init();
    term_setcolor(VGA_WHITE, VGA_BLACK);
    kprintf("Starting %s...\n", OS_NAME);

    if (magic == 0x2BADB002) {
        const struct multiboot_info *mbi = (const struct multiboot_info *)(uintptr_t)mbi_addr;
        if (mbi->flags & 1)
            mem_total_kb = (mbi->mem_upper + 1024 + 1023) / 1024 * 1024;   /* round up to whole MB */
        ok("Loaded by a multiboot bootloader");
    }

    gdt_init();            ok("Memory segments (GDT)");
    idt_init();            ok("Interrupt table (IDT)");
    pic_init();            ok("Interrupt controller (PIC)");
    timer_init();          ok("Timer at 1000 Hz (PIT)");
    kb_init();             ok("Keyboard");
    cpu_sti();             /* interrupts on! */
    tsc_calibrate();
    kprintf("  [");
    term_setcolor(VGA_LIGHT_GREEN, VGA_BLACK);
    kprintf(" OK ");
    term_setcolor(VGA_LIGHT_GREY, VGA_BLACK);
    kprintf("] CPU clock measured: %llu.%02llu MHz\n", tsc_per_ms / 1000,
            (tsc_per_ms % 1000) / 10);
    gfx_init();            ok("VGA graphics (font saved)");

    serial_printf("# %s %s booted. CPU cycles per ms: %llu\n", OS_NAME, OS_VERSION, tsc_per_ms);

    banner();
    term_setcolor(VGA_LIGHT_GREY, VGA_BLACK);
    kprintf("  Type ");
    term_setcolor(VGA_WHITE, VGA_BLACK);
    kprintf("help");
    term_setcolor(VGA_LIGHT_GREY, VGA_BLACK);
    kprintf(" for commands, or ");
    term_setcolor(VGA_WHITE, VGA_BLACK);
    kprintf("bench");
    term_setcolor(VGA_LIGHT_GREY, VGA_BLACK);
    kprintf(" to run the experiment.\n\n");

    shell_run();
}
