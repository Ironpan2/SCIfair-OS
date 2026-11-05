/*
 * test_pi.c - checks the OS's pi algorithm on a normal computer.
 *
 * Usage: test_pi N   -> prints the first N digits of pi computed by
 *                       src/pi_core.c (the exact same code the OS runs).
 * `make test` compares the output against digits from tests/ref_pi.py.
 */
#include <stdio.h>
#include <stdlib.h>
#include "../src/pi_core.h"

static char *out;

static void emit(uint32_t index, uint8_t digit)
{
    out[index] = (char)('0' + digit);
}

int main(int argc, char **argv)
{
    uint32_t n = argc > 1 ? (uint32_t)strtoul(argv[1], 0, 10) : 1000;
    uint32_t *work = malloc(PI_WORK_CELLS(n) * sizeof(uint32_t));
    out = calloc(n + 1, 1);
    pi_spigot(n, work, emit);
    puts(out);
    return 0;
}
