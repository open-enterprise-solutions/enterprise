# ibNumber — exact-decimal number in 8 bytes

> **Scope:** the numeric core of the platform — `ibNumber` (`backend/fnumber.{h,cpp}`), the
> exact-decimal type behind `ibValue::m_fData`. A two-tier tagged number: an inline immediate
> that costs no heap, and a self-contained sign-magnitude bignum for everything larger. This is a
> MAP of code that **already exists**.
>
> **Companions:** [script-value-types.md](script-value-types.md) (where `ibValue` puts it),
> [value-audit.md](value-audit.md) (the value subsystem), [database-layer.md](database-layer.md)
> (SQL_INT128 interop), [serialization-io.md](serialization-io.md) (the chunk stream it writes to).
> The one-line version lives in [../CLAUDE.md](../CLAUDE.md) §2a.
>
> **Status:** landed and self-contained — the `ttmath::Big` dependency was **removed**; every
> algorithm (Add/Sub/Mul/Div/Compare, decimal I/O, transcendentals) lives in `fnumber.cpp`.
> Tests: `tests/test_number.cpp`. Benchmarks (DISABLED): `tests/bench_runtime.cpp` `NumberBench`.

---

## 1. Why not `double`, why not a plain bignum

Accounting needs **exact** decimal: `0.1 + 0.2` must be `0.3`, and a 20-digit total must not lose a
cent. `double` is out (binary fraction, rounding). A heavyweight arbitrary-precision library is
correct but pays a heap allocation and indirection on *every* number — most of which are small
integers (quantities, ids, counts, prices with two decimals).

`ibNumber` takes the third path: **one tagged 64-bit word**. The common case (a value that fits ~14
significant digits and a modest exponent) lives entirely inline — no heap, `sizeof(ibNumber) == 8`,
8-byte aligned (`static_assert` in the header). Only a value that outgrows that promotes to a
heap bignum, automatically. It is exact at both tiers; the inline tier is just the fast, allocation-
free representation of the values that fit.

---

## 2. The tagged word — two tiers in one `uint64_t`

`m_payload` is the whole object. Bit 0 is the tag:

| Bit 0 | Tier | Layout of the other 63 bits |
|---|---|---|
| **1** | **immediate** | `[63:17]` mantissa — 47-bit signed (±2⁴⁶ ≈ ±70 trillion, ~14 digits); `[16:1]` exp10 — 16-bit signed (±32767) |
| **0** | **heap** | the remaining bits are a pointer to a `BigImpl` |

Value (both tiers) = `mantissa × 10^exp10`. So `456` is `(456, 0)`, `0.01` is `(1, −2)` — both
immediate. `"765.3456754567765443343"` (22 digits) does not fit the 47-bit mantissa, so it lives
on the heap as a `BigImpl`. `IsHeap()` / `IsImmediate()` read bit 0; there is no separate type
field. `IsNan()` is a permanent `false` — an exact decimal has no NaN state; the method exists
only for API compatibility with the float world it replaced.

---

## 3. The immediate fast paths — common arithmetic never touches the heap

The reason the inline tier is worth the bit-packing: `+ − × ÷` and `Compare` **short-circuit** when
both operands are inline *integers* (`exp10 == 0`). The shared gate is `TryImmInts`
(`fnumber.h`), which hands back the two mantissas as `int64_t`:

```cpp
bool TryImmInts(const ibNumber& rhs, int64_t& a, int64_t& b) const {
    if (IsImmediate() && rhs.IsImmediate() && ImmExp() == 0 && rhs.ImmExp() == 0) {
        a = ImmMantissa(); b = rhs.ImmMantissa(); return true;
    }
    return false;
}
```

Each operator tries it first (`fnumber.cpp` — `+=` :650, `−=` :666, `*=` :684, `/=` :706,
`Compare` :740). The result is computed with a single native `int64` op and packed back inline —
no `BigImpl`, no allocation, no long division. A few nanoseconds for the case that dominates real
workloads (integer counts, ids, small money).

**Exactness is preserved, not traded for speed.** The fast path only *commits* when the result
still fits inline. Division is the sharp case: `/=` takes the fast path **only when the division is
exact** — `bm != 0 && am % bm == 0` (`fnumber.cpp:706`); any remainder falls through to the full
`BigImpl` path that computes the ~30-significant-digit quotient. So the inline result is always
**bit-identical** to what the heap path would have produced. Non-exact decimal division still pays
the full long-division cost — inherent to being exact, not a regression.

---

## 4. The heap tier — `BigImpl`, self-contained schoolbook bignum

`BigImpl` (`fnumber.cpp:69`, opaque to callers) is a **sign-magnitude** decimal bignum:

```cpp
struct ibNumber::BigImpl {
    std::vector<uint32_t> limbs;   // magnitude, little-endian base 2^32 (limbs[0] = LSB), trimmed
    bool                  negative; // sign; zero is canonicalised not-negative
    int32_t               exp;      // base-10 exponent (wider than the inline ±32767)
};
// value = (negative ? -1 : 1) * |limbs| * 10^exp
```

Arithmetic is textbook, on the limb vectors:

- **Add / Sub** — carry / borrow chains (`AddMag` / `SubMag`). The carry primitive is
  `_addcarry_u32` / `_subborrow_u32` (single `ADC` / `SBB`) on MSVC x86/x64, and a portable
  64-bit-wide fallback on clang/gcc/ARM — which x86 compilers pattern-match back to `ADC`/`SBB`,
  and which stays single-cycle add+shift on AArch64. **No platform header leaks past that one block**
  (`fnumber.cpp:17–55`).
- **Mul** — schoolbook `O(n·m)` (`MulMag`).
- **Div** — base-2 **long division**, `DivModMag` (`fnumber.cpp:203`), the one workhorse behind
  `/=`, `%=`, `Round`, `ToString`, and the transcendentals (many callers).

No dependency on ttmath, no external bignum. The inline bit-packing plus these routines are the
whole of `ibNumber`.

### Promotion / demotion

Automatic and symmetric. `StoreBig(src)` packs `src` inline if it fits the 47-bit mantissa /
16-bit exp, otherwise allocates a `BigImpl` and tags the pointer. `LoadBig(out)` materialises the
current value into a **stack** `BigImpl` (no extra heap) — this is how every `const` method operates
on a uniform representation without mutating or allocating. A heap value that shrinks back into
range (e.g. a subtraction) demotes to immediate on the next `StoreBig`.

---

## 5. Conversions, rounding, formatting

- **Out:** `ToInt` / `ToUInt` (truncate, clamp on overflow), `ToInt(int64_t&)` /
  `ToInt(uint64_t&)` (ttmath-compat: 0 ok / 1 overflow), `ToInt64` (throws on overflow / non-integer),
  `ToDouble` / `ToFloat` (lossy), `ToString` / `ToWString`.
- **Rounding:** `Round()` (nearest integer, half-away-from-zero), `Round(n)` (n decimals),
  `Trunc()` (toward zero).
- **Formatting:** `ToString(const Format&)` with `Format { fracDigits, precision, decimalSep,
  groupSep, groupSize, minIntDigits }` — decimal places, significant-digit cap, separators,
  thousand grouping, zero-padding (sign sits **outside** the pad: `-5` width 4 → `"-0005"`).

---

## 5a. Division — where an endless fraction stops

A quotient has to stop somewhere, and nothing in long division stops it: the algorithm produces digits
for exactly as long as it is asked to. So the length is decided **before** the division runs, from one
rule:

> **The dividend's own fractional digits, plus `kDivExtraDigits` (15). The last digit is rounded
> (half away from zero, the same rule `Round` uses), trailing zeros are trimmed, and
> `kMaxDivFracDigits` (100) is the stop for a chain.**

Four properties follow, and each of them was a defect before the rule was stated this way:

- **The input is never shortened.** The room is added on top of what the dividend already carries, so
  dividing a value with 32 digits does not quietly round it to a fixed width first. The ceiling only
  ever declines to invent more digits — it does not cut the ones that arrived.
- **The same division always answers the same.** Length is measured on the **normalised** operands
  (trailing decimal zeros dropped), because `0.10` and `0.1000000` are one number in two spellings.
  Measured as written, they would produce quotients of different length, which are then **not equal to
  each other** — and that inequality travels into comparisons, grouping keys and folded totals.
- **The divisor does not change the answer's length.** The room belongs to the dividend. (Before, the
  inflation happened before the divisor's exponent was subtracted, so a longer divisor silently
  produced a shorter quotient.)
- **A chain of divisions does not creep.** Trimming means `1/8` is `0.125`, not `0.125` followed by
  however many zeros the rule asked for — so the next division measures its room from real digits.

The remainder is not lost: `%` returns it exactly, so quotient and remainder still reconstruct the
dividend. Inflation is bounded by the RULE, never by the VALUE — a dividend of 10^1000 is not
multiplied out into a thousand-digit integer to hold decimal places a number that size has no use for.

Tests: `NumberDivision.*` in `tests/test_number.cpp`.

---

## 6. Transcendental / power math — exact tier, not `double`

`Pow`, `Sqrt`, `Ln`, `Exp`, `Log(base)` are computed on the **exact decimal tier** (~30 significant
digits, matching `operator/`), not by round-tripping through `double`:

- `Pow(int)` — exact repeated multiplication.
- `Sqrt` — Newton–Raphson (Heron), double-**seeded** then refined.
- `Exp` — Taylor series with `exp(x) = exp(x/2ⁿ)^(2ⁿ)` argument reduction.
- `Ln` — Newton on `exp(y) − x` (reuses `Exp`), double-seeded.
- `Log(base)` = `Ln(x)/Ln(base)`; `Pow(frac)` = `exp(n·ln x)`, `x > 0`.

`double` is used **only** to seed the iterations / for out-of-range fallbacks — the converged
result is decimal-precise.

---

## 7. Serialization + DB interop

- **Binary buffer** (`GetBuffer` / `SetBuffer`), little-endian, round-trip exact:
  `[1] sign · [4] exp10 (int32) · [4] limb_count (uint32) · [4·N] magnitude limbs`. Minimum 9 bytes.
  Two `GetBuffer` overloads — a returning one and an out-parameter one that reuses the caller's
  `wxMemoryBuffer` capacity for tight loops.
- **Chunked stream** — `GetBuffer(ibWriterMemory&)` / `SetBuffer(const ibReaderMemory&)` wrap the
  above in a chunk keyed by `kIbNumberChunk = 0x023456555` (`fnumber.cpp:1569`), so a number
  round-trips through the same chunk stream as the rest of serialization.
- **Compact zero** — zero serialises to a 0-byte buffer, no allocation.
- **128-bit raw** — `To128Bytes(uint8_t[16])` / `From128Bytes` in little-endian two's-complement,
  for DB columns like **Firebird `SQL_INT128`**. Caller ensures the value is integer (`Round`/`Trunc`
  or pre-scale by the column's scale factor).

---

## 8. Thread safety

- **Distinct instances are fully independent** — no shared global/static state, no hidden cache;
  operations on different objects from different threads are always safe.
- **Concurrent const-only access to one instance is safe** — every `const` method (`ToString`,
  `Compare`, `IsZero`, `ToInt`, serialization, …) reads `m_payload` and materialises a **local**
  `BigImpl` via `LoadBig`; it never mutates shared state.
- **Read+write, or two writes, to the same instance concurrently is NOT safe** — same contract as
  `std::vector` / `wxString`. Use one instance per worker, or a mutex.

---

## 9. Honest remainder

- **`IsNan()` is a permanent `false`.** A vestige of the float API it replaced — exact decimal has
  no NaN. Kept so call sites that probed for NaN compile; a caller relying on it to signal an error
  will never see one.
- **ttmath is gone from this type**, but the credit/attribution convention for any remaining ttmath
  usage elsewhere in the tree still stands (see project memory) — this doc concerns only `ibNumber`.
- **Non-exact division is inherently the slow path.** The immediate fast path (§3) covers exact
  integer division only; a decimal quotient with a remainder always pays full long division. That is
  the cost of exactness, not a missing optimisation.
- **A decoded number's exponent is bounded** (`kMaxDecodedExp10`, ±1e6) at both byte and text doors:
  past it the bytes are not a number and the decoder says so. Beyond that bound `ToString` answers in
  scientific notation rather than attempting the plain expansion — a number ALWAYS has a text. This
  exists because the plain form is built by RESERVING the string first, so a corrupt exponent raised
  `std::length_error` ("string too long") inside whoever asked for the text — typically the code
  composing an error message ABOUT that very number, which then lost the message it was carrying.
- **`exp10` ranges differ by tier** — ±32767 inline (16-bit), full `int32_t` on the heap. A value
  with an extreme exponent but a tiny mantissa still promotes to `BigImpl` purely to carry the
  exponent.
