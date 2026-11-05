# The number model

**What.** A number is an exact decimal: digits and a power of ten, `mantissa × 10^exp10`. In the engine it
is `ibNumber` (`core/fnumber.h`), the engine's own type beside `ibString` and `ibDateTime`: 8 bytes. Up to
about fourteen digits and an exponent within ±32767 it lives in that word and asks the heap for nothing;
past that the word points to a shared big number that grows as its digits do. The arithmetic is done on
decimal digits in the core, with no library under it.

**Why.** Money is decimal. `0.1` has no binary floating-point form, so a `double` adds three tenths into
`0.30000000000000004`, and a column of cents drifts as it is summed. The database stores `NUMERIC(p,s)` - a
decimal; the value that carries it between the base and a script has to be one too, or every read is a
rounding.

## The reading

- **Zero is all-zero bits.** A zeroed word is the number 0, so a value keeps a number in its union beside
  pointers, with no constructor to run.
- **One number, however it is written.** `0.1`, `0.10` and `0.1000000` are equal, and comparisons,
  grouping keys and totals see one value. Text drops the zeros after the point (`15 / 5` prints `3`); a
  whole number keeps its own (`1000000`).
- **Addition, subtraction and multiplication are exact.** Every digit of the result is kept.
- **Division is the one bounded step.** A quotient carries the dividend's own fractional digits and
  fifteen more, the last of them rounded half away from zero: `2 / 3` is `0.666666666666667`, the nearest
  number of that length. Measured on the normalised operands, so equal operands give equal quotients. In a
  chain of divisions the room stops growing at 100 fractional digits; the digits that arrived are never
  cut.
- **Division by zero is an error** the script hears, not an infinity and not a zero.
- **Root, logarithm, exponent, a fractional power** are computed in decimal to 34 fractional digits,
  seeded from a `double` and refined by Newton's iteration - not a `double`'s answer.
- **A copy is one word.** The heap tier is shared: a copy is one more owner, a write takes a copy of its
  own first. Two numbers are independent across threads; one number read by many threads is safe; one
  number written while it is read is not.

## Rounding

- **`Round(n, places, mode)`.** `RoundMode.Round15as20`, the default, rounds a half away from zero
  (`1.5` → `2`, `-1.5` → `-2`); `RoundMode.Round15as10` rounds a half towards zero (`1.5` → `1`,
  `1.6` → `2`). At most 32 places.
- **`Int(n)`** drops the fraction, towards zero.
- **A declared number rounds what it is given.** An attribute, a resource or a variable typed
  `Number(p, s)` takes a value rounded to `s` places by the default rule. Precision 0 means "no limit" and
  rounds nothing.

## The doors

- **Text.** `FromString` reads `123`, `-1.5`, `1.2e3`, with a dot and in any locale; a text is a number
  only when nothing but blanks is left after it. `ToString` writes the digits with a dot and no grouping.
- **A `double`.** Taken by its seventeen significant digits; `NaN` and the infinities become zero. A
  `double` comes in from outside (a window, a seed), never as the arithmetic.
- **The database.** `Number(p, s)` is a `NUMERIC(p,s)` column on Firebird and PostgreSQL; with no
  precision declared, `(18, s)`. Firebird's scaled integers are read and written by moving the decimal
  point, its `INT128` by the sixteen bytes, and a value that does not fit its column is refused, never
  wrapped.
- **Bytes.** `GetBuffer` / `SetBuffer`: sign, exponent, then the digits' limbs - exact both ways.

## Where it stops

- Division and the transcendental functions round; everything else is exact. A part divided out and
  multiplied back (`1 / 3 * 3`) can differ from the whole in its last place, so a sum is split by giving the
  remainder to one line, not by division alone.
- A fractional power of a negative number has no real answer and gives 0; a square root of one is an error.
- Fifteen digits past the dividend is the engine's rule, not the script's choice; a script that needs
  fewer rounds the quotient.
