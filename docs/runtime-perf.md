# Runtime performance — hot-loop trim + benchmark harness

> **Scope:** the `ibProcUnit` interpreter hot loop + the `tests/bench_runtime.cpp`
> micro-benchmark harness. What was trimmed, why it is safe, how to measure, and where
> the numbers land against interpreters of the same class.
>
> **Status:** hot-loop trim + wall-time bench column are an **experimental working copy
> (uncommitted), 2026-07-18**. Numbers are Release|x64, MSVC 19.42, min-of-5 micro-bench
> (jitter ±3–5% — read orders of magnitude, not the last nanosecond).
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

## 3. Results (Release x64, 2026-07-18)

### Interpreter — overhead + before/after (ns, dispatch trim)

| Scenario | before (3 runs) | after (2 runs) | overhead | one full run |
|---|---|---|---|---|
| arith loop (1M iters) | 150.7 / 154.8 / 147.9 | **135.7 / 136.0** | ×~200 | 136 ms |
| LINQ build+pipe (10K) | 1111 / 1087 / 1013 | **946 / 982** | — | 9.6 ms |
| recursion Fib(28) (1.03M) | 371.6 / 380.5 / 370.9 | 369.8 / 348.0 | ×~220 | 359 ms |
| host→script (200K) | 413.7 / 397.6 / 378.1 | 379.5 / 388.3 | ×~215 | 77 ms |
| string concat Cat(2000) | 1545 / 1462 / 1486 | 1427 / 1454 | ×~1100 | 2.9 ms |

`arith` and `LINQ` drop ~10% cleanly (both after-runs below all before-runs). `recursion` /
`host` / `concat` sit inside jitter — their bottleneck is not the dispatch checks. The
`string concat ×1100` outlier is O(n²) immutable-string alloc, not the interpreter (same trap as
`String` vs `StringBuilder`).

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
Ruby YARV, and 1С (~50–300× over native C). Reference orders of magnitude (different machines,
approximate): ~2× slower than CPython (30 years of interpreter tuning), ~5–7× slower than Lua 5
(register-VM, the interpreter-speed benchmark), same league as 1С or faster.

Assessment: **healthy for its class** — overhead is uniform (×200–220 across three independent
scenarios, a health sign), and the absolute cost is trivial for a server-centric domain (heavy
work — queries, volume — lives in the C++ core, not the script; the script holds business rules).

Acceleration ladder, **by need, not now** (profile gives no reason — the runtime is not, and is
unlikely to become, the bottleneck):

1. **Hot-loop check trim** — DONE (this doc), −10%, portable.
2. **computed-goto dispatch** — +10–30% on GCC/Clang, but a GNU extension **MSVC does not support**
   (would need an `#ifdef` fork or a clang-cl build of `procUnit.cpp`).
3. **register-VM** — the no-JIT ceiling (Lua-style). A big rewrite (codegen + interpreter + AOT
   bump); note the operand addressing is already `(array, index)` three-address, i.e. register-style,
   so the main register-VM win (no operand stack traffic) is largely already present.
4. **JIT** — 1–5×, maximum cost.
