/*
 * cpu.c - setting up the CPU's tables.
 *
 *  GDT (Global Descriptor Table): tells the CPU how memory is divided into
 *       "segments". We just use one big flat 4 GB segment for code and data.
 *  IDT (Interrupt Descriptor Table): tells the CPU which function to run for
 *       each interrupt (keyboard, timer, errors...).
 *  PIC (Programmable Interrupt Controller): the chip that collects hardware
 *       interrupts (IRQs) and forwards them to the CPU.
 */
#include "kernel.h"

/* ---------------- GDT ---------------- */
struct gdt_entry {
    uint16_t limit_lo;
    uint16_t base_lo;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  gran;
    uint8_t  base_hi;
} __attribute__((packed));

struct table_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct gdt_entry gdt[3];

static void gdt_set(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran)
{
    gdt[i].limit_lo = (uint16_t)(limit & 0xFFFF);
    gdt[i].base_lo  = (uint16_t)(base & 0xFFFF);
    gdt[i].base_mid = (uint8_t)((base >> 16) & 0xFF);
    gdt[i].access   = access;
    gdt[i].gran     = (uint8_t)(((limit >> 16) & 0x0F) | (gran & 0xF0));
    gdt[i].base_hi  = (uint8_t)(base >> 24);
}

void gdt_init(void)
{
    gdt_set(0, 0, 0, 0, 0);                  /* required null entry  */
    gdt_set(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);   /* 0x08: kernel code     */
    gdt_set(2, 0, 0xFFFFFFFF, 0x92, 0xCF);   /* 0x10: kernel data     */

    struct table_ptr p = { sizeof(gdt) - 1, (uint32_t)(uintptr_t)gdt };
    __asm__ volatile (
        "lgdt %0\n\t"
        "ljmp $0x08, $1f\n"
        "1:\n\t"
        "movw $0x10, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        "movw %%ax, %%ss\n\t"
        : : "m"(p) : "eax", "memory");
}

/* ---------------- IDT ---------------- */
struct idt_entry {
    uint16_t off_lo;
    uint16_t sel;
    uint8_t  zero;
    uint8_t  flags;
    uint16_t off_hi;
} __attribute__((packed));

static struct idt_entry idt[256];
extern uint32_t isr_stub_table[48];
static irq_handler_t irq_handlers[16];

void idt_init(void)
{
    for (int i = 0; i < 48; i++) {
        uint32_t a = isr_stub_table[i];
        idt[i].off_lo = (uint16_t)(a & 0xFFFF);
        idt[i].sel    = 0x08;
        idt[i].zero   = 0;
        idt[i].flags  = 0x8E;            /* present, ring 0, interrupt gate */
        idt[i].off_hi = (uint16_t)(a >> 16);
    }
    struct table_ptr p = { sizeof(idt) - 1, (uint32_t)(uintptr_t)idt };
    __asm__ volatile ("lidt %0" : : "m"(p) : "memory");
}

/* ---------------- PIC ---------------- */
#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

void pic_init(void)
{
    /* Move IRQ 0-15 to interrupts 32-47 so they don't clash with the
       CPU's own exceptions (0-31). */
    outb(PIC1_CMD, 0x11);  io_wait();
    outb(PIC2_CMD, 0x11);  io_wait();
    outb(PIC1_DATA, 0x20); io_wait();
    outb(PIC2_DATA, 0x28); io_wait();
    outb(PIC1_DATA, 0x04); io_wait();
    outb(PIC2_DATA, 0x02); io_wait();
    outb(PIC1_DATA, 0x01); io_wait();
    outb(PIC2_DATA, 0x01); io_wait();
    outb(PIC1_DATA, 0xFF);               /* mask everything for now */
    outb(PIC2_DATA, 0xFF);
}

void irq_install(int irq, irq_handler_t handler)
{
    irq_handlers[irq] = handler;
    if (irq < 8) {
        outb(PIC1_DATA, inb(PIC1_DATA) & (uint8_t)~(1 << irq));
    } else {
        outb(PIC2_DATA, inb(PIC2_DATA) & (uint8_t)~(1 << (irq - 8)));
        outb(PIC1_DATA, inb(PIC1_DATA) & (uint8_t)~(1 << 2));   /* cascade */
    }
}

/* ---------------- interrupt dispatch ---------------- */
static const char *exception_names[32] = {
    "Divide by zero", "Debug", "Non-maskable interrupt", "Breakpoint",
    "Overflow", "Bound range exceeded", "Invalid opcode", "Device not available",
    "Double fault", "Coprocessor segment overrun", "Invalid TSS",
    "Segment not present", "Stack-segment fault", "General protection fault",
    "Page fault", "Reserved", "x87 floating-point error", "Alignment check",
    "Machine check", "SIMD floating-point error", "Virtualization error",
    "Control protection error", "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Hypervisor injection", "VMM communication",
    "Security exception", "Reserved"
};

void isr_handler(regs_t *r)
{
    if (r->int_no >= 32 && r->int_no < 48) {
        int irq = (int)r->int_no - 32;

        /* "Spurious" IRQ 7 / 15: electrical noise, not a real interrupt. */
        if (irq == 7) {
            outb(PIC1_CMD, 0x0B);
            if (!(inb(PIC1_CMD) & 0x80))
                return;
        }
        if (irq == 15) {
            outb(PIC2_CMD, 0x0B);
            if (!(inb(PIC2_CMD) & 0x80)) {
                outb(PIC1_CMD, 0x20);
                return;
            }
        }

        if (irq_handlers[irq])
            irq_handlers[irq](r);
        if (irq >= 8)
            outb(PIC2_CMD, 0x20);        /* "end of interrupt" */
        outb(PIC1_CMD, 0x20);
        return;
    }

    /* A CPU exception means a bug: show a crash screen and stop. */
    if (gfx_active())
        gfx_leave();
    serial_mirror = true;
    term_setcolor(VGA_WHITE, VGA_RED);
    term_clear();
    kprintf("\n  *** KERNEL PANIC ***\n\n");
    kprintf("  CPU exception %u: %s (error code %u)\n\n", r->int_no,
            r->int_no < 32 ? exception_names[r->int_no] : "?", r->err_code);
    kprintf("  EIP=%08x  CS=%04x  EFLAGS=%08x\n", r->eip, r->cs, r->eflags);
    kprintf("  EAX=%08x  EBX=%08x  ECX=%08x  EDX=%08x\n", r->eax, r->ebx, r->ecx, r->edx);
    kprintf("  ESI=%08x  EDI=%08x  EBP=%08x\n", r->esi, r->edi, r->ebp);
    kprintf("\n  The system has been halted. Restart the computer.\n");
    for (;;) {
        cpu_cli();
        cpu_hlt();
    }
}
