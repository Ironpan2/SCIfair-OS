/*
 * pi_core.h - the pi digit algorithm, shared by all three pi programs.
 *
 * This file has no OS-specific code, so it can also be compiled and tested
 * on a normal computer (see tests/test_pi.c).
 */
#ifndef PI_CORE_H
#define PI_CORE_H

#include <stdint.h>

/* Extra "guard" digits computed internally so the last digit is correct. */
#define PI_GUARD 12

/* Number of 32-bit work cells needed to compute n digits. */
#define PI_WORK_CELLS(n) ((10u * ((n) + PI_GUARD)) / 3u + 2u)

/* Called once for every digit, in order: index 0 is the "3". */
typedef void (*pi_emit_fn)(uint32_t index, uint8_t digit);

/* Compute the first n digits of pi. work must hold PI_WORK_CELLS(n) cells. */
void pi_spigot(uint32_t n, uint32_t *work, pi_emit_fn emit);

#endif
