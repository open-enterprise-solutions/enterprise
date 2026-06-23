# Memory allocator — strategy & seam (design note, not built)

> **Status: DESIGN NOTE / NOT STARTED — captured so the option is not lost.**
> No central allocator exists today and none is urgently needed (see §2 — the
> one *correctness* motive is already closed by `/MD`). This doc records the
> decision **shape**: introduce a thin `ibMemory` / `IMemoryManager` seam when
> convenient, and grow the machinery (drop-in allocator → pools → arena →
> per-session accounting) **only under a concrete motive**, never because
> "serious engines have one". The spectrum (§4) and the correct arena layer
> (§5) are the load-bearing conclusions.
>
> Related: [[project_value_footprint_audit]], [[reference_flat_map_over_stdmap]],
> [[feedback_prefer_shared_ptr]], [[project_erp_platform_roadmap]] (the
> per-session-accounting tie is part of Blocker B). Source touch-points:
> `backend/fstring.h` (`ibFStringPool` — the one pool already in the tree),
> `backend/rowValues.h` (`ibRowValues` — per-object flat store),
> `backend/compiler/value.{h,cpp}`. Cross-refs: [value-audit.md](value-audit.md),
> [compute-server-tiering.md](compute-server-tiering.md),
> [closure-capture.md](closure-capture.md),
> [debugger-per-session.md](debugger-per-session.md).

---

## 1. Scope — what this decides

"Write our own allocator" is **a spectrum, not a yes/no**. The four points on it
(§4) have opposite cost/benefit, so the question only has an answer once you name
the *motive*. This note pins the motives (§3), the current floor we already stand
on (§2), and where each spectrum level pays. It deliberately does **not** schedule
any of it — there is no production workload yet, so any allocator now is tuned on
synthetic numbers. The measurement harness already exists
(`tests/bench_runtime.cpp` — `RuntimeBench` / `NumberBench`), so "measure before"
is cheap and is step 0 of any level.

## 2. The floor we already stand on

- **Cross-DLL ownership is already correct.** OES links the CRT dynamically
  (`/MD` — the OES projects carry no explicit `RuntimeLibrary` override, so they
  take the MSBuild default `MultiThreadedDLL` / `MultiThreadedDebugDLL`; the wx
  submodule is `MultiThreadedDLL`). `backend.dll`, `frontend.dll` and the exes
  therefore share **one** CRT heap, so an `ibValue` allocated in the backend is
  freed correctly in the frontend **today**. The single *correctness* reason a
  multi-module platform centralises allocation (a `/MT` world where each module
  has a private heap and a cross-module `free` corrupts) **does not apply here.**
  If OES ever switches to `/MT`, re-open this — it would flip a unified allocator
  from "nice" to "mandatory".
- **A size-classed pool is already in the tree, isolated.** `ibFStringPool`
  (`fstring.h`) is a per-thread, size-classed free-list behind `ibString`: short
  strings stay in `std::wstring` SSO and never allocate; longer ones reuse a
  cached block. This is exactly one slice of a memory subsystem, already proven —
  the generalisation is *lifting the policy behind a shared seam*, not inventing it.
- **Per-object allocation already optimised.** `ibRowValues` (flat sorted vector)
  replaced `std::map<ibMetaID, ibValue>` per record/row — one block per object
  instead of a node per attribute (see [[reference_flat_map_over_stdmap]]). The
  instinct here is already "optimise the data-structure / ownership path", not
  "swap the global allocator".
- **No unified seam exists.** Outside `ibFStringPool`, every allocation goes
  straight to global `new` / CRT. There is no single point to hang a policy on.

## 3. The three motives — and the one we *don't* have

A custom memory subsystem is built for one or more of these. Naming the motive is
what makes the decision, not the precedent.

| Motive | What it buys | Applies to OES? |
|---|---|---|
| **Governance** | one point to **account / cap / diagnose** memory: per-session limits, runaway-script containment, leak tracking | **Yes — strongest.** N web sessions share a process with **no** per-session memory accounting today; one runaway script grows RSS and takes down every session. This is the natural home for a "memory per session" limit and is part of the compute-server (Blocker B) contour. |
| **Debug-heap** | fill-on-free + guard bytes catch use-after-free **at the fault**, not later | **Yes.** There is an open, un-pinned UAF (freed `ibByteCode` while a session is parked — see [debugger-per-session.md](debugger-per-session.md)). A debug heap is precisely the tool that catches it on first touch. (For a *one-off* catch, Windows PageHeap / App Verifier does this with zero code — a built-in debug heap only pays if you want it **always on** in Debug builds.) |
| **Speed** | thread-caching alloc + pools cut malloc contention and per-object churn | **Partially.** Worker pool + N sessions make cross-thread alloc/free contend; a drop-in thread-caching allocator (§4 level 0) recovers most of it for free. Targeted pools only by profile. |
| **Frame budget** | arena reset per render frame keeps a 16 ms loop allocation-free | **No.** An enterprise platform has no 60 Hz loop under a hard budget; a button press building a form in 50–200 ms is fine. **The one exception is scroll/paging** (`ibDataViewItem` allocated in batches per scroll edge) — that is the only place the game-engine "frame pool" pattern transfers. |

The frame-budget motive is what forces game engines and large managed-memory
platforms to ship the full pool/arena machinery from day one. OES lacks it, which
is exactly why the full build-out is **not** justified by precedent alone.

## 4. The spectrum

| Level | What | Verdict for OES | When |
|---|---|---|---|
| **0** | Drop-in thread-caching allocator (mimalloc / rpmalloc) as global `new`/`malloc` override | **Do this first if anything.** Link-only, no layout/ABI change, MIT-licensed (compatible with LGPL 2.1), cross-compiler. Wins on the multi-thread runtime for free. | Anytime; gate on a `bench_runtime` delta. |
| **1** | Targeted free-list / slab pool per hot type | By profile. Named candidates: `ibRunContext` under closures (heap-promoted per fn-with-lambda — [closure-capture.md](closure-capture.md) calls out "small-object pool TBD"); `ibDataViewItem` batches under paging. `ibFStringPool` is the template. | After level 0 measurement shows a hot type. |
| **2** | Arena / region (`alloc` in bulk, `clear` at once) | Correct **only** in the right layer — see §5. **Blocked in the interpreter** (closures), **natural in the query layer**. | With a real workload + the query-scope lifetime confirmed. |
| **3** | Hand-rolled general-purpose `malloc` | **No.** Solved problem; will not beat mimalloc/jemalloc, and is a permanent 3-platform maintenance tax. Only justified by an exotic environment constraint OES does not have. | Never (absent such a constraint). |

## 5. Where an arena is correct — query layer, not the VM

The tempting arena is "all temporaries of an interpreter frame live in a bump
region, reset on `RET`". **It is unsafe in the bytecode interpreter** because
closure capture lets a frame outlive its call (`ibRunContext` heap-promoted, held
through a lambda's `m_capturedFrames`), and `ibValue` is ref-counted and escapes
upward. A naive per-frame arena would reclaim still-live objects. Making it safe
needs escape analysis, which does not exist — so level 2 in the VM is an R&D arc,
not a near-term move.

The arena **does** belong where lifetime is strictly bounded and nothing escapes:
the **query / composer / temp-db scope** (`ibQueryIR` nodes, `ibDataComposer`
intermediates, materialisation buffers — L3–L5). Those live exactly as long as one
query's processing and are dropped wholesale at its end — the textbook region
shape. This is the same lifetime as the connection-pinning scope already used for
write-locks and temp tables (see [temp-db.md](temp-db.md) §6,
[record-locks.md](record-locks.md)). **Conclusion: if/when an arena is built, aim
it at the query scope, never at VM frames.**

## 6. Prior art already in the tree

- **wxWidgets memory debugging.** The toolkit in the submodule already vends
  memory-debug hooks (`wxUSE_MEMORY_TRACING`, the `__FILE__`/`__LINE__`-tagged
  `operator new` under `wxDEBUG`, `wxDebugContext`). Prior art for the debug-heap
  motive — the pattern is not exotic, it is shipped in a dependency we already build.
- **CRT debug heap.** MSVC Debug already routes `new` through the slow checked
  heap — which is *why* `std::map`-per-node churn punishes Debug runs (noted in
  [value-audit.md](value-audit.md)). A pool's biggest visible win is often just
  sidestepping that path in Debug.
- **`ibFStringPool`, `ibRowValues`** — covered in §2; the first allocator brick
  and the first per-object store optimisation are already laid.

## 7. Recommendation

1. **No separate DLL.** A dedicated allocator module earns its keep where *many*
   native modules must route through one heap. OES has **two** DLLs and a shared
   `/MD` heap already (§2). A seam **inside** `backend.dll` exported via
   `BACKEND_API` is sufficient; a `memory.dll` would be cargo-culting the shape
   without the reason.
2. **Introduce the thin seam when convenient** — an `IMemoryManager` /
   `ibMemory::Alloc/Free` that hot types route through, initially just forwarding
   to global `new`. This is the memory analogue of the project's other
   unifications (single `GetValueByPath`, single L3 door, gate-at-creator): its
   value is **one place** to later attach a pool / mimalloc / arena / accounting
   **without touching call sites**. Cost ≈ zero; it is a placeholder, not machinery.
3. **Grow machinery by motive, in this order of likelihood:** level-0 mimalloc
   (speed, free) → pools for `ibRunContext` / `ibDataViewItem` (by profile) →
   query-scope arena (§5) → **per-session accounting** (governance — the seam's
   real payoff, lands with the compute-server contour).
4. **Debug heap** rides the same seam (fill-on-free + guard in Debug). Until then,
   PageHeap is the zero-code way to chase the open parked-session UAF.

## 8. Open / ties

- **Per-session memory limit** has no home until the seam exists; it is part of
  the compute-server / Blocker-B work ([compute-server-tiering.md](compute-server-tiering.md)).
- **Parked-session UAF** ([debugger-per-session.md](debugger-per-session.md)) is
  the first concrete customer for a debug heap — or for a one-shot PageHeap run.
- **mimalloc licensing** (MIT) is compatible with the LGPL 2.1 tree; vendoring it
  is a build-system change, not a code change.
- **IP hygiene** if studying external implementations: take the *pattern* (pools +
  arena + single manager interface — general region-allocator knowledge), never
  symbols/layout/disassembly from any shipped binary. OES builds its own.
