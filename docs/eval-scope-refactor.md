# Eval / scoped bindings refactor

> **Status:** LANDED 2026-05-02. Cross-bc resolver unified onto
> bc-side vector storage with kind discriminator. Tests 1-7 from the
> original second-pass plan pass on `fb_test251` (Debug|x86).
> Follow-up candidates documented at end-of-doc.

## Final architecture

### bc-side data layout

```cpp
struct ibByteCode {
    std::vector<ibByteCodeVarInfo>  m_listVar;    // 5 ibVarKind
    std::vector<ibByteFunction>     m_listFunc;   // ibFnKind (3 at this arc; +Lambda, +Protected since)
    // m_listCode, m_listConst, m_dependencies, m_id, m_version, …
};

struct ibByteFunction {
    std::vector<ibByteCodeVarInfo>  m_listLocals; // function-frame scope
    // m_lCodeLine, m_listParam, m_kind, m_strRealName, m_strContext, m_parentRef, …
};
```

Kinds:

| Var kind     | Meaning                                                        | Slot semantic                       |
|--------------|----------------------------------------------------------------|-------------------------------------|
| Local        | user-private frame var                                         | frame slot                          |
| Export       | user-declared export                                           | frame slot, cross-bc visible        |
| External     | runtime-bound, no helper                                       | extern slot (binder fills)          |
| Context      | top-level binding (`Manager` / `ThisForm`)                     | extern slot                         |
| ContextProp  | prop of a Context binding (`Catalogs` of `Manager`)            | prop-idx in parent's helper         |

| Fn kind        | Meaning                                                  | Dispatch        |
|----------------|----------------------------------------------------------|-----------------|
| Local          | user-private function                                    | OPER_CALL       |
| Export         | user-declared export function                            | OPER_CALL       |
| ContextMethod  | method of a Context binding (`GetForm` of `Manager`)     | OPER_CALL_METHOD |

> Two `ibFnKind` values landed after this arc and are NOT covered here:
> **Lambda** (anonymous function, cross-bc invisible — `docs/lambda.md`) and
> **Protected** (the `Protected` access modifier — visible to children only).
> `IsCrossBcVisible()` is `Export || ContextMethod`.

Cross-table: `ContextProp.m_parentRef` and `ContextMethod.m_parentRef`
index into `m_listVar` (parent Context binding entry).

### Helpers

On `ibByteCodeVarInfo`: `IsLocal / IsExport / IsExternal / IsContext /
IsContextProp / IsBindRequired / IsUserLocal`.

On `ibByteFunction`: `IsLocal / IsExport / IsContextMethod /
IsCrossBcVisible`.

On compile-side `ibVariable` / `ibFunction`: `IsContextRelated()` for
the binding-or-prop predicate.

### Binder

`ibByteBinder` reads `bc.m_listVar` directly, filtering
`IsBindRequired()` (External + Context). Pre-flight in `Execute`
verifies each required slot is wired and clsid matches.

`ibByteBinding` / `ibBindingKind` / `m_listRequiredBindings` —
removed.

### Compile-side

`ibCompileCode` base exposes virtuals:

```cpp
virtual bool IsExpressionOnly() const { return false; }
virtual const ibByteCode::ibByteFunction* GetEvalHostFunction() const { return nullptr; }
```

`ibCompileEval` (in `procUnit.cpp`) overrides both. Two ctors:

```cpp
explicit ibCompileEval(ibRunContext* runCtx);                   // runtime-driven
ibCompileEval(const ibByteCode* hostBc,
              const ibByteCode::ibByteFunction* hostFn);        // explicit pair
```

### Descriptor

`ibRuntimeModuleDataObject::ExportNamesToHelper(ibValue::ibValueMethodHelper*, long alias)`
— protected. Replaces 10 copy-paste `for (auto e : m_listExportFunc /
m_listExportVar)` loops in business-object subclasses.

## Resolve flow

1. **In-module compile**: parser hits identifier → `m_rootContext->FindVariable(name, var, contextVar=true)`. Walks own `m_listVariable` map → recurses into parent compile-contexts → falls back to bc.m_parent chain at root.

2. **bc fallback**: `bc->FindVariable(name, var)` — single `find_if` over `m_listVar` with `!IsLocal()` filter (privates not visible cross-bc).

3. **Identifier emit (compileCode.cpp:1916)**:

```cpp
const bool isContextProp =
    m_rootContext->FindVariable(strRealName, foundedVar, true) &&
    foundedVar && !foundedVar->m_strContext.IsEmpty();

if (isContextProp) {
    // OPER_GET_A on parent binding (foundedVar->m_strContext) + prop name const
} else {
    // GetVariable → OPER_GET on frame slot (handles bare bindings + Locals + auto-add)
}
```

4. **Eval expressions**: `ibCompileEval(pRunContext)` ctor sets
   `m_cByteCode.m_parent = host bc`, `m_cByteCode.m_bExpressionOnly =
   true`, `m_evalHostFunction = host's currentFunction`. Resolve walks
   bc parent chain genealogically — ThisForm / ThisObject visible
   from form's eval; foreign forms not in chain so isolation is
   automatic.

## Critical fix: Pass-3 self-prop skip

ThisForm/ThisObject expose a self-named prop (prop "ThisForm" of
binding "ThisForm"). The Pass-3 push via `PushVariable` would
overwrite the Pass-2 binding entry in `m_listVariable` via
`insert_or_assign`. After the bc mirror this left only kind=ContextProp,
no kind=Context — binder pre-flight skipped the slot, runtime read
garbage.

In HEAD this was masked by the parallel `m_listExternValue` array on
bc (removed in step 2c when External/Context became the must-bind
set on `m_listVar` directly).

Fix in `compileCode.cpp::PrepareModuleData` Pass 3:

```cpp
if (stringUtils::CompareString(propName, pair.first))
    continue;  // self-prop skipped — binding owns its own name
```

## What still feels redundant (next-session candidates)

> **Status (2026-06-22): 6 of 7 landed** (`Debug|x86` green). Done — the compile-side
> kind enum on `ibVariable` / `ibFunction` (the three `m_bExport` / `m_bContext` /
> `m_bExternal` booleans collapse to a single `ibVarKind` / `ibFnKind m_kind`, reusing
> the bc-side enum; `m_access` stays a **separate** visibility axis so the parent-chain
> gate is untouched, and the bc-mirror ctors become a straight `m_kind` copy);
> `m_strName` / `m_strRealName` redundancy (dead field dropped, `m_strRealName` is the
> sole name); `ibCompileContext::m_listVariable` / `m_listFunction` map → vector (keyed
> by `m_strRealName` via case-insensitive `find_if`, map upsert preserved as
> find-or-replace); `ibByteFunction::m_lCodeParamCount` (→ `m_listParam.size()`);
> `ibByteFunction::m_listParamRealName` (folded into `ibByteParam::m_strName`, **AOT
> format v14 → v15**); `ibCompileEval` ctor delegation. **Still open:** the registry
> `clsid → ibValueMethodHelper*` (blocked by helper-template lifecycle — AOT persistence
> lands first). Subsections below kept as the design record.

### Compile-side `ibVariable` / `ibFunction` — kind enum  *(landed 2026-06-22)*

Compile-side still holds 5 booleans per ibVariable
(`m_bExport / m_bContext / m_bExternal / m_bTempVar / m_bScoped`).
Symmetric to bc-side: introduce `ibVarKind m_kind` (and
`ibFnKind m_kind` on ibFunction), drop 4 of 5 booleans.

Effect: `AddVariable(name, type, bExport, bContext, bTempVar)` →
`AddVariable(name, kind, type)`. `IsContextRelated()` →
`IsContext() || IsContextProp()`. The
`stampOnContext` lambda in PrepareModuleData disappears.

### `m_strName` vs `m_strRealName` redundancy

`m_strName` historically held the upper-cased map key. After the
vector flip + case-insensitive `find_if`, `m_strName` is dead — replace
all callsites with `m_strRealName`. ~50 callsites mechanical.

### `ibCompileContext::m_listVariable` map → vector

Symmetry with bc-side. `find_if + CompareString` everywhere; drop
`stringUtils::MakeUpper` calls.

### `ibByteFunction::m_lCodeParamCount`

Pure mirror of `m_listParam.size()`. Drop the field.

### `ibByteFunction::m_listParamRealName`

Parallel vector to `m_listParam`. Fold the name into `ibByteParam`
itself.

### `ibCompileEval` ctors duplication

The two ctors share their setup. Make the runtime-context ctor
delegate:

```cpp
explicit ibCompileEval(ibRunContext* p)
    : ibCompileEval(p ? p->GetByteCode() : nullptr,
                    p ? p->m_currentFunction : nullptr) {}
```

### Registry `clsid → ibValueMethodHelper*`

Biggest remaining win. ContextProp / ContextMethod entries in
`m_listVar / m_listFunc` are duplicated copies of what each binding
class's helper already knows. After registry:

* Drop kind=ContextProp / kind=ContextMethod from m_listVar / m_listFunc.
* Resolver, given a binding, asks `ibValueMethodHelper::ForClass(binding.clsid)->FindProp(name)`.
* Drop `m_strContext` / `m_parentRef` cross-table refs on bc.
* AOT cache row drops 10-30% of payload.

Blocked by: helper-template lifecycle (per-class vs per-instance
state) — requires splitting `PrepareNames()` into static and
instance-dependent halves, or constructing one throwaway instance
per class on first lookup.

See `docs/next-session-aot.md` for the persistence-layer plan, which
should land **before** the registry refactor (registry compresses
cache rows but isn't a blocker for AOT correctness).
