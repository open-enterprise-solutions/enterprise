# Runtime performance — hot-loop trim + benchmark harness

> **Scope:** the `ibProcUnit` interpreter hot loop + the `tests/bench_runtime.cpp`
> micro-benchmark harness. What was trimmed, why it is safe, how to measure, and where
> the numbers land against interpreters of the same class.
>
> **Status:** hot-loop trim + wall-time bench column, 2026-07-18. **The shortLet-peephole revival +
> string in-place append landed 2026-07-19 (§1b) — the string-concat O(n²) is gone (×1189 → ×68).**
> Numbers are Release|x64, MSVC 19.42, min-of-5 micro-bench (jitter ±3–5% — read orders of
> magnitude, not the last nanosecond).
>
> **Companions:** [ARCHITECTURE.md](ARCHITECTURE.md) (Bytecode Engine), [fnumber.md](fnumber.md)
> (`ibNumber`), [compiler-pipeline.md](compiler-pipeline.md).

---

## 1. The hot-loop trim (`compiler/procUnit.cpp`)

`ibProcUnit::Execute` runs one `switch (curCode.m_numOper)` per opcode. **Before** the switch
the loop header ran four per-opcode checks (`procUnit.cpp` ~539–564):

1. `ibBackendException::IsEvalMode()` — gates the `m_lCurLine` update. `IsEvalMode()` is
   `ibSession::Current()` + an acquire-load (`backend_exception.cpp:326`).
2. `cancelSession->IsForceExit()` — atomic acquire-load.
3. `cancelSession->IsCancelRequested()` — atomic acquire-load.
4. `debugServer != nullptr && !IsEvalMode()` — a **second** `IsEvalMode()`.

So every opcode paid `Current() + atomic` **twice** (checks 1 and 4) plus two more
acquire-loads (checks 2 and 3) — all of which run **always**, in production, on the hot path.

### The change

- **Cache `evalMode` once per `Execute`.** Eval-mode is a session-level flag the debugger
  toggles ONLY around a watch-eval (a separate nested `Execute`); it never changes within one
  `Execute` frame. Resolve it once in the prologue (`const bool evalMode = cancelSession &&
  cancelSession->IsEvalMode();` — `cancelSession` already IS `Current()`), then read the local
  in both checks. Removes two `Current()`-calls per opcode.
- **Poll cancel/force-exit every `kCancelPoll` (1024) opcodes**, not every one:
  `if (((++opTick & (kCancelPoll - 1)) == 0) && cancelSession) { … }`. `opTick` is global to the
  run, so even a 3-opcode tight loop is polled ~every 341 iterations. Removes two acquire-loads
  from ~4095 of every 4096 opcodes.

### Why it is safe

- **`evalMode` is constant per frame** — a nested call is a separate `Execute` with its own
  prologue; normal execution never calls `SetEvalMode` (only the debugger does, around a
  distinct top-level eval). `goto start_label` (post-exception re-entry) jumps *past* the
  already-run initialisation — legal, the `const` stays valid.
- **`m_lCurLine` still updates every opcode** (needed for the line number in error reports) —
  only its `IsEvalMode` source changed to the cache.
- **The debugger still fires every opcode** (breakpoints must check per line) — unchanged, just
  reads `!evalMode`. In runtime `debugServer == nullptr` → one predictable null-branch, ~free.
- **Cancel/force-exit stay cooperative** — they abort long-running loops, and any long loop has
  thousands of opcodes; a ~150 µs poll latency (1024 opcodes × ~136 ns) is invisible. Correctness
  is unchanged; only the poll *frequency* dropped.

### Result

−~10% on the **dispatch-bound** scenarios (`arith loop`, `LINQ`), noise elsewhere — see §3.
The win sits exactly where the always-run checks dominate; where per-instruction work is large
(call setup, marshalling, string alloc) it is invisible. In `oes_tests` the win is likely
*understated* (no live session → `Current()` cheaper, cancel atomics already `nullptr`-gated); in
real runtime with a session it should be ≥.

---

## 1b. shortLet peephole + string in-place append (2026-07-19)

The `string concat ×~1100` in §3 was **not** an inherent O(n²) — it was a dead compiler peephole
masking a missed optimisation.

- **The peephole.** `x = a op b` compiles to `OP tmp,a,b; LET x,tmp`. The `shortLet` peephole is
  meant to fuse that to `OP x,a,b` (drop the `LET`). A macro-precedence bug —
  `#define TYPE_DELTA1 1 * (OPER_END + 1)` without outer parens, so `m_numOper % TYPE_DELTA1` is
  `(m_numOper % 1) * N == 0` — made it match nothing. It was dead for **every** compound assignment,
  language-wide (see [compiler-pipeline.md](compiler-pipeline.md) §3.1). Parenthesising the macro
  revived it: every `x = a op b` now emits one fewer opcode + one fewer `ibValue` copy.
- **In-place string append.** With the `LET` gone, the fused `ADD` writes straight to the LHS slot,
  so in `s = s + expr` the dest and the left operand are the **same** `ibValue`. `AddValue`'s string
  branch detects that alias (`&dest == &left && dest is a live TYPE_STRING`) and appends onto `s` in
  place instead of building `s + expr` into a fresh string and copying it back — **O(n²) → O(n)**.
  The guard also checks `m_pStr != null`: a reused / moved-out slot can read `TYPE_STRING` with a
  null buffer, which falls back to the safe `SetString` path (`GetString()` is null-safe, so the
  result is `"" + expr`). A `cdb` session pinned exactly this null-buffer AV before the guard landed.
- **Result:** `string concat` **×1189 → ×67.8** (84.8 ns/append). The revival also shows up wherever
  compound assignments dominate a loop body: `arith loop` (`i = i + 1` + accumulate) **136 → 119
  ns/iter (−12%)**, on top of §1's dispatch trim (≈ −21% cumulative from the 150 ns raw baseline).
  Non-assignment loops (recursion / host / LINQ) and the direct `ibNumber` / `ibString` micro-ops are
  flat — as they must be, the fix touches assignment overhead, not arithmetic. 712/712 tests green;
  AOT format version 17 → 18 (`byteCodeAOT.cpp`) so cached bytecode recompiles to the fused form — old
  blobs still execute correctly, just without the optimisation.

---

## 2. Benchmark harness (`tests/bench_runtime.cpp`)

DISABLED-by-default gtest benches (kept out of the fast suite). Four groups:

| Group | Measures |
|---|---|
| `RuntimeBench` | the `ibProcUnit` interpreter — arith loop, recursion, host→script, string concat, LINQ |
| `NumberBench` | `ibNumber` directly — immediate vs heap tier, div, ToString/FromString, vs int64/double |
| `ParserBench` | `ibCompileCode::Compile` throughput (µs/compile, lines/s) |
| `IbStringBench` | `ibString` vs `wxString` (`bench_string.cpp`) |

Every OES figure prints next to a **native-C++ baseline** and the ratio, so the number reads as
an *overhead factor*, not raw ns tied to one CPU. The baseline is honest: it is written through a
`volatile g_sink` + `volatile` loop var, so `-O2` **cannot** elide the loop or fold it to a
closed form (Gauss). It measures working code, not a deleted loop.

### Wall-time column (added 2026-07-18)

`RuntimeBench` rows now also print `[run: oes=… native=…]` — the absolute time one full run of the
scenario took (`FmtWall` auto-scales ns/µs/ms/s). This turns the scary "×200" into a human unit:
*1 000 000 arith ops = 136 ms.* `Row`/`RowOes` gained optional total-ns params; the five
`RuntimeBench` scenarios capture the total before dividing by N.

### Run it

```
cmake --build build --target oes_tests --config Release
build/bin/Release/oes_tests --gtest_also_run_disabled_tests --gtest_filter=*Bench* --gtest_color=no
```

Release only — Debug numbers are meaningless (MSVC checked-iterators tax value-heavy code).

---

## 3. Results (Release x64, 2026-07-18; string concat + arith re-measured 2026-07-19, §1b)

### Interpreter — overhead + before/after (ns, dispatch trim)

| Scenario | before (3 runs) | after (2 runs) | overhead | one full run |
|---|---|---|---|---|
| arith loop (1M iters) | 150.7 / 154.8 / 147.9 | 135.7 / 136.0 → **119.0** (peephole §1b) | ×~200 → **×178** | 119 ms |
| LINQ build+pipe (10K) | 1111 / 1087 / 1013 | **946 / 982** | — | 9.6 ms |
| recursion Fib(28) (1.03M) | 371.6 / 380.5 / 370.9 | 369.8 / 348.0 | ×~220 | 359 ms |
| host→script (200K) | 413.7 / 397.6 / 378.1 | 379.5 / 388.3 | ×~215 | 77 ms |
| string concat Cat(2000) | 1427 (pre-peephole) | **84.8** (§1b) | ×~1100 → **×68** | 2.9 ms → **0.17 ms** |

`arith` and `LINQ` drop ~10% cleanly (both after-runs below all before-runs), and `arith` drops a
further ~12% (136 → 119) with the §1b peephole — its body is compound-assignment-dense. `recursion` /
`host` sit inside jitter — their bottleneck is not the dispatch checks. The `string concat` row
**was** an O(n²) outlier (×~1100, immutable-string alloc — the `String` vs `StringBuilder` trap);
the shortLet-peephole revival + in-place append (§1b) cut it to **×68 / 84.8 ns/append** — now O(n).

### `ibNumber` (8 bytes, exact decimal)

| op | oes | ×native | | op | oes | ×native |
|---|---|---|---|---|---|---|
| add immediate | 5.7 ns | 4.1 | | div exact (fast) | 5.7 ns | 2.6 |
| mul immediate | 5.8 ns | 4.1 | | div non-exact | 3016 ns | 2200 |
| compare | 3.1 ns | 1.7 | | ToString | 214 ns | 0.6 |
| mul 30×30 digits | 233 ns | — | | FromString | 116 ns | 1.9 |

Immediate fast-path works — 2–4× over `double`/`int64` for exact decimal. The gate is visible:
`div exact 5.7 ns` vs `div non-exact 3016 ns` (inherent base-2 long division, not a regression).
`ToString` beats native.

### `ibString` vs `wxString` (sizeof 32 vs 48)

| op | ib | wx | × | | op | ib | wx | × |
|---|---|---|---|---|---|---|---|---|
| ctor+dtor | 35 | 60 | 0.59 | | utf8 decode | 70 | 283 | 0.25 |
| copy | 14 | 38 | 0.38 | | find+mid | 17 | 49 | 0.34 |
| concat | 30 | 75 | 0.40 | | | | | |

Faster than `wxString` everywhere (2–4×) and 16 bytes lighter.

### Compiler — `ibCompileCode::Compile`

| module | time | lines/s |
|---|---|---|
| small (~15 lines) | 106 µs | 104K |
| large (200 funcs) | 10.1 ms | 98K |

~100K lines/s → a 200-function module compiles in ~10 ms. This is why the designer stays
responsive (recompile-on-edit for autocomplete / name-binding is milliseconds).

---

## 4. Positioning + acceleration ladder

The interpreter is a **bytecode VM with a boxed `ibValue`, no JIT** — same class as CPython,
Ruby YARV, and the business-scripting runtimes of this class (~50–300× over native C). Reference orders of magnitude (different machines,
approximate): ~2× slower than CPython (30 years of interpreter tuning), ~5–7× slower than Lua 5
(register-VM, the interpreter-speed benchmark), the same league as its peers or faster.

Assessment: **healthy for its class** — overhead is uniform across independent scenarios (a health
sign), and the absolute cost is trivial for a server-centric domain (heavy work — queries, volume —
lives in the C++ core, not the script; the script holds business rules).

⚠ The figures in this section predate LTCG (§5) and the ratios in them are stale in **both**
directions — see §5.3. Post-LTCG the interpreter runs ~16–24 ns per opcode on `arith`, which is
the CPython band rather than "2× behind it". One caveat belongs with any such comparison: CPython
and Lua loop over machine `int`/`double`, while every OES arithmetic op is **exact decimal**
(`ibNumber`). The like-for-like comparison is against Python's `decimal`, not its `int` — and that
one has never been measured here. Do it before quoting a position.

Acceleration ladder, **by need, not now** (profile gives no reason — the runtime is not, and is
unlikely to become, the bottleneck):

1. **Hot-loop check trim** — DONE (this doc), −10%, portable.
2. **Link-time code generation** — DONE 2026-08-08 (§5). The rung that was missing from this list
   entirely, and the largest single win so far: −18% arith, −16% method resolve, no source change.
3. **computed-goto dispatch** — +10–30% on GCC/Clang, but a GNU extension **MSVC does not support**
   (would need an `#ifdef` fork or a clang-cl build of `procUnit.cpp`).
4. **register-VM** — the no-JIT ceiling (Lua-style). A big rewrite (codegen + interpreter + AOT
   bump); note the operand addressing is already `(array, index)` three-address, i.e. register-style,
   so the main register-VM win (no operand stack traffic) is largely already present.
5. **JIT** — 1–5×, maximum cost.

---

## 5. Cross-TU calls — the rung this list was missing (2026-08-08)

The ladder above jumped from "trim the loop header" straight to "rewrite the VM", and skipped the
thing that turned out to matter most: **the hot loop leaves its translation unit on nearly every
opcode**, and until 2026-08-08 nothing let the compiler see across that boundary.

`ibProcUnit::Execute` lives in `procUnit.cpp`. The arithmetic helpers it calls (`AddValue`,
`ResolveRead`, …) are in the same file and were always visible — but the work they delegate to is
not: `ibNumber::Compare` / `operator+=` in `fnumber.cpp`, `ibByteCode::Find*` in `byteCode.cpp`.
A call the compiler cannot see through is not expensive because of the jump (~1 ns); it is
expensive because it **forces the optimiser to spill registers and re-read memory afterwards**,
and it cancels unrolling around itself.

### 5.1 LTCG (`CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE`)

Release x64 MSVC 19.42, min-of-4, quiet machine. Ranges did not overlap on any of the four:

| scenario | before | with LTCG | Δ |
|---|---|---|---|
| arith loop | 116.2 ns | **95.0** | −18% |
| method resolve | 297.8 ns | **251.1** | −16% |
| LINQ | 1029 ns | **936** | −9% |
| recursion | 364.1 ns | **345.2** | −5% |

`host->script` and `string concat` landed inside their own run-to-run spread and are **not**
claimed. Full `oes_tests` stayed green (1102 passed / 0 failed) — the check that matters, since
LTCG inlines and reorders across the module boundary that `~ibApplicationData`'s destruction
order, the inline registry variables and `ibMemberTable`'s pointer-to-member all depend on.

Enabled for **MSVC only** (`OES_ENABLE_LTO`, `CMakeLists.txt`): GCC/Clang would use `-flto`, whose
link-time cost against the CI budget has not been measured.

### 5.1a The one that mattered — operand resolution (2026-08-08)

LTCG was not the biggest win of the day; it was the one that pointed at the biggest.
Disassembling `ibProcUnit::Execute` in both builds showed the two most-called targets inside
it — **163 and 100 call sites** — surviving *even under whole-program optimisation*, while
`ibNumber::Compare` (7 sites) was inlined away. They were `ResolveWrite` / `ResolveRead`: the
functions behind the `variable1` / `cvariable2` macros, i.e. **one call per operand of every
opcode**.

Both carried `inline` and the compiler ignored it — a body holding
`ibBackendCoreException::Error` (varargs + gettext) is past any inliner's budget, and it dragged
a /GS stack-cookie prologue along. The common case is ONE line: a slot in the current frame.

Split (frame-local case inline, outer-frame walk + raises in `…Outer`) alone changed **nothing**:
`Execute` came out byte-for-byte identical, 27 232 bytes, same 263 calls — the size budget
declined a two-instruction body across 263 sites. Only `IB_FORCEINLINE` on top of the split did
it. Measured against the same-day baseline, min-of-4, no LTCG:

| scenario | before | after | Δ |
|---|---|---|---|
| arith loop | 116.2 | **71.7** | **−37%** |
| string concat | 89.2 | **62.5** | **−30%** |
| method resolve | 297.8 | **236.8** | **−19%** |
| recursion | 364.1 | **318.2** | −11% |
| LINQ | 1029 | **903** | −10% |

Overhead over native: **×175 → ×105**. Roughly **13.7 M script operations/sec**; a million
operations in 0.07 s. Suite green throughout (1102 passed). And it beat LTCG — which is the
point: this holds on every toolchain, with no flag and no 7-minute link.

A second, smaller pass moved 23 raise sites out of the loop body (the `_("…")` texts were
expanding into `wxString` + `wxFormatString` construction inside `Execute` — 163 wx calls
counted in the disassembly). `Execute` went 33 312 → 29 328 bytes, wx calls 163 → 112. Timing
gain was within noise; the reason to keep it is that the loop now reads as opcodes rather than
message formatting. Those texts moved into the error-code table the exception layer already
owned (`backend_exception.h` enum + `gs_listErrorString`), reached through one `Raise(code, …)`
entry point — see §5.5.

### 5.2 Hot/cold split — the portable half

LTCG is a build flag, so it does not hold where it is off. The same effect is bought permanently by
putting the **fast path** in the header and leaving the cold branch in the `.cpp` — the shape
`TryImmInts` already had, now applied to its callers (`ibNumber::Compare`) and to the primitives it
itself calls (`ImmMantissa`, `ImmExp` — both were out-of-line, so the "fast path" was paying four
cross-module calls per number operation to run a shift and a mask).

On top of LTCG this adds nothing (the linker already did it). Without LTO, all six scenarios
improved by 2–6%, but **run-to-run spread that day was 7–20% and the ranges overlap** — the honest
reading is "signal, not measurement": six of six improving by chance is ~1.6%, so the direction is
probably real while the magnitude is at the limit of what this harness can resolve.

### 5.3 Reading the numbers after LTCG

The `×native` column is **no longer comparable to earlier entries in this document**: LTCG
optimised the native baseline too (0.7–0.8 ns → 0.3 ns on `arith`), so ratios jumped to ×293–312
while OES itself got faster. Compare absolute nanoseconds, not the ratio.

### 5.5 One raise entry point

The runtime's own error texts used to be spelled inline as `_("…")` at ~20 sites inside
`Execute`. They now live in the table the exception layer already owned — the `enum` in
`backend_exception.h` and `gs_listErrorString` in `backend_exception.cpp`, kept in lock-step the
same way `s_listKeyWord` is — reached through one entry point in `procUnit.cpp`:

```cpp
template <class... Args> IB_NOINLINE void Raise(int code, Args&&... args);
IB_NOINLINE void Raise(int code, const ibValue& value);   // NOT folded in — see below
```

The second overload exists for one reason: it calls `GetString()` **inside itself**, off the hot
path. Forwarding an `ibValue` through the variadic form would put the `wxString` construction
back at the call site, inside the loop — which is what moving the raises out was for. (`ibValue`
has no implicit conversion to `wxString`, only `GetString()`, so the variadic form does not even
compile on it. Partial specialisation of function templates is illegal in C++; the overload IS
the idiomatic mechanism here.)

Two effects worth naming, neither of them speed: the loop reads as opcodes instead of message
formatting, and every runtime error is now visible as a list next to the compiler's — which is
also what makes them translatable in one place.

### 5.6 Call frame — measured, NOT fixed

The largest remaining gap, left deliberately as an arc rather than an evening's edit.

A call costs **335.8 ns** while an arithmetic opcode is ~15 ns. `ibRunContextSmall` holds
`ibValue m_cLocVars[MAX_STATIC_VAR]` (25) plus a pointer row, and `ibValue` has a virtual
destructor — so entering ANY function value-initialises 25 objects and leaving it runs 25
destructors, whatever the function declares.

Measured (`tests/bench_runtime.cpp`, `DISABLED_FrameCost`, min-of-4):

| | ns |
|---|---|
| 25 `ibValue` — what every call builds today | **149.8** |
| 3 `ibValue` — what a typical function uses | **20.5** |

**~45% of a call is spent on slots nobody reads.** Building only the used ones (raw storage +
placement new — the CPython 3.11 "zero-cost frames" move) would put recursion at ~206 ns (−39%)
and LINQ, which pays three calls per element, at ~520 ns (−43%). Bigger than everything landed
above, combined.

Two caveats that must travel with the number. The measurement is **indirect** —
`ibRunContextSmall`'s destructor is not exported, so a test TU cannot build the frame; what is
timed is its core, the `ibValue` array, not the pointer row alongside it. And the fix hands
lifetime management to us **inside the interpreter**, where exceptions are a working mechanism
(every `Raise`, every script `try`, every session cancel unwinds through a live frame). Today the
compiler destroys those 25 objects on unwind; with placement new that becomes our job. Needs its
own tests — an exception thrown mid-frame, and deep recursion — before it needs its optimisation.

### 5.4 What measuring this cost, methodologically

Nine rebuilds; two produced knowledge. The other seven tested guesses — a conversion, a scratch
buffer, where to declare an accessor — and all of those guesses were wrong. Two rules earned the
hard way:

- **Measure the harness before the change.** Spread was 7–20% on this machine; anything below ~10%
  is undecidable here, and no number of rebuilds fixes that.
- **Baseline the TARGET scenario first.** The `ibString`-on-name-resolution branch was abandoned
  after one clean measurement showed its own target case 4% *slower* — a measurement that could
  have been taken before writing any of it.
