/*
 * pi_core.c - calculating pi one digit at a time.
 *
 * This is the "spigot" algorithm by Stanley Rabinowitz and Stan Wagon (1995).
 * Like water dripping from a tap (a spigot), digits of pi come out one at a
 * time, using only whole-number maths - no decimals or fractions needed.
 *
 * The idea: pi can be written as
 *     pi = 2 + 1/3 * (2 + 2/5 * (2 + 3/7 * (2 + 4/9 * (2 + ...))))
 * which is like a number written in a strange "mixed base". Each pass of the
 * loop multiplies everything by 10 and carries from right to left, which
 * pushes the next decimal digit of pi out of the left end.
 *
 * Sometimes a digit can't be decided yet (if a 9 might later become a 10 and
 * carry), so digits are held back in "predigit" and "nines" until certain.
 *
 * The amount of work grows with the square of the number of digits:
 * twice as many digits takes about four times as long.
 */
#include "pi_core.h"

void pi_spigot(uint32_t n, uint32_t *a, pi_emit_fn emit)
{
    uint32_t total = n + PI_GUARD;         /* compute a few extra digits */
    uint32_t len = (10u * total) / 3u + 1u;
    uint32_t emitted = 0;
    uint32_t nines = 0, predigit = 0;
    int have_predigit = 0;

/* hand a finished digit to the caller (only the first n of them) */
#define OUT(d) do { if (emitted < n) emit(emitted, (uint8_t)(d)); emitted++; } while (0)

    for (uint32_t i = 0; i < len; i++)
        a[i] = 2;

    for (uint32_t j = 0; j < total && emitted < n; j++) {
        uint32_t q = 0;

        /* multiply by 10 and carry from right to left */
        for (uint32_t i = len; i > 0; i--) {
            uint32_t x = 10u * a[i - 1] + q * i;
            uint32_t d = 2u * i - 1u;
            a[i - 1] = x % d;
            q = x / d;
        }
        a[0] = q % 10u;
        q /= 10u;

        if (q == 9) {
            nines++;                        /* can't decide yet */
        } else if (q == 10) {
            OUT(predigit + 1);              /* a carry turns ...499 into ...500 */
            for (; nines; nines--)
                OUT(0);
            predigit = 0;
        } else {
            if (have_predigit)
                OUT(predigit);
            have_predigit = 1;
            predigit = q;
            for (; nines; nines--)
                OUT(9);
        }
    }
    if (emitted < n)
        OUT(predigit);
#undef OUT
}
