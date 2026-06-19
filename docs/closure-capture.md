# Lambda closure capture

> **Status**: LANDED 2026-05-11..12 — Phase A/B/C/D/F all in working
> tree. AOT v14 (read `byteCodeAOT.cpp::kAOTFormatVersion`; v10 at the
> closure landing, bumped since). Build clean Debug/x86. Working copy
> uncommitted (experimental arc per convention).
>
> Mechanism: per-frame heap promotion (v2). v1 used per-slot boxing
> + `FindOrAddCapture` linear walk — rolled back as wrong shape
> (violates single-pass + no-search-collections invariants). v2
> sidesteps by making the capture chain per-frame: the whole
> **`ibRunContext`** is heap-promoted (it inherits
> `enable_shared_from_this`), and the lambda holds
> `std::vector<std::shared_ptr<ibRunContext>> m_capturedFrames`
> (`compiler/procUnitValues.h`). Runtime wires `m_pppArrayList[1..N]`
> from each captured context's `m_pRefLocVars` per invoke; existing
> OPER_GET/SET with depth ≥ 1 work unchanged.
>
> **NB:** there is NO separate `HeapFrame` struct — the design sketch
> below (a `struct HeapFrame { vector<ibValue> locals; }`) was the early
> shape; the landed code heap-promotes `ibRunContext` itself. Read the
> mechanism as "captured = `shared_ptr<ibRunContext>`" throughout.
>
> See `docs/lambda.md` for the base lambda machinery this builds on.

---

## Semantics (unchanged from v1)

```c
function makeAdder(n) {
    return Function(x) { return x + n; };   // captures n
}
var add5 = makeAdder(5);
Message(add5(10));   // → 15
```

* **Lexical scope** — capture sees defining environment, not call site.
* **Live reference** — outer mutations visible inside lambda; inner
  mutations visible outside (when outer frame still alive).
* **Full parent chain** — nested lambdas can reach any enclosing
  function's locals through chained shared_ptr frames.
* **Compile-time resolved** — symbol resolution at compile decides
  depth; runtime is dumb dereference.

Counter / live-link example:
```c
function counter() {
    var i = 0;
    var bump = Function() { i = i + 1; return i; };
    return bump;
}
var c = counter();
Message(c());   // → 1
Message(c());   // → 2  (i lives between calls)
```

LINQ chain capture (closes pivot risk #5):
```c
var threshold = 5;
var bigOnes = arr.Where(Function(x) { return x > threshold; }).ToArray();
```

---

## Mechanism — heap-promoted run context, NOT per-slot boxing

A function that has any inner lambda gets its **entire `ibRunContext`
heap-allocated** (`make_shared<ibRunContext>`, the context inherits
`enable_shared_from_this`). The lambda holds a `shared_ptr` to that
context in its `m_capturedFrames` vector. The context — and its locals
array `m_pRefLocVars` — stays alive as long as some lambda holds a
reference, surviving outer's return.

Landed shape (`compiler/procUnitValues.h`):

```cpp
class ibValueFunction : public ibValue {
    const ibByteCode* m_parentBc  = nullptr;
    long              m_funcIndex = -1;
    // Captured chain — direct enclosing fn at [0], outer-outer at [1],
    // etc. Per-reference append at materialise; no dedup needed
    // because same shared_ptr regardless of how many lambdas capture
    // it. Vector size = nesting depth (≤ 3-5 typical for real code),
    // not per-slot.
    std::vector<std::shared_ptr<ibRunContext>> m_capturedFrames;
    bool m_needsHeapFrame = false;   // cached from this lambda's own ibByteFunction
};
```

(The earlier sketch named a `struct HeapFrame { vector<ibValue> locals; }`;
no such struct exists — the unit of capture is the whole `ibRunContext`.)

**Why NOT per-slot boxing** (v1's path, rolled back):
* v1 wanted `m_lambdaCaptures: vector<ibLambdaCapture{outerSlotIdx, captureLevel}>` on each lambda. Required `FindOrAddCapture` to dedup per-reference appends. Linear search on every lambda-body identifier resolution — exactly the "search-collection in single-pass" anti-pattern user flagged.
* v2 sidesteps this entirely: capture chain is per-frame (not per-slot). Slot resolution at runtime is direct `m_pppArrayList[depth][slotIdx]` — existing OPER_GET/OPER_SET dispatch works unchanged.

**Why NOT new opcodes**:
* v1 reserved 4 new opcodes (`OPER_GET_CAPTURED / SET_CAPTURED / GET_BOXED / SET_BOXED`). v2 reuses existing `OPER_GET / OPER_SET` with `depth ≥ 1` in `m_param2.m_numArray`. Lambda's m_pppArrayList already supports the chain (currently used for root-context bindings). Adding outer-fn-locals is just chain extension.

---

## Compile-time

A function's `ibCompileContext` gains a single boolean:

```cpp
struct ibCompileContext {
    bool m_needsHeapFrame = false;   // set on first inner-lambda encounter
    // ... existing fields ...
};
```

**During lambda body parse** (`CompileLambdaExpression`):
1. Mark `enclosing_function_context->m_needsHeapFrame = true`. One flag, no search.
2. Inside lambda's body parsing, when resolving an identifier:
   * Found in own scope (lambda's locals) → emit `OPER_GET` with `depth=0`.
   * Found in 1st enclosing function's scope → emit `OPER_GET` with `depth=1`.
   * Found in N-th enclosing function's scope → emit `OPER_GET` with `depth=N`.
   * Found in root context (Catalogs etc) → existing depth (rootCtx fallback, unchanged).
3. Per-reference: simple `OPER_GET` emit. No capture-vector push, no `FindOrAdd`.

**Identifier resolution walk** in `ibCompileContext::GetVariable`:
Currently lambda discipline restricts to root context. Extend:
* Walk `m_parentContext` chain.
* Track `captureDepth` counter (incremented per lambda boundary crossed).
* On hit in some ancestor function frame F → emit OPER_GET with that depth.
* On hit in root context → unchanged (depth = root-shim layer).

Single pass over identifier reference. No collection-searches.

---

## Runtime

### Function entry — heap-allocate when needed

`OPER_FUNC` dispatch (function body entry):
* If function's `m_needsHeapFrame` (from bc-side `ibByteFunction`): `make_shared<HeapFrame>` sized to `m_lVarCount`.
* `m_pppArrayList[0]` points into `heapFrame->locals.data()`.
* Otherwise (no inner lambda): current stack-frame behavior unchanged.

### Lambda materialise — capture chain

`OPER_LFUNC` (instantiate ibValueFunction):
* Walk current run context's frame stack. For each enclosing function frame (those with HeapFrame), copy shared_ptr into lambda's `m_capturedFrames`.
* Vector size = number of enclosing fn frames currently live. Trivial single-pass walk, no search.

```cpp
// Sketch:
auto lambda = make_shared<ibValueFunction>(bc, funcIdx);
for (frame : enclosing_fn_frames_with_heap) {
    lambda->m_capturedFrames.push_back(frame.heapFramePtr);
}
```

### Lambda invoke — restore chain into m_pppArrayList

`OPER_CALL_LAMBDA`:
* `m_pppArrayList[0]` = lambda's own frame (its locals).
* `m_pppArrayList[1..N]` = pointers into `m_capturedFrames[0..N-1]->locals`.
* `m_pppArrayList[N+1..]` = existing shim's root-context layers (unchanged).

OPER_GET / OPER_SET in lambda body use these depths directly. No new dispatch logic.

### Frame lifetime

Outer function returns:
* Frame's shared_ptr refcount decrements.
* If lambda holds a copy (escaped lambda), frame stays alive.
* If lambda doesn't escape (called only within outer's body and discarded), refcount → 0, frame freed.

---

## What changes vs current state

| Layer | Change |
|-------|--------|
| `ibCompileContext` | `bool m_needsHeapFrame` |
| `ibByteFunction` | mirror `bool m_needsHeapFrame` |
| `GetVariable` (compile) | walk parent chain past lambda boundaries; mark `m_needsHeapFrame` on hit function; return depth |
| `ibValueFunction` | `vector<shared_ptr<HeapFrame>> m_capturedFrames` |
| Runtime — function entry | heap-alloc HeapFrame when `m_needsHeapFrame` |
| Runtime — OPER_LFUNC | populate lambda's `m_capturedFrames` from enclosing frames |
| Runtime — OPER_CALL_LAMBDA | wire `m_pppArrayList[1..N]` to captured frames' locals |
| OPER_GET / OPER_SET | unchanged — already dispatch on `depth` |
| AOT | `m_needsHeapFrame` per fn serialized (v14 in tree — read `byteCodeAOT.cpp::kAOTFormatVersion`; v10 at closure-capture landing, bumped since by a CLSID encoding switch (v12) and the LINQ push-down lambda-AST payload (v14)) |
| Debugger Locals | walk lambda's `m_capturedFrames`, render with origin fn name |

**Zero new opcodes**. Zero new compile-side `Find*` helpers. Single boolean per fn, single vector per lambda (small).

---

## Phase landings (all in working tree)

* **Phase A** (compile-side resolve + emit) — `m_needsHeapFrame` lazy on `ibCompileContext::ibFunction`, mirror on `ibByteFunction`. `CompileLambdaExpression` keeps `m_parentContext = context` (was strict-isolation `nullptr`). `GetVariable` walks parent chain past lambda boundary, tracks `crossedLambda` to mark on capture-hit only.
* **Phase B** (runtime) — `ibRunContext : enable_shared_from_this` + raw `m_parentRunContext` chain. New `OPER_CALL_CLOSURE` opcode (split from OPER_CALL) for callees with `m_needsHeapFrame`. `OPER_CALL_LAMBDA` temp-swaps shim's `m_pppArrayList`: `[1..N]` from captured frames, `[N+1..]` from shim's root layers. Try/catch restores on exception. Hot OPER_CALL stays probe-free.
* **Phase C** (nested lambdas) — chain capture works through the same materialise walk; outer lambda's frame is heap-promoted iff it has its own inner lambda.
* **Phase D** (LINQ chain syntax unlocked) — `arr.Where(Function(x){ return x > threshold; })` captures `threshold`. Validated via `test_closure_linq.txt`.
* **Phase E** (AOT v10) — `m_needsHeapFrame` per function. Format version bumped from v9 at the closure landing; later raised to v12 (CLSID encoding) and v14 (LINQ push-down lambda-AST).
* **Phase F** (Debugger Locals) — `SendLocalVariables` walks `m_parentRunContext` chain, labels captured-frame entries `<fn>.<var>`, skips non-heap-promoted ancestors (those belong to stack frames, not Locals).

## Risks (residual after landing)

1. **Heap-alloc per function call** for fns with inner lambda. Mitigation deferred: small-object pool / arena.
2. **Escape analysis** not done — every fn-with-lambda heap-allocates even if lambda doesn't escape. Future optimization: detect non-escaping case, keep stack frame.
3. **Frame lifetime** — outer's locals live beyond outer's return when lambda holds reference. Matches JS / Python; users manage.
4. **AOT migration** — v9 → v10 invalidated caches at closure landing; subsequent bumps to v12 / v14 invalidate the same way. One-time recompile.

---

## Decisions to remember

* **Per-frame, not per-slot.** Entire outer frame heap-allocated, one shared_ptr per nested level. No per-slot promotion decisions.
* **Existing opcodes**. OPER_GET / OPER_SET with `depth ≥ 1` already work via m_pppArrayList chain — closure just extends the chain.
* **Single boolean per fn**. `m_needsHeapFrame` set during lambda body parse. No vector of captured-slot records.
* **Materialise-time capture**. Lambda copies enclosing frames' shared_ptrs into `m_capturedFrames`. Append-only, no dedup needed (same frame's shared_ptr = same target).
* **Identifier resolution = depth assignment**. Compile walks parent chain past lambda boundary, returns `OPER_GET depth=N` where N is the enclosing fn frame's position in lambda's m_pppArrayList.

---

## Files touched

| File | Role |
|------|------|
| `compiler/compileContext.{h,cpp}` | `m_needsHeapFrame` on inner `ibFunction`; lazy mark in `GetVariable`; `crossedLambda` flag |
| `compiler/compileCode.cpp` | `m_parentContext = context` in `CompileLambdaExpression`; `OPER_CALL_CLOSURE` emit in PushCallFunction; templated ctor mirrors `m_needsHeapFrame` |
| `compiler/byteCode.h` | `m_needsHeapFrame` on `ibByteFunction` + mirror init |
| `compiler/codeDef.h` | `OPER_CALL_CLOSURE` opcode value (after `OPER_CALL_METHOD`) |
| `compiler/procContext.h` | `ibRunContext : enable_shared_from_this` + `m_parentRunContext` raw chain |
| `compiler/procUnit.cpp` | OPER_CALL_CLOSURE handler (split from OPER_CALL); OPER_LFUNC captures via parent-walk; OPER_CALL_LAMBDA wires `m_pppArrayList[1..N]` |
| `compiler/procUnitValues.h` | `ibValueFunction` with `m_capturedFrames` + cached `m_needsHeapFrame` |
| `compiler/byteCodeAOT.cpp` | `kAOTFormatVersion` bumped to 10 here (now 14 in tree — read the constant) |
| `backend/debugger/debugServer.cpp` | `SendLocalVariables` walks chain, labels `<fn>.<var>` |

## v1 rollback (historical)

v1 used per-slot boxing + `FindOrAddCapture(name, outerSlot, captureLevel)` helper. Rolled back because linear walk before append violates single-pass + no-search-collections invariants. v2 (per-frame, this design) sidesteps entirely — capture chain is per-frame, append-only, small.
