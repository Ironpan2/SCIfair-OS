#!/usr/bin/env python3
"""Reference digits of pi using Machin's formula with big integers:
    pi = 16*arctan(1/5) - 4*arctan(1/239)
Usage: ref_pi.py N  -> prints the first N digits ("31415...")."""
import sys


def arctan_inv(x, unity):
    total = term = unity // x
    x2, n, sign = x * x, 3, -1
    while term:
        term //= x2
        total += sign * (term // n)
        sign, n = -sign, n + 2
    return total


n = int(sys.argv[1]) if len(sys.argv) > 1 else 1000
if hasattr(sys, "set_int_max_str_digits"):
    sys.set_int_max_str_digits(0)
unity = 10 ** (n + 20)
pi = 4 * (4 * arctan_inv(5, unity) - arctan_inv(239, unity))
print(str(pi)[:n])
