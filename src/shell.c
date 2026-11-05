/*
 * shell.c - the command line.
 *
 * Reads a line of text, splits it into words, and runs the matching command.
 * Each "program" is really just a C function inside the kernel.
 */
#include "kernel.h"

#define LINE_MAX 128
#define MAX_ARGS 8
#define HIST_N   16

typedef struct {
    const char *name;
    const char *args;
    const char *help;
    void (*fn)(int argc, char **argv);
    int group;                 /* 0 = programs, 1 = experiment, 2 = system */
} command_t;

static const command_t *find_command(const char *name);
static void run_line(char *line);

/* ================================================================== */
/*  Programs                                                           */
/* ================================================================== */

static void cmd_hello(int argc, char **argv)
{
    (void)argc; (void)argv;
    static const uint8_t rainbow[] = { VGA_LIGHT_RED, VGA_YELLOW, VGA_LIGHT_GREEN,
                                       VGA_LIGHT_CYAN, VGA_LIGHT_BLUE, VGA_LIGHT_MAGENTA };
    const char *msg = "Hello, World!";
    for (int i = 0; msg[i]; i++) {
        term_setcolor(rainbow[i % 6], VGA_BLACK);
        kprintf("%c", msg[i]);
    }
    term_setcolor(VGA_LIGHT_GREY, VGA_BLACK);
    kprintf("\nGreetings from inside %s, a 32-bit operating system written in C.\n", OS_NAME);
}

static void cmd_echo(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
        kprintf("%s%s", argv[i], i + 1 < argc ? " " : "");
    kprintf("\n");
}

static void cmd_clear(int argc, char **argv)
{
    (void)argc; (void)argv;
    term_clear();
}

static void cmd_fib(int argc, char **argv)
{
    uint32_t n = 20;
    if (argc > 1 && (!parse_uint(argv[1], &n) || n < 1 || n > 93)) {
        kprintf("Usage: fib [1-93]\n");
        return;
    }
    uint64_t a = 0, b = 1;
    for (uint32_t i = 1; i <= n; i++) {
        kprintf("%2u: %llu\n", i, b);
        uint64_t c = a + b;
        a = b;
        b = c;
    }
}

static void cmd_primes(int argc, char **argv)
{
    static uint8_t composite[100001];
    uint32_t n = 100, count = 0;
    if (argc > 1 && (!parse_uint(argv[1], &n) || n < 2 || n > 100000)) {
        kprintf("Usage: primes [2-100000]\n");
        return;
    }
    /* Sieve of Eratosthenes: cross out every multiple of every prime. */
    memset(composite, 0, n + 1);
    for (uint32_t i = 2; i * i <= n; i++)
        if (!composite[i])
            for (uint32_t j = i * i; j <= n; j += i)
                composite[j] = 1;
    for (uint32_t i = 2; i <= n; i++) {
        if (!composite[i]) {
            kprintf("%u ", i);
            count++;
        }
    }
    kprintf("\n%u prime numbers up to %u.\n", count, n);
}

static void cmd_colors(int argc, char **argv)
{
    (void)argc; (void)argv;
    for (int bg = 0; bg < 8; bg++) {
        for (int fg = 0; fg < 16; fg++) {
            term_setcolor((uint8_t)fg, (uint8_t)bg);
            kprintf(" Ab ");
        }
        term_setcolor(VGA_LIGHT_GREY, VGA_BLACK);
        kprintf("\n");
    }
}

/* ================================================================== */
/*  The pi programs                                                    */
/* ================================================================== */

static bool get_digits(int argc, char **argv, uint32_t *n)
{
    *n = 1000;
    if (argc > 1 && (!parse_uint(argv[1], n) || *n < 1 || *n > PI_MAX_DIGITS)) {
        kprintf("Usage: %s [digits]   (1 to %u, default 1000)\n", argv[0], PI_MAX_DIGITS);
        return false;
    }
    return true;
}

static void cmd_pi_base(int argc, char **argv)
{
    uint32_t n;
    if (get_digits(argc, argv, &n))
        pi_run(MODE_BASE, n, 0, 0);
}

static void cmd_pi_text(int argc, char **argv)
{
    uint32_t n;
    if (get_digits(argc, argv, &n))
        pi_run(MODE_TEXT, n, 0, 0);
}

static void cmd_pi_gui(int argc, char **argv)
{
    uint32_t n;
    if (get_digits(argc, argv, &n))
        pi_run(MODE_GUI, n, PI_INTERACTIVE, 0);
}

/* ================================================================== */
/*  System commands                                                    */
/* ================================================================== */

static void cmd_help(int argc, char **argv);

static void cmd_about(int argc, char **argv)
{
    (void)argc; (void)argv;
    term_setcolor(VGA_LIGHT_CYAN, VGA_BLACK);
    kprintf("%s %s\n", OS_NAME, OS_VERSION);
    term_setcolor(VGA_LIGHT_GREY, VGA_BLACK);
    kprintf(
        "A tiny 32-bit x86 operating system written in C for a science fair.\n\n"
        "THE QUESTION: How much slower does a program get when it has to show\n"
        "its work as text, or in a fancy graphical interface (GUI)?\n\n"
        "THE TEST: three programs calculate the same digits of pi with the\n"
        "exact same maths (the Rabinowitz-Wagon spigot algorithm):\n"
        "   pibase - no display: digits are only stored in memory (control group)\n"
        "   pitext - prints every digit to this text console\n"
        "   pigui  - draws every digit in a window with a progress bar & ring\n"
        "The CPU's cycle counter times each one. 'bench' runs them all several\n"
        "times and compares the results.\n");
}

static void cmd_sysinfo(int argc, char **argv)
{
    (void)argc; (void)argv;
    uint32_t a, b, c, d;
    char vendor[13], brand[49];

    cpuid(0, &a, &b, &c, &d);
    memcpy(vendor, &b, 4);
    memcpy(vendor + 4, &d, 4);
    memcpy(vendor + 8, &c, 4);
    vendor[12] = '\0';

    brand[0] = '\0';
    cpuid(0x80000000u, &a, &b, &c, &d);
    if (a >= 0x80000004u) {
        uint32_t *p = (uint32_t *)brand;
        for (uint32_t leaf = 0x80000002u; leaf <= 0x80000004u; leaf++, p += 4)
            cpuid(leaf, &p[0], &p[1], &p[2], &p[3]);
        brand[48] = '\0';
    }
    const char *bp = brand;
    while (*bp == ' ')
        bp++;

    uint32_t up = timer_ticks() / 1000;
    kprintf("OS:          %s %s (32-bit, x86 protected mode)\n", OS_NAME, OS_VERSION);
    kprintf("CPU vendor:  %s\n", vendor);
    kprintf("CPU model:   %s\n", *bp ? bp : "(unknown)");
    kprintf("CPU speed:   %llu.%02llu MHz (measured with the timer at boot)\n",
            tsc_per_ms / 1000, (tsc_per_ms % 1000) / 10);
    kprintf("Memory:      %u MB\n", mem_total_kb / 1024);
    kprintf("Display:     VGA 80x25 text, 320x200 256-colour graphics\n");
    kprintf("Serial port: %s\n", serial_present() ? "COM1 (results are logged here)" : "not found");
    kprintf("Uptime:      %u min %u s\n", up / 60, up % 60);
}

static void cmd_uptime(int argc, char **argv)
{
    (void)argc; (void)argv;
    uint32_t ms = timer_ticks();
    kprintf("Up for %u min %u.%03u s (%u timer ticks)\n", ms / 60000,
            (ms / 1000) % 60, ms % 1000, ms);
}

/* time <command>: run any command and report how long it took */
static void cmd_time(int argc, char **argv)
{
    char line[LINE_MAX], shown[LINE_MAX], t[24];
    if (argc < 2) {
        kprintf("Usage: time <command> [args]   e.g.  time primes 5000\n");
        return;
    }
    if (strcmp(argv[1], "time") == 0) {
        kprintf("Nice try!\n");
        return;
    }
    size_t len = 0;
    for (int i = 1; i < argc; i++) {
        size_t l = strlen(argv[i]);
        if (len + l + 2 >= sizeof(line))
            break;
        memcpy(line + len, argv[i], l);
        len += l;
        line[len++] = ' ';
    }
    line[len ? len - 1 : 0] = '\0';

    memcpy(shown, line, sizeof(line));     /* run_line chops up 'line' */
    uint64_t t0 = rdtsc();
    run_line(line);
    uint64_t cyc = rdtsc() - t0;
    fmt_ms(t, sizeof(t), cycles_to_us(cyc));
    term_setcolor(VGA_LIGHT_GREEN, VGA_BLACK);
    kprintf("[time] '%s' took %s (%llu CPU cycles)\n", shown, t, cyc);
    term_setcolor(VGA_LIGHT_GREY, VGA_BLACK);
}

static void cmd_reboot(int argc, char **argv)
{
    (void)argc; (void)argv;
    kprintf("Rebooting...\n");
    sleep_ms(300);
    cpu_cli();
    for (int i = 0; i < 100000 && (inb(0x64) & 0x02); i++)
        ;
    outb(0x64, 0xFE);                 /* ask the keyboard controller to reset */
    struct { uint16_t l; uint32_t b; } __attribute__((packed)) none = { 0, 0 };
    __asm__ volatile ("lidt %0; int $3" : : "m"(none));   /* fallback: crash-reset */
}

static void cmd_shutdown(int argc, char **argv)
{
    (void)argc; (void)argv;
    kprintf("Shutting down...\n");
    sleep_ms(300);
    outw(0x604, 0x2000);              /* QEMU */
    outw(0xB004, 0x2000);             /* Bochs / old QEMU */
    outw(0x4004, 0x3400);             /* VirtualBox */
    term_setcolor(VGA_YELLOW, VGA_BLACK);
    kprintf("It is now safe to turn off your computer.\n");
    for (;;) {
        cpu_cli();
        cpu_hlt();
    }
}

/* ================================================================== */
/*  Command table                                                      */
/* ================================================================== */

static const command_t commands[] = {
    { "hello",    "",                "Say hello",                               cmd_hello,    0 },
    { "echo",     "<text>",          "Repeat what you type",                    cmd_echo,     0 },
    { "fib",      "[n]",             "First n Fibonacci numbers",               cmd_fib,      0 },
    { "primes",   "[n]",             "Prime numbers up to n",                   cmd_primes,   0 },
    { "colors",   "",                "Show the 16 text colours",                cmd_colors,   0 },
    { "pibase",   "[digits]",        "Pi, NO display while timing (control)",   cmd_pi_base,  1 },
    { "pitext",   "[digits]",        "Pi, printed as TEXT",                     cmd_pi_text,  1 },
    { "pigui",    "[digits]",        "Pi, in a fancy GUI window",               cmd_pi_gui,   1 },
    { "bench",    "[digits] [trials]", "Run the full experiment & compare",     cmd_bench,    1 },
    { "results",  "[clear]",         "Show every recorded run",                 cmd_results,  1 },
    { "graph",    "",                "Bar chart of the last bench",             cmd_graph,    1 },
    { "about",    "",                "What this project is about",              cmd_about,    2 },
    { "sysinfo",  "",                "CPU, memory and system info",             cmd_sysinfo,  2 },
    { "uptime",   "",                "Time since boot",                         cmd_uptime,   2 },
    { "time",     "<command>",       "Time how long any command takes",         cmd_time,     2 },
    { "clear",    "",                "Clear the screen",                        cmd_clear,    2 },
    { "help",     "",                "This list",                               cmd_help,     2 },
    { "reboot",   "",                "Restart the computer",                    cmd_reboot,   2 },
    { "shutdown", "",                "Turn off (QEMU/VirtualBox)",              cmd_shutdown, 2 },
};
#define N_COMMANDS (sizeof(commands) / sizeof(commands[0]))

static void cmd_help(int argc, char **argv)
{
    (void)argc; (void)argv;
    static const char *groups[] = { "Programs", "Pi experiment", "System" };
    char usage[32];
    for (int g = 0; g < 3; g++) {
        term_setcolor(VGA_LIGHT_CYAN, VGA_BLACK);
        kprintf("%s:\n", groups[g]);
        for (size_t i = 0; i < N_COMMANDS; i++) {
            if (commands[i].group != g)
                continue;
            ksnprintf(usage, sizeof(usage), "%s %s", commands[i].name, commands[i].args);
            term_setcolor(VGA_WHITE, VGA_BLACK);
            kprintf("  %-26s", usage);
            term_setcolor(VGA_LIGHT_GREY, VGA_BLACK);
            kprintf("%s\n", commands[i].help);
        }
    }
}

static const command_t *find_command(const char *name)
{
    for (size_t i = 0; i < N_COMMANDS; i++)
        if (strcmp(commands[i].name, name) == 0)
            return &commands[i];
    return 0;
}

/* Split a line into words and run the command. (Modifies the line.) */
static void run_line(char *line)
{
    char *argv[MAX_ARGS];
    int argc = 0;
    char *p = line;

    while (*p && argc < MAX_ARGS) {
        while (*p == ' ' || *p == '\t')
            *p++ = '\0';
        if (!*p)
            break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t')
            p++;
    }
    if (*p)                               /* too many words: cut off the rest */
        *p = '\0';
    if (argc == 0)
        return;

    const command_t *cmd = find_command(argv[0]);
    if (cmd) {
        cmd->fn(argc, argv);
    } else {
        term_setcolor(VGA_LIGHT_RED, VGA_BLACK);
        kprintf("Unknown command: %s", argv[0]);
        term_setcolor(VGA_LIGHT_GREY, VGA_BLACK);
        kprintf("  (type 'help' for a list)\n");
    }
}

/* ================================================================== */
/*  Line editor with history (use the up/down arrow keys)              */
/* ================================================================== */

static char history[HIST_N][LINE_MAX];
static int hist_count;

static void replace_line(char *buf, int *len, const char *with)
{
    while (*len > 0) {
        term_backspace();
        (*len)--;
    }
    for (; with[*len] && *len < LINE_MAX - 1; (*len)++) {
        buf[*len] = with[*len];
        term_putc(with[*len]);
    }
    buf[*len] = '\0';
}

static void read_line(char *buf)
{
    int len = 0, hpos = hist_count;
    buf[0] = '\0';

    for (;;) {
        int k = kb_getkey();
        if (k == '\n') {
            term_putc('\n');
            buf[len] = '\0';
            break;
        } else if (k == '\b') {
            if (len > 0) {
                len--;
                term_backspace();
            }
        } else if (k == KEY_UP) {
            if (hpos > 0 && hpos > hist_count - HIST_N) {
                hpos--;
                replace_line(buf, &len, history[hpos % HIST_N]);
            }
        } else if (k == KEY_DOWN) {
            if (hpos < hist_count) {
                hpos++;
                replace_line(buf, &len, hpos == hist_count ? "" : history[hpos % HIST_N]);
            }
        } else if (k == KEY_CTRL_C) {
            kprintf("^C\n");
            len = 0;
            buf[0] = '\0';
            break;
        } else if (k == KEY_CTRL_L) {
            term_clear();
            buf[0] = '\0';
            len = 0;
            break;
        } else if (k >= 32 && k < 127 && len < LINE_MAX - 1) {
            buf[len++] = (char)k;
            term_putc((char)k);
        }
    }
    buf[len] = '\0';

    if (len > 0 && (hist_count == 0 ||
                    strcmp(history[(hist_count - 1) % HIST_N], buf) != 0)) {
        memcpy(history[hist_count % HIST_N], buf, (size_t)len + 1);
        hist_count++;
    }
}

void shell_run(void)
{
    char line[LINE_MAX];
    for (;;) {
        term_setcolor(VGA_LIGHT_GREEN, VGA_BLACK);
        kprintf("scifair");
        term_setcolor(VGA_WHITE, VGA_BLACK);
        kprintf("> ");
        term_setcolor(VGA_LIGHT_GREY, VGA_BLACK);
        read_line(line);
        run_line(line);
    }
}
