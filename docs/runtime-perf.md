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

⚠ The figures in this section predate the 2026-08-08 work (§5) and are stale. What replaces them
is below — and unlike everything above, the comparison is now **measured on one machine** rather
than quoted from elsewhere.

### 4a. Measured against CPython, same machine (2026-08-08)

The scenario is identical in both: `while i < n: s = s + i; i = i + 1` — two arithmetic ops and a
comparison per iteration. Python 3.9.13, min-of-4; `tests/scripts` equivalent for OES.

| | ns/iteration | semantics |
|---|---|---|
| native C++ `int64` | **0.7** | machine integer |
| Python 3.9 `int` | **41.4** | arbitrary-precision integer |
| **OES** | **71.9** | **exact decimal** |
| Python 3.9 `decimal` | **95.1** | exact decimal |

**Like-for-like — against `decimal`, the type that actually matches `ibNumber` — OES is ~32%
faster than CPython.** Against Python's `int` it is 1.74× slower, but that type cannot represent
money without loss, so the comparison flatters the wrong side.

Worth recording that this reading is what the day's work bought: at the morning's 116.2 ns the
same row read *slower* than Python's `decimal` (95.1); it now reads faster.

Two caveats. Python **3.9**, not 3.11+ — the adaptive-specialising interpreter (3.11) would put
`int` near 30 ns and `decimal` near 70–80, which narrows the gap without reversing it. And this is
one scenario: strings, calls and collections have their own profiles.

Class placement, in ns per instruction, everything below from literature rather than measurement
here: Lua 5.4 5–10 (register VM), CPython 3.11+ 10–20, **OES ~12–18**, PHP 8 (no JIT) 15–30,
Ruby YARV (no JIT) 20–40. JIT engines (LuaJIT, JVM, .NET, V8) are a different class at ×1–10 over
native and are not reachable without one.

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

### 5.6 Call frame — LANDED 2026-08-08

The largest single win of the day, and the last one found by measuring rather than guessing.

A call costs **335.8 ns** while an arithmetic opcode is ~15 ns. `ibRunContextSmall` holds
`ibValue m_cLocVars[MAX_STATIC_VAR]` (25) plus a pointer row, and `ibValue` has a virtual
destructor — so entering ANY function value-initialises 25 objects and leaving it runs 25
destructors, whatever the function declares.

Measured (`tests/bench_runtime.cpp`, `DISABLED_FrameCost`, min-of-4):

| | ns |
|---|---|
| 25 `ibValue` — what every call builds today | **149.8** |
| 3 `ibValue` — what a typical function uses | **20.5** |

**~45% of a call was spent on slots nobody reads.**

Both frames (`ibRunContextSmall` and `ibRunContext`) now hold RAW storage —
`alignas(ibValue) unsigned char m_cLocStorage[sizeof(ibValue) * MAX_STATIC_VAR]` — and construct
only the declared slots via placement new. The pointer row lost its `= {}` for the same reason:
zeroing 25 pointers to use three is the same waste in miniature. This is the CPython 3.11
"zero-cost frames" move.

Result (min-of-4, no LTCG, against the state right before this change):

| scenario | before | after | Δ |
|---|---|---|---|
| host→script | 360.7 | **223.8** | **−38%** |
| recursion | 318.2 | **226.5** | **−29%** |
| LINQ | 903 | **711.9** | **−21%** |
| arith loop | 71.7 | 71.1 | control — barely calls |

Call overhead over native: **×220 → ×154** (recursion), **×205 → ×130** (host→script).

**Which frame matters, and why the first attempt showed nothing.** Applying this to
`ibRunContextSmall` alone moved not a single number — its call sites ask for
`std::max(argCount, MAX_STATIC_VAR)`, i.e. 25 regardless (`procUnit.cpp` OPER_CALL_METHOD /
OPER_NEW), so there was nothing to save. `OPER_CALL` asks for the function's REAL local count
(`ibRunContext cRunContext(index3)`), and that is where the win lives. If those two call sites are
ever revisited, the `max(...)` is the thing to question.

**Lifetime is ours now.** `DestroyLocals` is the single place that knows whether storage came from
the heap or the inline buffer; both destructors and `SetLocalCount` funnel through it. That
matters because an exception unwinding through a live frame is a working mechanism here — every
`Raise`, every script `try`, every session cancel — and the compiler no longer destroys those
slots for us. The suite (1102, including closure-capture and try/except paths) stayed green
across the change; a dedicated exception-mid-frame test would still be worth adding.

### 5.7 Method-call frame — sized by arity (2026-08-08)

`OPER_CALL_METHOD` built its frame as `std::max(argCount, MAX_STATIC_VAR)` — 25 slots per method
call whatever the arity — which is why §5.6 landed nothing on this path.

The 25 was **not** laziness. Method implementations index `paParams[0..GetNParams)` without
consulting the count they were handed (`ibValueArray::Add` does `*paParams[0]` outright), and the
check above them only catches too MANY arguments, never too few — so a slot the method can reach
has to exist and be empty rather than absent. But the method's own arity is the honest bound for
that, and it was already being fetched two lines below.

**LINQ 711.9 → 566.5 ns/element (−20%)**, ranges disjoint. The arithmetic checks out: `arr.Add(i)`
per element, 24 fewer `ibValue` at ~6 ns each ≈ 144 ns predicted, 145 ns observed.
`paramCount == wxNOT_FOUND` keeps the blanket 25 — with the arity unknown there is nothing better
to bound by.

Separately, and not a performance point: that implementations do not check the arity they were
given means correctness rests on the CALLER having sized the frame. The bound is exact now instead
of generous, so the guarantee is unchanged — but the check belongs in one place rather than
resting on a frame being oversized.

### 5.8 Where the call cost actually is

With both frames fixed, the three call-ish scenarios converge — and that convergence is the
finding:

| path | ns/call |
|---|---|
| script function call (`recursion`) | 226.5 |
| method call **with name resolution** (`arr.Get(0)`) | **220.0** |
| LINQ dispatch by enum id, no lookup (`arr.Count()`) | 244.6 |

**Name resolution does not dominate.** A method call that searches the hash index costs the same
as a plain function call that searches nothing. Whatever remains in those ~220 ns is the call
mechanism itself — argument marshalling, context setup, dispatch — not the lookup.

This retires the `ibString`-on-name-resolution idea for good: it optimised a lookup that is not
where the time goes. (It was abandoned earlier the same day for a *wrong* reason — the scenario
used to judge it called `arr.Count()`, an `ibLinqMethod`, which resolves no name at all. A bench
that measures something other than its name is worse than a missing one: a missing bench makes you
look, a mislabelled one makes you conclude.)

The remaining tier is the `ibValue` virtual destructor — ~6 ns per slot to build and tear down,
paid by every frame and every temporary. Dropping the vptr is a far larger change than anything
here and needs its own arc.

### 5.4 What measuring this cost, methodologically

Nine rebuilds; two produced knowledge. The other seven tested guesses — a conversion, a scratch
buffer, where to declare an accessor — and all of those guesses were wrong. Two rules earned the
hard way:

- **Measure the harness before the change.** Spread was 7–20% on this machine; anything below ~10%
  is undecidable here, and no number of rebuilds fixes that.
- **Baseline the TARGET scenario first.** The `ibString`-on-name-resolution branch was abandoned
  after one clean measurement showed its own target case 4% *slower* — a measurement that could
  have been taken before writing any of it.

### 5.9 Type-name resolution — the one the microbenchmarks could not see (2026-08-08)

The largest defect of the day, found only after the harness gained a scenario shaped like
GENERATED code rather than like a microbenchmark.

`ibCtorRegistry::Find(const wxString&)` resolved a type name by **scanning every registered ctor**
and comparing with `stringUtils::CompareString` — which takes `ToStdWstring()` of BOTH sides, i.e.
two heap allocations per comparison. With ~180 registered types that is ~360 allocations to answer
one question. And `OPER_NEW` asks it **at runtime, for every object a script creates**
(`procUnit.cpp` → `CreateObject(className, …)`).

The comment above the function said the opposite, which is why it survived: *"name -> LINEAR scan
(NOT very hot: compile-time resolution of CreateObject(Name))"*. An assumption written down as a
fact, never checked.

Fixed by giving the name its own index, keyed on the folded name (`m_byName`). Measured
(`DISABLED_RecordParts`, min-of-3):

| | before | after | factor |
|---|---|---|---|
| `New Structure` | 18 495.8 ns | **606.8** | **30.5×** |
| `New` + 2 × `Insert` | 27 557.8 | 4 086.3 | 6.7× |
| dot access on Structure | 1 399.5 | 536.1 | 2.6× |
| `Array.Get` | 608.3 | 217.8 | 2.8× |
| **one record, build + walk** | **35 892** | **7 280** | **4.9×** |

`ibCtorRegistry` is a template with two users — the value factory (`valueFactory.cpp`) and the
per-image metadata factory (`metaData.h`, `m_factoryCtors`) — so the same fix covers creating
metaobjects by type name, not just script values.

**Why six microbenchmarks missed it for a whole day.** None of them creates objects in a loop:
`arith`, `recursion` and `string concat` work on values that already exist, and LINQ builds its
array once. The cost was invisible by construction. It surfaced within minutes of adding a
scenario that builds records and walks them — the shape generated code actually has, and the shape
an AI-first platform will run most of.

The lesson is about the harness, not the registry: **a benchmark suite measures the shapes you
thought of.** Scenarios should be chosen from what the platform will really execute, not from what
stresses the interpreter most cleanly.

---

## 6. Full-suite reading, 2026-08-09 — a CONFIRMATION run

Taken after the process-wide-static cleanup of the same day (§6a). **Methodology is weaker than
every figure above it**: these are single runs, best of three, on a machine that was not quiet,
where §§1–5 are min-of-4/5 on a quiet one. Min-of-3 systematically loses to min-of-4, so read the
table as "nothing moved", not as a delta — a real before/after needs the pre-change binary built
on the same machine in the same hour, which was not done.

| | 2026-08-09 | previous entry | reading |
|---|---|---|---|
| arith loop (ns/iter) | 76.7 | 71.1 (§5.6) | within methodology gap; the ONLY line where the spread (±1.7%) is tighter than the difference — worth a real A/B before anyone calls it a regression |
| recursion (ns/call) | 243.2 | 226.5 | noise (±14% run-to-run) |
| host→script (ns/call) | 238.3 | 223.8 | noise |
| string concat (ns/append) | 70.0 | 63.1 | noise; the O(n) property (§1b) holds — ×42–48 over native, not ×1145 |
| LINQ build+pipe (ns/el) | 574.6 | 566.5 | unchanged |
| method resolve (ns/call) | 227.1 | 220.0 | unchanged |
| call frame (ns) | 151.2 (×6.9 over native) | §5.6 | unchanged |
| `New Structure` | 612.5 | 606.8 (§5.9) | unchanged |
| `New` + 2 × `Insert` | 3 874.3 | 4 086.3 | unchanged |
| dot access on Structure | 549.8 | 536.1 | unchanged |
| `Array.Add` / `Array.Get` | 211.6 / 224.4 | 217.8 (Get) | unchanged |
| record build + walk (ns/row) | 7 093 → 7 377 (n = 2 500 → 20 000) | 7 280 | unchanged, and **linear** — 4 % growth over an 8× row count |
| `ibNumber` add / mul / cmp / div-exact | ×3.2 / ×3.3 / ×1.4 / ×2.5 | §3 | unchanged |
| `ibNumber` ToString | ×0.6 (faster than `snprintf`) | §3 | unchanged |
| `ibString` vs `wxString` | ×0.55 / 0.37 / 0.38 / 0.24 / 0.35 | §3 | unchanged |
| compiler (200 funcs) | 142K / 160K / 167K / 161K lines/s | ~100K (§3, 2026-07-18) | see below |

**`ParserBench` is the noisiest instrument in the harness** — 18 % spread on the large module and
31 % on the small one between runs of the *same binary*. Any compile-side claim needs repeated runs;
a single figure from it means nothing. The move from ~100K to 140–167K lines/s spans three weeks of
work and cannot be attributed to any one change without an A/B.

**The dispatch lever is spent.** `DISABLED_DumpBytecode` shows the arith body compiles to five
opcodes — `LS, IF, ADD, ADD, GOTO`, and no codegen can emit fewer — so 76.7 ns / 5 ≈ **15.4 ns per
opcode**. It also shows the `shortLet` fusion alive (`ADD p1(0,1) p2(0,1)`: destination aliases the
left operand, §1b). With the headroom on threaded dispatch already down to ×1.3–1.5, further work
on *how* an opcode is dispatched has little left to give.

**Where the weight actually is.** Not dispatch, not arithmetic, not strings — `Structure`.
`Insert` costs ~1 630 ns against `Array.Add`'s 212 (**×8 for the same job**), and a field read
through the dot costs 550 ns, i.e. **36 opcodes' worth for one field access**. Record walk is
7.1–7.4 µs/row ≈ 135 K rows/s, which is the figure a report on real data will be judged by.

The mechanism is visible in `system/value/valueMap.h`: `ibValueContainer` stores
`std::map<const ibValue, ibValue, ContainerComparator>` — **the key is a full `ibValue`**, so every
field access boxes the name into a value (heap, for a string) and walks a tree whose every
comparison is an `ibValue` comparison. On top of that, each mutation calls `m_members.Invalidate()`,
dropping the dynamic-member table the dot goes through, which is then rebuilt lazily — and
build-then-read is exactly the record-walk shape. `Array` is faster because its key is an index: no
boxing, no comparator, no member table. **This, not the compiler, is the remaining runtime target.**

### 6a. Process-wide mutable statics — the cleanup measured above (2026-08-09)

Not a performance arc; a correctness one, on the same files. Lazy `if (empty()) Fill()` init of
shared tables is unsafe once sessions compile concurrently, which lazy compilation made real.

- **`ibTranslateCode::ms_listDefine`** — now `static const`, parent links are `const` pointers
  (see [compiler-pipeline.md](compiler-pipeline.md) §2).
- **The keyword table** — was a public mutable `ms_listHashKeyWord` that every `ibTranslateCode`
  ctor topped up behind `if (size() == 0) LoadKeyWords()`, where `LoadKeyWords` opens with
  `clear()`. A `wxModule` that appeared to preload it at startup declared neither
  `wxDECLARE_DYNAMIC_CLASS` nor `wxIMPLEMENT_DYNAMIC_CLASS`, so it was never registered and never
  ran — the table was in fact built by whichever ctor got there first. Now a build-once function
  static inside its one reader, `IsKeyWord`. Two sibling file-statics (`s_listHelpDescription`,
  `s_listHashKeyword`) were filled and never read: removed.
- **`gs_operPriority`** — same shape, worse consequence: a reader could see a slot the filler had
  not reached, and a zero priority does not crash, it mis-associates the expression. Now
  `constexpr`, built by the compiler; `InitializeCompileModule()` and its four call sites are gone.
- **`static ibValue cVal`** ×2 in `backend_spreadsheet.cpp` — scratch locals made static; an
  `ibValue` holding a `TYPE_REFFER` makes concurrent renders race on a refcount. Now ordinary
  locals.
- **`s_weededFor`** (`byteCodeCache.cpp`) — a `wxString` compare-and-set on every session's compile
  path; now under a mutex, with the DELETE outside the lock.

Two allocations left the compile path with it: `IsKeyWord` no longer upper-cases every identifier
into a throw-away `wxString` (case folding moved into the map comparator, `ibCaseFoldLess`), and
`FindDefine` does one `find` per scope instead of two linear scans of the map. Predicted to show up
in `ParserBench` and nowhere else — unverified, for the noise reason above.

---

## 7. After the AST arc was reverted (2026-08-10)

The tree-compiler arc was reverted the same day it was measured (§1e). The runtime work done
during it does not depend on the tree, so it was carried onto the canonical compiler as a patch
and re-measured here. **Method:** Release, min-of-5, machine quiet, nothing else building —
`oes_tests --gtest_also_run_disabled_tests --gtest_filter=*RuntimeBench*`.

| row | 2026-08-08 evening | 2026-08-10 final | Δ |
|---|---:|---:|---:|
| arith loop (ns/iter) | 71.1 | 73.4 | +3.2% |
| string concat (ns/app) | 63.1 | 65.3 | +3.5% |
| method resolve (ns/call) | 220.0 | 201.4 | −8.5% |
| host→script (ns/call) | 223.8 | **174.0** | **−22.3%** |
| recursion (ns/call) | 226.5 | **179.6** | **−20.7%** |
| LINQ build+pipe (ns/el) | 566.5 | **425.2** | **−24.9%** |

**Read it by the shape, not the size.** Every row that costs a CALL fell by about a fifth;
every row without one stands still (the +3% pair is inside run-to-run spread). That is the
signature of the change: all of it is frame-entry cost, not instruction cost.

- the empty `std::map` in every frame (`procContext.h` `m_listEval` → `std::vector`) — MSVC
  allocates the tree's sentinel node in the DEFAULT CONSTRUCTOR, so an empty map member is a heap
  allocation per call;
- eight `ibSession::Current()` per invocation → resolved once (`ibSession::PUStateOf`);
- `ibProcStackGuard` re-resolving the state per guard → resolved once;
- six copies of the argument-load loop → one `LOAD_ARG_CONST` macro.

Reproduced three times across the day, each min-of-5, with different builds in between. The
distribution held every time, which is what makes it a result rather than a session artefact.

**What it does NOT say.** The instruction-level standing is unchanged since 2026-08-08: `arith
loop` is the row that maps to "ns per instruction", and it did not move. Nothing here improves
the position against CPython — that was §5's work.

**A caveat on the ratio.** This harness reports `native=0.7 ns/iter` for the arithmetic loop, and
the derived "×N over native" therefore rests on a quantity measured worse than the thing it
divides: today it reads ×117 where 2026-08-08 recorded ×103. The per-row deltas are comparable
across days; the RATIO is not, until the native baseline is measured on its own terms.

**Still untouched by both days — the data path.** `record build+walk` is ~5300 ns/row,
`ibMemberTable::Build` is 25% of a join run (§1g), and the join index pays ×6.6 on probe for an
`ibValue` key against a `long` one (§1f-bis). One element of data still costs what thirty
interpreter iterations do.

---

## 8. The LINQ join is a nested loop (2026-08-11)

Release x64, quiet machine. The interpreter is unchanged — `arith loop` 71.8 ns/iter against the
71.1 / 73.4 recorded above, `string concat` 63.4 against 63.1 — so this section is about one line
only, and it is not a regression in anything measured before.

### The reading

| n | ns/row | total | **ns per compared pair** (÷n) |
|---:|---:|---:|---:|
| 250 | 9 414 | 2.35 ms | 37.7 |
| 1 000 | 33 449 | 33.4 ms | 33.4 |
| 4 000 | 130 484 | 522 ms | 32.6 |
| 16 000 | 546 829 | **8.75 s** | 34.2 |

The cost **per pair is constant**, so every outer row walks the whole inner side: a nested loop,
O(n²). The scaling loop was written as a discriminator and its own comment named the three outcomes
— flat (constant per-row work), +log N (tree comparisons), **linear in n (something is O(N) per row,
and THAT is the bug)**. The measurement landed on the third.

### What it is not

* **Not the container and not the comparator** — 33 ns per pair is already cheap.
* **Not the index**, which is measured in the same run and is nearly flat: an `ibValue` probe goes
  280.8 ns at n=2000 → 329.5 at n=16000 (×1.17 for ×8 of n); build 476.8 → 628.8. An indexed row at
  n=16000 would cost about **1 µs instead of 547** — roughly ×550. The machinery exists and the join
  does not use it.
* **Not a general degradation** — the control is flat: `record build+walk` reads 5436 / 5512 / 5581 /
  5584 ns at n = 2500 / 5000 / 10000 / 20000.

### The constant part, which dominates only at small n

Three lambda invocations per row (~90 ns each: `one lambda` 104 ns/el against `no lambda` 8.4) plus
one composite row: `New + 2 Insert` 3264 ns, `struct build 3 fields` 1603 ns, field read 331 ns. At
n=250 that is 17–35 % of a row; at n=16000 it is 0.5 %.

### The ×16 gap against the bench's own comment — and why it is NOT a regression

The comment beside this bench records **3973 ns/row at n=2000**; the line now reads **65 276**. The
arithmetic invited a regression story: 3973 is almost exactly the constant per-row work above, i.e.
the shape of a join that WAS using an index.

**The history says otherwise, and it says it cheaply.** `valueQueryable.cpp` — the join executor —
was last touched on **2026-08-07**, three days BEFORE the 3973 was written down (`12c588d5`,
2026-08-10). Between that commit and today the only change anywhere in the path is six lines in
`ibMemberTable::Build` (`506640f7`, the `ForEachBinder` refactor), and it cannot be worth ×16 or the
Structure lines would have moved with it — they did not (`record build+walk` 5436–5584 ns against
~5300 historically; `New + 2 Insert` and `struct build` in range).

**The same code cannot be both numbers.** So 3973 was measured under conditions this run does not
reproduce, and a bisect would burn a Release build to confirm that nothing changed. What stands on
today's measurement alone is the shape: the join is a nested loop, and the index beside it is unused.

⚠ Method, not blame: a figure recorded in a comment carries no build, no machine and no date, so it
cannot be compared with a later one. The numbers that ARE comparable in this document all name the
configuration they were taken under.

### What comes after the index

The **key type**, as already recorded in §1f-bis: probe 45.2 ns for a `long` key against 329.5 for
an `ibValue` one (**×7.3**), build 179.6 against 628.8.

## 6. The container is a hash now, not a tree with a mirrored key surface (2026-08-11)

The join / group hash and every script `Structure` are `ibValueContainer`. It carried three
faults, each a mark of code written on the fly, and each fixed here.

**The store was a red-black tree with an allocating comparator.** `std::map<ibValue, ibValue,
ContainerComparator>`, where the comparator materialised and upper-cased BOTH keys on every
comparison, and a lookup does O(log n) of them. Now a `std::vector<pair>` (insertion order — a
stable index for the property protocol, a deterministic iteration order) beside an
`std::unordered_map` keyed by the ibValue itself: one hash probe, and the hash / equality read the
key's identity IN PLACE (zero-copy for a string), so a string lookup allocates nothing.

**Keys were mirrored into the member table to fake `container.key`.** That is a Structure feature —
named fields — leaking into a map, and it made building an n-key container **O(n²)**: every Insert
invalidated the member surface, and the next access rebuilt it O(size). Keys are DATA now: they
live in the store and are resolved by `FindProp` / `GetPropVal` (overridden) straight off it. The
member table carries only the fixed methods and is built once.

**A band-aid had grown over the symptom** — an `@Index` container subtype with an
`m_omitKeyMembers` flag, the LINQ join / group hashes opting out of the key surface. Deleted whole;
a plain Container is now O(1) and needs no variant.

Measured, Release, `DISABLED_LinqJoin` (integer keys, so the container is the variable):

| n | before (O(n²)) | after (hash) |
|---|---|---|
| 250 | 11 227 | 2 118 |
| 1 000 | 36 911 | 2 248 |
| 4 000 | 140 468 | 2 356 |
| 16 000 | **577 565** | **2 425** |

Flat — O(1) — and **238×** at n=16 000. Group-by, the same store, is flat too. The full suite
(1269 tests) stays green; the change touches the property protocol every `object.member` runs
through, so that number is the guard.

### Two correctness fixes that rode with it

- **A block `join` fans out on a repeated inner key.** It used to die: the hash was a Container and
  `Insert` threw on a duplicate, so an inner table with repeating join keys (orders per customer)
  raised out of the whole query. The hash maps a key to a bucket of rows now, one result row per
  match, like the `.Join()` executor and SQL.
- **A block `skip` / `take` no longer crashes inside a function.** The module-init pass that
  pre-stamps MODULE-level types walked INTO named-function bodies (it skipped only lambda fences,
  not `FUNC` / `ENDFUNC`) and applied a function-local `SET_TYPE` against the smaller module frame —
  an out-of-range read, latent until a clause grew the function's local count. The pass steps over
  function bodies now, exactly as the dispatch loop already did.

### The boundary, honestly

The store went tree → hash and the key surface went O(size)-per-mutation → nothing; those were the
wins, and they are multiples. The last string allocation on a lookup (the `wstring` the old index
key needed) is gone too, but that one shows up **in the noise** — a field read is dominated by
interpreter dispatch and ibValue copies, not one allocation. And it does nothing for INTEGER keys
(the join/group bench): there the cost is the number → string identity, whose real fix is a native
numeric hash, which runs into `ibNumber` canonicality (1 vs 1.0) — a percent for a risk, not taken.
The key type remains the lever §1f-bis names (`ibValue` probe ×7.3 a `long`'s); the container no
longer is.

---

## 9. The comparator itself (2026-08-15)

§8 left the key type as THE lever and put a number on it: an `ibValue` probe costs ×7.3 a `long`
one. That number is about `std::map` calling `ibValue::CompareValueLS` ~log₂n times per probe — so
it splits in two, the COUNT of comparisons (the tree) and the COST of one. This section is the
second half only; the tree is untouched and still O(log n).

**What one comparison was paying for nothing.** Three things, all in `value.cpp`:

- **Every compare ran twice.** `a < b ? -1 : (b < a ? 1 : 0)` performs the whole comparison a
  second time to learn what one call already returns — `ibNumber::Compare` and
  `std::wstring::compare` are three-way primitives.
- **A getter round-trip inside a `case` that already knew the type.** In `case TYPE_NUMBER`,
  `GetNumber()` is a virtual call returning BY VALUE; on the string arm `GetString()` returned a
  `wxString` by value, so an ORDERING comparison **allocated two strings**. The tag says where the
  bytes are — when both sides carry the same one, the payload is read off the member.
- **A reference to a string rebuilt the string.** `GetString(ibString&)` exists to hand back the
  live buffer, but a `TYPE_REFFER` fell past that into the coercion below and materialised a
  `wxString` of text it was already pointing at. It follows the chain now.

### Measured, Release x64, same machine as §8

| row | 2026-08-11 (§8) | comparator only | + hashed index | Δ overall |
|---|---:|---:|---:|---:|
| index probe `ibValue` n=16000 | 329.5 | 198.8 | **168.3** | **−49%** |
| index build `ibValue` n=16000 | 628.8 | 337.1 | **325.2** | −48% |
| index probe `long` n=16000 — CONTROL | 45.2 | 44.7 | 44.0 | −3% |
| LINQ join n=16000 | — | 2947.7 | **2837.5** | −4% |
| join n=1000000 | — | 3692.9 | **3534.7** | −4% |
| struct field read | 331 | 211.5 | 198.4 | −40% |
| arith loop (untouched) | 73.4 (§7) | 62.5 | 61.6 | −16% |

**Read the RATIO, not the column.** `arith loop` moved 15% without anyone touching arithmetic, so
this machine is not the machine of §8 — every absolute figure carries that drift. What survives it
is the pair measured in one process:

| | 2026-08-11 | 2026-08-15 |
|---|---:|---:|
| probe `ibValue` / `long` | ×7.29 | **×3.83** |
| build `ibValue` / `long` | ×3.50 | **×2.11** |

So the lever §8 named as ×7.3 is ×3.8 now: **most of that gap was the comparison itself**, not the
key type. What remains is the key type — an `ibValue` probe still costs ×3.8 a `long` one, and no
amount of comparator work closes that; it is the tagged union and the virtual call, not the code
around them.

### The tree, and what replacing it was actually worth (2026-08-15)

`ibValueJoinState::m_hash` was a `std::map`, so the comparator ran ~log₂n times per probe. It is a
`std::unordered_map` now, keyed by `GetValueHash` with `CompareValueLS == 0` for equality — the same
relation the tree meant by "same key". Group-by's `m_keyIdx` went the same way (its emission order
was never the tree's: `m_groups` holds it).

| | tree | hash |
|---|---:|---:|
| LINQ join n=16000 | 2947.7 | **2837.5** (−4%) |
| join n=1000000 | 3692.9 | **3534.7** (−4%) |
| group by, few distinct keys (n=250…16000) | 3848…4803 | 3642…4668 (−1…−5%) |
| group by, EVERY key distinct (n=16000…1M) | 4940…5753 | 5265…5785 (**+4…6%**) |

**−4% on the join, and group-by is a wash.** The prediction was "kratno" (multiple times) and it was
wrong — worth writing down WHY, because the reasoning error is reusable: a probe is ~18% of a join
row, and 14 tree comparisons over a hot, predictable node chain are not 14× the cost of one hash of
an `ibValue` plus one comparison. Removing a log factor only pays when the factor multiplies
something expensive; here it multiplied something cache-resident.

The group-by split is the same arithmetic seen from the other side: with every key distinct the
table grows to n entries and pays for that growth, while the tree paid per-comparison only. Left as
it is — the loss is inside the run-to-run band and the win at low cardinality is real — but nobody
should record group-by as a win.

### ⚠ The ranks nearly cost more than everything above put together

Separating the kinds (below) put a classification in front of every comparison, and classification
asks `GetType()`, which is VIRTUAL. On a path a tree walks 14 times per probe:

| | before ranks | ranks, unguarded | ranks, tag-guarded |
|---|---:|---:|---:|
| index probe `ibValue` n=16000 | 198.8 | **353.3** (+78%) | **168.3** |
| index build `ibValue` n=16000 | 337.1 | **724.3** (+115%) | 325.2 |

The guard is one line — if the two RAW tags are equal the ranks are equal, so skip the
classification entirely — and it is the case every real index is made of, a column against a
column. With it the comparison ends up 15% FASTER than before the ranks existed, because the same
guard also removes a `GetType()` that the string-rank check used to do on every scalar comparison.

**The lesson is placement, not correctness.** The rank rule was needed (see below); putting it at
the top of a function that runs 14 times per lookup, unguarded, was the mistake — and it was
invisible until measured: the suite stayed green throughout.

The `long` probe holding at 45.2 → 44.7 is what makes the probe row trustworthy; the `long` BUILD
moving 27% is why the build row is quoted but not leaned on — build allocates map nodes, and that
is heap state as much as code.

### ONE IDENTITY. `ibValue::GetHashKey` is gone (2026-08-15)

There were two ways to answer "are these the same value", and the engine used both:

* `GetHashKey() -> wxString` — the value rendered into text. Grouping, joins, DISTINCT, hierarchy
  linking, temp-table indexes and the registers all keyed a `std::map<wxString, …>` by it, and built
  COMPOSITE keys by gluing several with `\x1f`.
* `GetValueHash() + CompareValueLS` — the value compared as a value.

Two answers to one question drift, and this pair had already drifted: the rendered one made `1` and
`"1"` the same key, which is not what the language's comparison says anywhere else. So the rendered
one was **removed entirely**, and every caller moved onto the value.

**What a rendered key cost per row.** A number went through `ToString` (175 ns measured), a
reference through `wxString::Format("%i:%s")`; then the pieces were concatenated, and the `std::map`
compared the results character by character. On a group-by that is one conversion per level per row,
paid twice where a parent key was rebuilt from the same values.

| moved onto the value | was |
|---|---|
| RAM hash-join, GROUP BY, 3 × DISTINCT, semi-join, totals fold | text per cell + `map<wxString>` |
| ROLLUP index, reference hierarchy, multi-level dimensions | text + **three** `valOf`/`keyVal` maps |
| temp-store per-column index | text per row |
| balance opening, both registers (`ibBalanceOpening`) | both sides folded the same values to text |
| report-row identity, per-account caches, kind sets | `KeyOfValues` + 16 string-keyed containers |
| schema seed signature | two fingerprint strings per comparison |
| container keys | rendered text; now: strings fold case, everything else compares as a value |

**Three containers vanished outright** (`valOf` ×2, `keyVal`) — they existed only because a string
key cannot give back the value it was rendered from.

### Measured — median of THREE runs, machine quiet

| row | before | run 1 | run 2 | run 3 | median | Δ |
|---|---:|---:|---:|---:|---:|---:|
| index probe `ibValue` n=16000 | 146.2 | 124.0 | 121.2 | 121.7 | **121.7** | **−17%** |
| LINQ join n=16000 | 2319.4 | 2016.4 | 2083.2 | 2046.3 | **2046.3** | **−12%** |
| group by n=16000 | 4369.1 | 3852.7 | 4075.5 | 4095.3 | **4075.5** | **−7%** |
| record build+walk n=20000 | 4021.2 | 3813.9 | 3795.8 | 3828.4 | **3813.9** | **−5%** |
| index build `ibValue` n=16000 | 311.1 | 286.9 | 334.2 | 322.1 | 322.1 | +3% — no effect |
| New + 2 Insert | 2794.5 | 2808.7 | 2656.3 | 2767.6 | 2767.6 | −1% — noise |
| dot access | 189.0 | 190.6 | 184.0 | 191.1 | 190.6 | 0% |
| arith loop — CONTROL | 62.1 | 61.9 | 60.2 | 60.0 | 60.0 | −3% (machine cooled) |

Net of the 3% the control moved: **probe −14%, join −9%, group-by −4%, record build −2%**, and
**nothing** on index build, insert or field read.

That split is the honest reading: the win is in LOOKUP, not in construction. Building an index
allocates nodes, and allocation is what it is bound by — which the spread says too. Three runs of the
identical binary vary by 0.9% on `record build`, 2.3% on `probe`, 3.3% on `join`, 6% on `group by`
and **16% on `index build`**. The rows that allocate are the rows that shout, and they are exactly
where a single run gets mistaken for a result — twice in this section's history, before the rule
became "three runs or it did not happen".

From the §8 baseline the probe now reads 329.5 -> **121.7 ns, −63%**, and the `ibValue`/`long` ratio
§8 recorded as ×7.29 is **×2.7**.

**The shape cache keeps a string, and that is deliberate**: its key mixes numbers with kinds. It now
carries `GetValueHash()` **plus `GetClassType()`** — a hash alone can collide, and a collision there
hands the reader a table built for someone else's arguments; the type number pins which kind
produced the hash.

**This FIXES A REPORTING BUG, it does not merely change a rule.**

A composite-typed attribute (a characteristic's value, a ChartOfCharacteristicTypes column) holds a
number in one row and a string in another. Group a report by that column and the two used to land in
ONE row: both rendered to the text `"1"`, so two different values were shown as one group and their
sums were added together. Nothing reported the merge — the total simply looked plausible.

Types are compared as types now, so `1` and `"1"` are two groups, which is what an accountant reading
the report already assumed. Pinned by tests rather than left to be discovered:

* `ValueContainer.NumberAndItsSpellingAreDifferentKeys` — and that both can coexist in one container,
  which the old rule made impossible;
* a join / DISTINCT no longer matches a number against the string spelling it.

Case-insensitive field names survive: the fold runs ONCE per lookup while hashing, and inside a
bucket the comparison decides most candidates on length alone, folding only characters that differ —
the same two shortcuts `stringUtils::CompareString` earned the hard way (below).

### READ THE DISASSEMBLY. Two rounds of reasoning lost to one /FAsc run

Everything above was argued from the source and the bench. Compiling `value.cpp` with `/FAsc`
(flags lifted from `backend.tlog\CL.command.1.tlog`, so they match the real build) took a minute and
contradicted three claims made from reading:

```
; const bool bNull = (cParam.GetType() == TYPE_EMPTY || cParam.GetType() == TYPE_NULL);
  0005c   call QWORD PTR [rax+72]        <- TWO indirect calls, not one
  00069   call QWORD PTR [rax+72]
  00090   call ?KindRank@...             <- `inline` did NOT inline
  00154   call ?Compare@ibNumber@@...    <- nor did the header's hot path
```

1. **A virtual call is opaque to the optimiser.** Two `GetType()` comparisons in one expression are
   two indirect calls — MSVC cannot prove the second returns what the first did. This was paid on
   EVERY comparison, ahead of every fast path added above. Resolving the kind ONCE, and only when
   the value is a reference (`cParam.IsReference() ? cParam.GetType() : cParam.m_typeClass`), leaves
   the common case with no virtual call here at all.
2. **`KindRank` is a real `call`**, twice, despite `inline`. It sits behind the tag guard so it only
   costs on mixed pairs — but "a switch on a tag is free" was an assumption, not a fact.
3. **`ibNumber::Compare` is a real `call`**, next to a `call ??1ibNumber` for the temporary that
   `GetNumber()` returns. `fnumber.h` states it lives in the header so the caller's compiler can
   inline the immediate path; in this caller it does not. Untouched for now — `__forceinline` is not
   portable across the three toolchains, so this needs either a shared macro or splitting the arm
   into a small function.

Measured effect of (1) alone:

| row | before | after | Δ |
|---|---:|---:|---:|
| index probe `ibValue` n=16000 | 168.3 | **151.4** | −10% |
| LINQ join n=16000 | 2837.5 | **2687.8** | −5% |
| group by n=16000, all keys distinct | 5264.7 | **4542.8** | **−14%** |
| arith loop — control | 61.6 | 59.9 | −3% |
| probe `long` — control | 44.0 | 47.3 | +8% |

The controls moved ±8% here, so only the probe and group-by rows carry. That group-by row matters
beyond its size: hashing had made it WORSE (+6%, above), and it is now 4542.8 against the 4939.6 it
started at — **−8% overall**. The virtual calls in the comparator were what ate the hash's win.

**Method note.** Two earlier rounds in this section explained numbers by reading code, and both were
wrong in their details (a bucket allocation that did not exist; a "cheap" switch that compiles to a
call). The disassembly settles such questions in a minute and should come first.

### Forcing the inline the header already asked for

`ibNumber::Compare` lives in fnumber.h with a hot/cold split so callers can inline the immediate
path — and the disassembly above showed a real `call` on the value comparator's number arm. `inline`
is a hint; under a size budget the inliner declines it. **IB_FORCEINLINE already existed** in
procUnit.cpp, portable across the three toolchains, with a note describing this exact wall ("the
disassembly says so"); it moved to `backend/backend.h` for the second caller rather than being
copied. Verified in `/FAsc`: `call ?Compare@ibNumber` is gone, `call ?CompareBig` (the cold path)
remains — which is the split working as designed.

| row | before | after (2 runs) |
|---|---:|---:|
| index probe `ibValue` n=16000 | 151.4 | **136.0 / 124.1** |
| index build `ibValue` n=16000 | 336.3 | **302.9 / 282.3** |
| `compare immediate` (NumberBench) | 2.7 | **2.0** |
| `add immediate` | 5.1 | 4.2 |
| record build+walk n=20000 | 3925.5 | 4273.9 / 4023.1 |
| dot access | 191.3 | 209.6 / 188.6 |
| arith loop — control | 59.9 | 60.6 / 60.4 |

**Code size: 9.90 -> 9.95 MB (+0.5%)** — the forced body is a gate plus one int64 compare, CompareBig
stays out of line, so pasting it into every caller costs almost nothing.

⚠ **The first run said record build+walk and dot access had regressed 9-10%. The second says they
had not** — both returned to their means. The rule this cost twice today: on rows whose run-to-run
spread is ±5-9%, a single run cannot distinguish an effect of that size from noise. Two runs of the
SAME binary read 136.0 and 124.1 on the probe — so treat anything under ~9% on these rows as
unproven, and quote a range rather than a number.

### Chasing `struct build` 1603 -> ~2200, and what it turned out to be

§8 recorded `struct build 3 fields` at 1603 ns; today it reads 2131. The baseline predates
`675b04db` (the container-becomes-a-hash commit, 23:19 the same day) by hours, and `git log -L`
over the ctor body confirms that commit is the last thing to touch it — so the suspicion was a
fixed price the tree never paid: the first insert allocating a bucket array.

**Measured, and the hypothesis was wrong.** `DISABLED_StructBuildWidth` builds a Structure of
0/1/2/3/5/10 fields (values omitted — the ctor allows it, so this is the insert machinery alone):

| width | before | after the fix below |
|---|---:|---:|
| empty | 695.6 | 693.4 |
| 1 field | 1096.5 | 1074.9 |
| 2 fields | 1535.0 | 1467.4 |
| 3 fields | 2007.7 | 1899.5 |
| 10 fields | 6272.8 | 5933.7 |

No step at the first field — the per-field cost RISES with width (401 -> 610 ns) instead. A bucket
allocation would have shown as the opposite shape.

**What it actually was, in part:** `Insert` asked about the key TWICE — `m_index.find`, then
`m_index.emplace` — and the key's hash is a fold over its whole text, which on a Structure is a
field name. `emplace` already reports whether the key was present. One question instead of two:
−5% per field, **−7% on `New + 2 Insert`** (2955.7 -> 2747.5).

**What it was NOT.** That leaves most of the 1603 -> 2131 gap unexplained, and two things say not to
force an explanation onto it. The bench itself spread 2165.9 / 2312.2 / 2321.4 across three runs of
identical code — 7% — so part of the "regression" is this machine. And the structural cost is
visible elsewhere: the key is stored TWICE, as an `ibValue` in `m_index` and again in `m_entries`,
which is the same finding the footprint probe reports as ~1000 bytes per field for 40 bytes of data.
Removing that second copy means the index holding entry positions rather than keys, with lookup
reaching through to `m_entries` — a rebuild of the container's store, not a patch, and not done here.

### Two results that were not speedups

**The type check: `dynamic_cast` WINS, ×7.6.** Array/container comparison asks "is the other side
one of me" per compared pair, and per ELEMENT when the elements are composite. The class id looks
cheaper — an integer compare — but obtaining it is a virtual call ending in `GetTypeIDByRef`:

```
type check: id vs cast    id=59.7ns   cast=7.9ns   x7.6
```

(`DISABLED_TypeCheckCost`.) The id version was written, measured, and reverted the same hour. The
numbers live in a comment beside the cast so the next reader does not repeat it.

**Structure footprint — the member table is NOT the weight.** The CI million-row run shows ~7.5 KB
of resident set per 2-field row and the standing hypothesis was per-row member tables with copied
names and helper strings. `DISABLED_StructureFootprint` separates the shapes and refutes it:

```
empty Structure          538 bytes/row      <- the object AND its member table
+ two inserted fields   1998 bytes/row      <- the fields alone (increment)
two ibValue                80 bytes         <- what the DATA is
```

An empty Structure carries its whole member table for 538 bytes; each inserted field adds ~1000 for
40 bytes of data. So the weight is the ENTRY storage — a key held twice (`m_entries` and
`m_index`), an `unordered_map` node, its bucket array, the heap buffer of the name — and a common
string pool for helper text, which was the planned arc, would have bought almost nothing.

### Correctness that rode with it

Not performance, found while reading the comparator:

- **Arrays and containers compared EQUAL to each other.** Neither overrode `CompareValueLS`, so the
  base compared object kinds by `GetString()`, which for an object kind is the CLASS NAME — every
  array read as `"Array"`. A join or group keyed on a composite key put every row in one bucket,
  silently. Both override it now, walking their elements/entries through `std::lexicographical_compare`.
- **`CompareValueNE` was a second copy of `CompareValueEQ`, inverted.** A dozen classes override
  only EQ (enums, guid, OLE, composition field, the module managers) and inherited the base's `<>`,
  so `=` and `<>` could disagree about one pair. It is `!CompareValueEQ` now.
- **A reffer must not change an answer.** `GetType()` follows the reference chain, the raw
  `m_typeClass` tag does not. Classification asks the former, field access the latter — swap them
  and `1 = ref(1)` turns false. `ValueThroughReference.*` in `tests/test_value.cpp` guards it.
- **THE ORDER WAS NOT A STRICT WEAK ORDERING.** Two coercions inside it were non-transitive, both
  live for as long as the comparator has existed:

  ```
  True == 2  and  True == 3,   but 2 != 3        (any non-zero number read as True)
  1 == date(1500) and 1 == date(1999),  but those differ   (a date read as instant/1000)
  0 == an empty array                            (GetNumber() on an object kind is 0)
  ```

  A relation where something equals two values that differ from each other cannot key a `std::map`
  (lookup is unspecified) and makes `std::sort` formally undefined. Fixed by giving each kind its
  own stretch of the order — Boolean · Number · Date · text-ish · TYPE_VALUE — so a comparison only
  meets kinds it can answer about exactly. Coercion is untouched everywhere else: `True + 1` still
  works, `GetNumber()` on a string still parses. `1 < "0"` is TRUE now and was false.

  **None of this was found by reading.** Two rounds of it were found by
  `ValueHashContract.OrderEqualImpliesHashEqual`, which exists only because a hash had to agree with
  the order — a second implementation of "equal" disagreeing with the first is what made the
  non-transitivity visible. `ValueOrderAcrossKinds.EqualityUnderOrderIsTransitive` now states the
  property directly.

⚠ **Non-ASCII key folding is the process locale's, not ours.** `HashOf`/`FoldedEquals` fold case
through `std::towupper`, which folds a non-ASCII letter only where the locale says how. The same
configuration therefore sees case-SENSITIVE Cyrillic keys headless (daemon, codeRunner, the suite)
and case-INSENSITIVE ones under a UI locale. Unchanged by this work — an ASCII fast path was added
in front of the same call — but for a Russian-language platform it is a language question, open.

### The x86 build was running a different hash, and only the SOLUTION said so

The arc above landed and pushed green. Then the MSBuild solution was built — Debug|x86, which the
CMake tree never produces — and two warnings came back on `value.h`:

```
warning C4305: truncation from 'unsigned __int64' to 'size_t'
warning C4309: truncation of constant value
```

`size_t` is 32 bits on x86. Every hash written in this arc accumulated in `size_t`, and FNV-1a's
constants are 64-bit, so on that build the basis and the prime were **silently cut to their low 32
bits** — a hash of a different, and much worse, shape than the x64 one. Not a crash and not a wrong
answer: containers would still have found their keys, just with more collisions than intended, which
is the kind of defect that never announces itself.

Six sites had it — `ibValueSeqHash` (value.h), `ibValue::GetValueHash` and its `HashStep`
(value.cpp), both container hashes (valueMap.cpp), the array's (valueArray.cpp) and the reference's
(reference.h).

**The fix is not six casts, it is one primitive.** Six hand-written copies of FNV-1a is six chances
for one of them to be typed a little differently, so the mixer now exists once, in value.h beside the
key policy it serves:

```cpp
constexpr std::uint64_t kIbHashBasis = 14695981039346656037ULL;   // FNV-1a offset basis
inline std::uint64_t ibHashCombine(std::uint64_t h, std::uint64_t v)
{
	return (h ^ v) * 1099511628211ULL;                            // FNV-1a prime
}
```

Every site accumulates in `uint64_t` through it and narrows ONCE at the return. The basis was also
wrong on the way in: `1469598103934665603` is `14695981039346656037` with digits dropped, a typo
this arc copied from `helpCorpus.cpp` / `leakTracker.cpp`, which still carry it (harmless there, and
`helpCorpus`'s digest is compared against stored values — left alone deliberately). `clsid.h` had the
correct one all along.

🛑 **The lesson is about COVERAGE, not about casts.** The test tree is CMake and CMake here is x64;
the solution is the only thing that builds x86. A whole class of defect — anything that depends on
the width of `size_t`, `long`, or a pointer — is therefore invisible to `ctest` and to CI, and shows
up only when somebody builds the solution. This one was caught by a build that was asked for after
the push, not before it. **Build the solution before the push, not after.**
