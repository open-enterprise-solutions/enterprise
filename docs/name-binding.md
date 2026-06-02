# Name binding — binds as the single source for context names

**Status:** landed 2026-06-02 (develop). Supersedes the manual
`m_methodHelper->AppendProp(...)` half of `PrepareNames` for context handles, scope
containers, export handles, and a module's own injected locals.

## The problem it replaces

Every runtime object used to expose its script-visible names twice:

- **`PrepareNames()`** built `m_methodHelper` by hand — `AppendProp("Value", …)`,
  `AppendProp("Controls", …)` — and resolved them in `GetPropVal/SetPropVal` through a
  private alias (`eSystem`). This fed the runtime resolver.
- The **designer IntelliSense** then re-read those helper props for autocomplete.

Two hand-maintained surfaces for the same names: easy to drift, and a handle that was
visible at runtime could be invisible (or crash) in the designer, or vice-versa.

A **bind** registers a name **once**, in the owning compile module, and both the runtime
binder and the designer read from there.

## The four bind kinds

`ibRuntimeModuleDataObject` (`backend/moduleInfo.h`) exposes:

| Call | Compile-side table | Editor name | Required? | Typed? | Use |
|------|--------------------|-------------|-----------|--------|-----|
| `BindContextVariable` | `m_listContextValue` | visible | **yes** | **yes** | self-handles: `ThisObject`, `ThisForm` |
| `BindScopeVariable`   | `m_listContextValue` (scope flag) | **hidden** (members only) | no | no | transparent scopes: `Manager`, `EnumManager`, `SystemManager` |
| `BindExportVariable`  | `m_listExternValue`  | visible | no | no | export handles: `RegisterRecords`, `Filter`, `Controls`, `DataSource`, global constants |
| `BindLocalVariable`   | `m_listLocalValue`   | visible | no | no | the module's OWN writable local: a constant's `Value` |

Each `Bind*` call routes to `EnsureCompileModule()->Add…` (so the compile module sees the
name as a declared variable) and, if the runtime binder already exists, forwards the value
to its slot via `SetVar`.

## Runtime — binder + pre-flight + opcodes

`ibByteCode::CreateBinder()` (`byteCode.h`) builds an `ibByteBinder` holding a
`std::vector<ibValue*>` indexed by frame slot. `ibRuntimeModuleDataObject::Run/Execute`
(`moduleInfo.cpp`) builds it from `m_listExternValue`, `m_listContextValue` and
`m_listLocalValue`, then `ibProcUnit::Execute` runs a **pre-flight** before dispatch
(`procUnit.cpp`, the `m_listVar` loop):

- **CONTEXT** (`ThisObject` / `ThisForm`): null slot → `Required binding not provided`
  error; class mismatch against the compile-time `clsid` → error. The handle's VALUE
  mutates (hence lazy access) but its TYPE is fixed, so the type check is sound.
- **EXTERN** (`RegisterRecords` / `Filter` / `Controls` / `DataSource`): null → skip
  (presence is irrelevant); no type check.
- **LOCAL** (a constant's `Value`): if the binder carries a value, fill the slot — no
  required check, no type check. An ordinary unbound local keeps its frame default.

`ibByteCodeVarInfo::IsBindable() = IsBindRequired() || Local` unifies External + Local for
binder slot sizing (`computeBinderSlotCount`) and `SetVar`: to the binder a local is
indistinguishable from an external — both just seed a slot.

Access opcodes (`codeDef.h`, **AOT format bumped to v13** in `byteCodeAOT.cpp` — the new
opcodes shifted the typed-op space behind `OPER_END`/`TYPE_DELTA1`):

| Kind | Read | Write |
|------|------|-------|
| EXTERN  | `OPER_GET_EXTERN`  | `OPER_SET_EXTERN`  |
| SCOPE   | `OPER_GET_SCOPE` (co-labels `OPER_GET_A`) | `OPER_SET_SCOPE` (co-labels `OPER_SET_A`) |
| CONTEXT | `OPER_GET_CONTEXT` | `OPER_SET_CONTEXT` |

A **LOCAL bind needs no opcode** — it is a plain frame slot read/written by the existing
`OPER_GET`/`OPER_SET`. The pre-flight redirects the slot at `&m_constValue`, so
`Value` / `Value = …` inside the constant module read/write the backing member directly.
This is the clearest proof that the binds — not the opcodes — do the `PrepareNames`
offload; `Value` lost its `AppendProp` with zero new opcodes.

Member access on a bound handle (`ThisForm.Controls`) resolves through
`FillHelperFromBinds(helper, eProcUnit)` + a `GetBoundValue(name)` fallback that reads
`m_listExternValue` directly (so it works in the designer where there is no ProcUnit).

## Designer — surfacing the same binds

`AttachRuntime` returns early for the designer, so there is no binder and no bytecode. The
code-editor IntelliSense instead reads the bind tables straight off the compile module
(`frontend/win/editor/codeEditor/codeEditorInterpreter.cpp`), walking the **descriptor**
parent chain (`ibRuntimeModuleDataObject::GetParent`) — the compile-module parent link is
dead in the designer (lazy creation order). Three loops push context, export and local
binds into the precompile symbol table.

### Trap — surfacing a LOCAL bind's value

A local bind (a constant's `Value` = `ibValue m_constValue;`) is an **embedded member of a
transient object**, held by raw pointer in the cached compile module. Surface it as a plain
**writable** local (`m_isContext = false`, so `Value = …` is accepted — a context handle
would be read-only), with its type for `Value.<member>` autocomplete supplied via:

```cpp
variable.m_valObject = kv.second->GetValue();   // NOT  = kv.second  (raw ibValue*)
```

`operator=(ibValue*)` would take an **owning `TYPE_REFFER`** (IncrRef → on the editor's
per-keystroke `ibPrecompileCode::Clear` a `DecrRef→delete`) to a **non-heap** address →
heap corruption. `GetValue()` returns a self-owned value **per type** — a deep copy for
primitives/strings (own buffer, lifetime decoupled), an IncrRef'd reffer for heap
aggregates — so it is safe either way.

**The criterion is what the pointer targets, not the bind kind:** a standalone heap object
→ an owning reffer is fine (this is why the export handles — `Controls`/`DataSource`, real
heap objects — do not need the `GetValue()` treatment); an embedded/transient member
(especially a primitive) → copy via `GetValue()`.

## What `PrepareNames` keeps

`PrepareNames` still exists and still runs `ExportNamesToHelper(eProcUnit)` for a module's
own exported procedures/functions. Only the hand-rolled handle/special-prop `AppendProp`
calls moved to binds.

## See also

- [`runtime-facade.md`](runtime-facade.md) — step 11, the binder factory + per-descriptor
  `m_binder`.
- [`value-const-reffer.md`](value-const-reffer.md) — `TYPE_CONST_REFFER` semantics behind
  the surfacing trap.
- [`module-manager-split.md`](module-manager-split.md) — the descriptor tree the binds hang
  off.
