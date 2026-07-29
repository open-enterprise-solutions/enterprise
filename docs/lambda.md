# Anonymous functions (lambdas)

First-class callable values in OES script: anonymous Function /
Procedure expressions, assignable to slots and event handlers, callable
through a variable. Default-arg values for primitive literals,
runtime-validated arg count, full debugger / eval visibility.

> **Status**: landed 2026-05-10; closure capture added 2026-05-11..12
> (`docs/closure-capture.md`). Build clean Debug/x86. Working copy
> uncommitted (experimental arc per project convention).

---

## Surface

Both syntax modes accept `Function` / `Procedure` as expression:

```vbs
// VBS — function with named locals + default
var add = Function(x, y = 10)
    Return x + y
EndFunction

button.OnClick = Procedure(sender)
    LogClick(sender.Name)
EndProcedure

users.Where(Function(u) Return u.IsActive EndFunction)

// Calls work like any function — default fills missing tail args
add(5)        // → 15  (y defaults to 10)
add(5, 20)    // → 25
add(1, 2, 3)  // → throws "Too many arguments to function value"
```

```c
// CES — same, brace-delimited
var add = Function(x, y = 10) { return x + y; };
button.OnClick = Procedure(sender) { LogClick(sender.Name); };
```

The opening keyword (Function / Procedure) selects the closer keyword
that the parser expects (`EndFunction` / `EndProcedure`) — same for
named declarations and anonymous lambdas.

Default-arg values are **literal primitives only**: number, string,
date, bool, null. No expressions, no object refs. This keeps lambda
values self-contained; nothing inside a lambda value depends on
creation-time external object lifetime.

---

## Runtime model

A lambda is an `ibByteCode::ibByteFunction` with `ibFnKind::Lambda` in
the bytecode's `m_listFunc`. Same path named functions use — single
source of truth for frame shape (paramCount / varCount / bCodeRet),
parameter metadata (`m_listParam` with byref + defaults, `m_listParamRealName`
with names), and locals (`m_listLocals` for eval).

The lambda **value** at runtime is `ibValueFunction` — an `ibValue` of
tag `TYPE_FUNCTION` (CLSID `VL_FUNC`). At the initial lambda landing this
was two fields (parent bytecode + func index); closure capture
(2026-05-11..12, see `docs/closure-capture.md`) added the captured-frame
chain. The current in-tree shape (`backend/compiler/procUnitValues.h` —
NOT `procUnit.cpp`; the values split out):

```cpp
class ibValueFunction : public ibValue {
    const ibByteCode* m_parentBc  = nullptr;
    long              m_funcIndex = -1;

    // Closure-capture fields (added with Phase A/B/C/D of closure):
    std::vector<std::shared_ptr<ibRunContext>> m_capturedFrames;
    bool                                       m_needsHeapFrame = false;
public:
    ibValueFunction() : ibValue(ibValueTypes::TYPE_FUNCTION) {}
    const ibByteCode::ibByteFunction* GetFunction() const {
        return &m_parentBc->m_listFunc[m_funcIndex];
    }
    void Execute(ibRunContext*, ibValue*, bool bDelta);
};
```

Dispatch swaps the lambda runtime's `m_pByteCode` to `m_parentBc` for
the call duration, runs `Execute`, restores on exit (incl. exceptions).
The runtime is the session's shared lambda shim
(`ibProcUnitState::m_lambdaRuntime`) — a single ProcUnit per session
whose parent chain anchors Catalogs / Documents / Manager / common
modules in its frame array. Re-entrant lambda-in-lambda calls are safe
because `ibProcUnit::Execute` snapshots `m_pByteCode` at entry.

Lifetime: `m_parentBc` points into session's bytecode storage. Lambda
values must not outlive the session that produced them. No cross-
session value transfer (no serialization path for `ibValueFunction`).

---

## Bytecode opcodes

Three lambda-related opcodes in `compiler/codeDef.h` (`OPER_LFUNC`,
`OPER_ENDLFUNC`, `OPER_CALL_LAMBDA`). The earlier `OPER_FUNC_PTR`
materialise opcode is fully removed — `OPER_LFUNC` itself materialises
the value; no token survives in `codeDef.h`.

| opcode | role | param layout |
|---|---|---|
| `OPER_LFUNC` | lambda body entry; materialises an `ibValueFunction` value at the dest slot. Distinct from `OPER_FUNC` so the containing named-function's module-init skip-through (`while not OPER_ENDFUNC`) doesn't terminate prematurely on a nested lambda's terminator. | `m_param1` = dest slot, `m_param2.m_numIndex` = end IP (matches OPER_ENDLFUNC, used for body skip), `m_param3.m_numIndex` = `funcIndex` into `m_listFunc` |
| `OPER_ENDLFUNC` | lambda body close — pairs with `OPER_LFUNC`. Falls through to `lCodeLine = lFinish; break` like `OPER_ENDFUNC`. | — |
| `OPER_CALL_LAMBDA` | dynamic counterpart of `OPER_CALL` — call target is read from a slot at runtime, must wrap an `ibValueFunction`. Caller-supplied arg count is stamped at compile (validated at runtime against the lambda's param count); missing tail filled from `m_listParam[i].m_defaultValue`. | `m_param1` = return-value dest, `m_param2.m_numIndex` = caller-supplied arg count, `m_param4` = source slot holding the callable |

`OPER_CALL_LAMBDA`'s param-bind loop reuses the same inline `OPER_SET` /
`OPER_SETCONST` instructions that follow `OPER_CALL` and `OPER_CALL_METHOD`.

---

## Compile-side flow

### `CompileLambdaExpression` (anonymous — from expression)

Hooked into `GetExpression` after the `KEY_NEW` branch — when next
lexem is `KEY_FUNCTION` or `KEY_PROCEDURE`, step back and call:

```cpp
ibParamUnit ibCompileCode::CompileLambdaExpression(ibCompileContext* context)
{
    // 1. Parse signature into a context that parents into m_rootContext.
    //    Strict isolation: m_parentContext = nullptr after parse so
    //    body sees only own params/locals + root-level Context bindings
    //    (via bc parent walk + the rootCtx fallback in GetVariable).
    ParseFunctionSignature(m_rootContext, ..., allowAnonymous=true)
    functionContext->m_parentContext = nullptr;

    // 2. Emit OPER_LFUNC + FUNC_PARAM declarators + body + OPER_ENDLFUNC
    //    inline. EmitFunctionBody also pushes the lambda to m_listFunc
    //    with kind = Lambda + synthetic name "<lambda@<lo>>".
    EmitFunctionBody(context, createdFunction, functionContext)

    // 3. Allocate the dest slot in CALLER's context — that's where the
    //    ibValueFunction value lands at runtime.
    ibParamUnit target = context->CreateVariable();

    // 4. Back-patch OPER_LFUNC's operands: dest slot, end IP,
    //    funcIndex (= m_listFunc.size() - 1 right after the push).
    listCode[lfuncIp].m_param1 = target;
    listCode[lfuncIp].m_param2.m_numIndex = endlfuncIp;
    listCode[lfuncIp].m_param3.m_numIndex = m_listFunc.size() - 1;

    return target;
}
```

### `EmitFunctionBody` (shared with named functions)

For lambdas, the same path that emits OPER_FUNC + FUNC_PARAM declarators
+ body + OPER_ENDFUNC for named functions emits OPER_LFUNC + ... +
OPER_ENDLFUNC for lambdas. The single discriminator is
`IsReturnLambda(functionContext->m_numReturn)`; both finish by pushing
an `ibByteFunction` to `m_listFunc`. For lambdas the `m_kind` is
overridden to `Lambda` and `m_strRealName` is set to `<lambda@<lo>>`.

`m_listLocals` and `m_listParam` (with names + byref + defaults) come
from the templated ctor `ibByteFunction(long, const CompileFn&)` —
identical to named-function path.

### Caller-side emit (`OPER_CALL_LAMBDA`)

When `(` follows an identifier, three branches in order in
`GetCurrentIdentifier`:

1. `m_rootContext->FindFunction(...)` → `OPER_CALL_METHOD` (context-method).
2. `context->FindVariable(...)` resolves to a regular Local / Export
   variable → `OPER_CALL_LAMBDA` on its slot. Caller-supplied arg count
   stamped in `m_param2.m_numIndex` (= `listParam.size()`).
3. Fallback to `GetCallFunction` → resolves as named function or
   queues for forward-ref binding.

Branch #2 is what unlocks `var f = Function(...)EndFunction; f(arg)`.

---

## Compile discipline (lambda body scoping)

Lambda body sees ONLY:

- own params + own locals
- root-level **Context bindings** via two complementary paths:
  - bytecode parent walk (`bc.m_parent` chain → topmost bc with
    `m_parent == nullptr` reached; `Catalogs.X` / `Documents.X` /
    `Constants.X` / `Manager.X` / `CommonModules.X.method()`)
  - `m_rootContext` fallback in `GetVariable` for entries with
    `m_bContext == true` (system functions / host-injected bindings
    like codeRunner's `ValueOutput` — these live in the
    CompileContext layer, not bc, so the bc walk alone misses them)

Body does NOT see: enclosing function's locals, module-level vars of
the lambda's own module, nested-procedure scope. Caller passes
anything else as args at invocation.

The rootCtx fallback **filters by `m_bContext`** — user `var x = ...`
declarations in rootCtx (codeRunner sandbox case where module-level
declarations land directly in root) stay invisible. Phase B isolation
preserved.

### Closure capture — landed 2026-05-11..12

Initially rejected (2026-05-09 design review) for simplicity, then
landed across Phase A/B/C/D/F via per-frame heap-promotion. See
`docs/closure-capture.md` for the mechanism. Default-arg expressions
remain rejected — defaults stay literal primitives only (cross-runtime
invocation safety + simplicity). Lambda body can now reach outer
function locals through chained `shared_ptr<ibRunContext>` frames
wired into `m_pppArrayList[1..N]` at invoke time.

Explicit-arg capture (still supported, sometimes cleaner than closure):

```vbs
Procedure Init()
    var ctx = New("Structure")
    ctx.Insert("prefix", "User:")
    button.OnClick = Function(s, c) Return c.prefix + s EndFunction
    // caller of button.OnClick provides ctx on each invocation
EndProcedure
```

---

## Runtime arg-count handling

Each `OPER_CALL_LAMBDA` carries the caller's actual arg count (= number of
inline `OPER_SET` / `OPER_SETCONST` ops emitted right after).

| caller args vs lambda paramCount | runtime behavior |
|---|---|
| `caller > paramCount` | throw "Too many arguments to function value: passed N, expected at most M" |
| `caller == paramCount` | consume N inline SET/SETCONST ops, dispatch |
| `caller < paramCount`, missing tail has defaults | consume caller's ops, fill `[caller, paramCount)` from `m_listParam[i].m_defaultValue` (which lives on the lambda's `ibByteFunction`) |
| `caller < paramCount`, missing tail has no default | throw "Missing required argument '<name>' to function value" — uses param name from `m_listParamRealName[i]` (or `pN` fallback) |

Strict opcode validation in the param-bind loop: a non-`OPER_SET` /
`OPER_SETCONST` opcode is treated as bytecode tape corruption (not a
user error) and throws.

---

## Debugger / eval support

Because the lambda lives in `m_listFunc` like a named function, the
existing debugger / eval infrastructure works without lambda-specific
code:

- **Call stack** — `runContext->m_currentFunction` is set by
  `OPER_CALL_LAMBDA` to the lambda's `ibByteFunction`. Debugger renders
  `<lambda@N>(p0=…, p1=…)` from `m_strRealName` + `m_listParamRealName`
  + `m_listParam`. (Pre-fix it inherited `<initializer>`.)
- **Eval / watch by name** — `m_compileModule->GetEvalHostFunction()`
  returns the lambda's `ibByteFunction`; `m_listLocals` carries
  param + local names so the eval expression's GetVariable resolves
  them at depth 1 of the lambda's frame.
- **AOT cache** — no special path; `byteCodeAOT.cpp` serializes
  `m_listFunc` wholesale, lambdas included.

Name-keyed lookups in `m_listFunc` (`FindFunction`, `FindMethod`,
`FindExportFunction`, etc.) are safe because:

1. Lambda's `m_strRealName` is `<lambda@N>` — `<` prefix isn't a valid
   identifier, so no user code can accidentally match it.
2. `IsCrossBcVisible()` returns `false` for `Lambda` kind, so cross-bc
   resolvers explicitly skip them.

---

## AOT format

Bumped to **v5** (2026-05-10 PM) at the lambda landing; current
format version in tree is **v18** (`byteCodeAOT.cpp::kAOTFormatVersion`)
after subsequent bumps for closure capture (v10), a CLSID encoding
switch (v12), the LINQ push-down lambda-AST payload (v14), kind-typed
CLSIDs (16), the `restrict`-pushdown-AST fix (17) and the shortLet
peephole (18). Always read the constant from the source file rather
than this prose — it drifts every few commits.
v3→v4 added the `m_param2` arg-count stamp on
`OPER_CALL_LAMBDA`; v4→v5 swapped the lambda metadata model from
inline `ibLambdaInfo` (operand-resident: paramCount/varCount/bCodeRet
on OPER_LFUNC's m_param3/m_param4) to `m_listFunc`-resident
`ibByteFunction` (single source of truth, frame shape derived via
`funcIndex`). Old caches reject as miss → recompile + repopulate.

---

## Touched files (canonical list)

Backend / compiler:

- `src/engine/backend/compiler/codeDef.h` — 3 live lambda opcodes (`OPER_LFUNC`, `OPER_ENDLFUNC`, `OPER_CALL_LAMBDA`); the earlier `OPER_FUNC_PTR` materialise opcode is gone (`OPER_LFUNC` absorbed value-materialisation)
- `src/engine/backend/compiler/byteCode.h` — `ibFnKind::Lambda` enum + `IsLambda()` helper, `IsCrossBcVisible()` filter
- `src/engine/backend/compiler/byteCodeAOT.cpp` — AOT format (read `kAOTFormatVersion`; v18 in tree)
- `src/engine/backend/compiler/compileContext.h` — `RETURN_LAMBDA_FUNCTION` / `RETURN_LAMBDA_PROCEDURE` enum kinds
- `src/engine/backend/compiler/compileContext.cpp` — lambda body scope discipline + rootCtx fallback for Context bindings
- `src/engine/backend/compiler/compileCode.h` — split helper decls (`ParseFunctionSignature`, `EmitFunctionBody`, `CompileLambdaExpression`)
- `src/engine/backend/compiler/compileCode.cpp` — lambda compile + push to `m_listFunc` with kind=Lambda + Variable-callable branch + `OPER_CALL_LAMBDA` emit with arg-count stamp
- `src/engine/backend/compiler/procUnit.cpp` — runtime cases for `OPER_LFUNC` / `OPER_ENDLFUNC` / `OPER_CALL_LAMBDA` (+ `OPER_CALL_CLOSURE`, the heap-frame named-call split)
- `src/engine/backend/compiler/procUnitValues.h` — `ibValueFunction` class declaration (`TYPE_FUNCTION`, closure `m_capturedFrames`)
- `src/engine/backend/compiler/procUnitState.h` — `GetLambdaRuntime()` accessor
- `src/engine/backend/session/session.cpp` — lambda runtime shim setup in `CompileRoot` (parent chain to root + common modules)

Frontend / editor:

- `src/engine/frontend/win/editor/codeEditor/codeEditor.h` — fold parser CES brace support
- `src/engine/frontend/win/editor/codeEditor/codeEditorParser.{h,cpp}` — `ibContentType::eLambda` + anonymous-lambda body skip + depth-aware body walk
- `src/engine/frontend/win/editor/codeEditor/codeEditorInterpreter.cpp` — `RETURN_LAMBDA_FUNCTION` / `RETURN_LAMBDA_PROCEDURE` in precompiler

Host:

- `src/engine/codeRunner/codeRunner.{h,cpp}` — VBS/CES syntax dropdown

---

## Design history

The current shape (option A — lambda in `m_listFunc`) is the third
iteration of the implementation. Earlier rounds:

- **Phase A (2026-05-09 AM)** — first-class anonymous functions inline
  in parent's bytecode, with their own `ibByteFunction` in `m_listFunc`.
  Worked but we tried to optimise it.
- **Phase B (2026-05-09 PM)** — removed the `ibByteFunction` from
  `m_listFunc`; lambda metadata lived in `OPER_LFUNC` operands +
  `BuildOrGetLambdaInfo` cache on `ibProcUnitState`. Strict body-scope
  discipline (`m_parentContext = nullptr`) added. The `m_listFunc`
  removal turned out to be a premature optimisation — debugger
  immediately broke (stack showed `<initializer>`, eval couldn't see
  locals because `m_listLocals` was discarded).
- **2026-05-10 (option A revival)** — pushed lambda back into
  `m_listFunc` with a `Lambda` kind marker. Single source of truth.
  `ibLambdaInfo` removed; `ibValueFunction` shrank to `(parentBc,
  funcIndex)`. Phase B's body-scope discipline retained.

**Lesson**: "anonymous" is a property of the **lookup**, not of
storage. Anonymous functions are still functions; they belong wherever
named functions are. Keeping them out of `m_listFunc` saves storage
that doesn't matter and breaks the infrastructure that already knows
how to deal with functions.

A few independent fixes landed alongside option A:

- **`OPER_CALL_LAMBDA` arg-count stamp** (`m_param2.m_numIndex` = caller
  arg count). v4 cache rows had `m_param2` at 0 → dispatch treated
  every dynamic call as "0 args supplied" and threw missing-required
  even when the source explicitly passed args.
- **Const-arg audit** (`OPER_NEW`, `OPER_CALL`, `OPER_CALL_LAMBDA`) —
  fixed a class of latent bugs where const literal args triggered
  "Attempt to write to a constant value" under specific binding
  shapes. Net win for non-lambda OES users too.
- **`ENDLFUNC` fall-through** — early Phase B iterations had the
  case-block as a NOP, which let body flow continue past the
  terminator into the caller's `OPER_FUNC_PTR + OPER_CALL_LAMBDA` →
  infinite recursion. Resolved by the standard
  `OPER_RET → OPER_ENDFUNC → OPER_END` fallthrough chain.

---

## Out of scope

- **Default-arg expressions** — defaults stay literal primitives only.
  Cross-runtime invocation safety + simplicity.
- **Module-level var visibility from lambda body** — caller passes
  everything via args. (Closure capture, landed 2026-05-12, makes
  outer-FUNCTION locals visible — module-level vars are a different
  axis.)
- **Lambda value serialisation** — `ibValueFunction` is runtime-only,
  not persistable across sessions / processes. The bytecode template
  IS AOT-cacheable through the standard pipeline; only the dynamic
  value isn't.
- **Lifetime safety beyond session** — raw `const ibByteCode*` on
  lambda value. Parent's bytecode death = session crash; lambda death
  is part of that crash.
- **Arrow syntax** `(x) => expr` — pure parser sugar over
  `Function/EndFunction`. Add when usage pressure surfaces it.

**See also:** [event-dispatcher.md](event-dispatcher.md) — a lambda assigned to an event (or a command's Action) is the
"lambda as an event handler" consumer of anonymous functions: `ibValueFunction` itself implements the event-dispatch
facet, so a runtime-assigned lambda fires through the same `CallAsEvent` door as a classic named handler.
