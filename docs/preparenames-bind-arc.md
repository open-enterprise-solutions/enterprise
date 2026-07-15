# PrepareNames → bind refactor (value name-surface)

> **Status (2026-06-05): LANDED.** `PrepareNames` (virtual + factory/compileCode
> calls) is gone; ~70 classes migrated to the two Members bases + composition. The
> helper class was renamed **`ibValueMethodHelper` → `ibMemberTable`** (members
> `m_props` / `m_methods` / `m_ctors`; value-member surface `m_members`). Full
> `Debug|x86` green and runtime-validated (designer resolves methods, "check
> syntax" no longer crashes; enterprise debugger inspects forms). The text below
> keeps the original migration journal (member names use the OLD `ibValueMethodHelper`
> spelling — read as `ibMemberTable`); the durable invariant is up top.

Canonical handoff for the arc that replaces the per-class virtual
`PrepareNames()` + manual `AppendProp`/`AppendFunc` with a bind-driven, NVI-based
name surface. Memory: `project_preparenames_to_bind`,
`feedback_no_diamond_inheritance`, `reference_preparenames_grandfather_model`,
`reference_ibvalue_first_base_pmf`.

---

## ⚠ INVARIANT: `ibValue` MUST be the first base (offset 0) of a binding class

Member fillers are bound with `ibMemberTable::Bind(this, &Cls::FillXMembers)`, which
**erases the pmf** via `static_cast<ibNameFiller>(fn)`, where
`ibNameFiller = void (ibValue::*)(ibMemberTable&) const`. `ibValue::*` is a
**single-inheritance** pmf (4 bytes, no this-adjustment slot — `ibValue` has no
bases). Casting a **multiple-inheritance** pmf (e.g. `ibValueForm::*`) down to
`ibValue::*` **silently drops the MI this-adjustment** — compiles clean, no warning.
It only misfires when `ibValue` is **not** at offset 0 of the class: the filler then
runs with `this` pointing at the wrong subobject → garbage walk (e.g. `GetControlList`
over junk children → `0xdddddddd`/`0xcdcd` AV).

- **Manifested:** `ibValueForm` had `ibBackendValueForm` as its first base → `ibValue`
  (reached via `ibValueFrame`) not at offset 0 → deterministic crash on designer
  "check syntax" / form inspection. **Fix: reorder bases** so the `ibValue`-holding
  base is first: `ibValueForm : public ibValueFrame, public ibBackendValueForm,
  public ibRuntimeModuleDataObject`. All other ~70 classes already had the
  `ibValue`-base first (adjustment 0 ⇒ harmless), which is why only the form showed it.
- **Guard:** `ibMemberTable::Bind` asserts
  `static_cast<const void*>(static_cast<const ibValue*>(obj)) == static_cast<const void*>(obj)`
  with the PROJECT INVARIANT message. New binding classes must keep the `ibValue`-base first.
- **Unrelated:** `ibBackendValue::GetImplValueRef()` is a *sibling* cross-cast
  (`ibBackendValue*`→`ibValue*`) on the hot value ctor/operator= path (avoids
  `dynamic_cast`). The reorder did **not** make it redundant — different concern.

---

## End state the user wants

- **`DoGetPMethods` lives ONLY in the two base classes** — no per-class override.
- **`PrepareNames` disappears entirely** (the virtual, the factory calls, the
  `compileCode.cpp` calls).

A class gets there by: swap its `ibValue` base slot for a Members base, drop its
own `m_methodHelper` + `DoGetPMethods`, turn its `PrepareNames` body into one or
more **const member fillers** bound in the ctor, and fix dispatch
`m_methodHelper->` → `m_methodHelper.`.

---

## Architecture (current, post-pivot)

### 1. NVI on `ibValue` (`compiler/value.h`) — unchanged

```cpp
public:    ibValueMethodHelper* GetPMethods() const { return EnsureMethods(DoGetPMethods()); } // the one entry, non-virtual
protected: virtual ibValueMethodHelper* DoGetPMethods() const { ... m_pRef->DoGetPMethods() ... } // overridden ONLY by the bases below
private:   static ibValueMethodHelper* EnsureMethods(h) { if (h) h->EnsureBuilt(); return h; }
```

### 2. `ibValueMethodHelper` — TWO contributor flavours, one binder list

```cpp
using ibNameBinder = void(*)(ibValueMethodHelper&, const ibValue* ctx);          // FREE fn  — type-invariant (Shared<>)
using ibNameFiller = void (ibValue::*)(ibValueMethodHelper&) const;              // MEMBER fn — per-instance dynamic
struct ibBoundNames { const ibValue* m_ctx; ibNameBinder m_freeFn; ibNameFiller m_memberFn; }; // exactly one fn set
void Bind(ibNameBinder fn, const ibValue* ctx = nullptr);                        // free
template<class C> void Bind(const C* obj, void (C::*fn)(ibValueMethodHelper&) const); // member — static_cast<ibNameFiller>(fn)
void Build();   // OUT-OF-LINE (value.cpp): freeFn ? freeFn(*this,ctx) : (ctx->*memberFn)(*this)
void EnsureBuilt(); void Invalidate(); // lazy + mutation
```

- **Why member-pmf (user's call):** for a per-instance value the helper's owner is
  always `this`, so a contributor should be a **member function** you bind with the
  object — not a free function that casts a `void*`/`ibValue*` ctx. This is the
  `ibPropertyValueFunctor` / `(handler->*m_funcHandler)()` idea (see
  `propertyManager/property/propertyList.h`), type-erased via a `pointer-to-member
  of ibValue` instead of a per-binder heap functor. The derived→base pmf cast is
  safe because the hierarchy is **diamond-free** (one `ibValue` subobject).
- **NOT a virtual.** A class binds one or **several** plain member fillers in its
  ctor; they run in bind order and accumulate. That is what lets the surface
  **compose** (base binds the common part, a leaf adds its own) without an
  override chain.
- The free flavour stays only for the **type-invariant `Shared<Binder>`** family
  (Array/Point/…): no instance exists, so a free fn is correct there.
- `ibValueMethodHelper` is ~154 bytes; shrinking it is a **separate later pass**
  (user deferred). It is now embedded by value in every dynamic value.

### 3. Two aggregate bases (`compiler/value.h`, after `ibValue`)

| Base | For | Helper | DoGetPMethods |
|---|---|---|---|
| `ibValueDynamicMembers` | per-instance dynamic (records, Map, ResultSet, …) | by-value `mutable m_methodHelper` | `→ &m_methodHelper` (protected) |
| `ibValueStaticMembers<Binder>` | type-invariant (Array, Point, File, …) | none (`Shared<Binder>`) | `→ Shared<Binder>()` (protected) |

`ibValue` stays **bare for primitives** (Number/String/Boolean/Date/Guid → nullptr).
`ibValueDynamicMembers` ctor does NOT bind anything — each derived class binds its
own fillers. Diamond-free slot swap (`: public ibValue` → base) keeps `ibValue`
once. See `feedback_no_diamond_inheritance`.

### 4. Composition for records (the pattern to copy)

`CallAsFunc` switches on the **method index** (`enIsNew = 0 …`), and methods vs
props live in **separate helper vectors**, so the common data filler and a leaf's
method filler can be bound in either order without shifting a method index.

- `ibValueRecordDataObject::FillDataMembers` — attributes (eProperty) + tabular
  sections (eTable) + module exports (eProcUnit). **Bound by the base ctor**,
  shared by every leaf. Attribute writability = `!GetMetaObject()->IsDataReference(id)`
  via a NEW **virtual `IsDataReference` (default false)** on
  `ibValueMetaObjectRecordData` (override already on `…RecordDataRef`).
- `ibValueRecordDataObject::FillBaseMethods` — GetFormObject(2)/GetTemplate/
  GetMetadata. Bound by **`ibValueRecordDataObjectExt` ctor** (DataProcessor /
  Report use only these).
- Each leaf with its own API binds **its own** `FillMethods` (Catalog, Document,
  ChartOfAccounts, ChartOfCharacteristicTypes). Document also binds
  `FillRegisterBinds` (FillHelperFromBinds + `m_registerRecords->PrepareNames()`).

Per-leaf ctor: `m_methodHelper.Bind(this, &Leaf::FillMethods);` — the base ctor
already bound `FillDataMembers`. No drop/replace dance (the abstract base's data
filler is wanted by every leaf).

### 5. Export autobind — "in the descriptor" (tail binder)

Module exports are surfaced by the **descriptor**, not by each value's filler:

- `moduleInfo.h`: `constexpr long g_aliasExport = 1000` (one alias for all exported
  names); `ibRuntimeModuleDataObject::ExportThunk(helper, ctx)` — a free name-binder
  that cross-casts ctx→descriptor and calls `ExportNamesToHelper(&helper, g_aliasExport)`.
- The descriptor ctor takes the **already-constructed** helper + owner and registers
  the thunk as the helper's **tail** contributor:
  `ibRuntimeModuleDataObject(ibValueMethodHelper& helper, const ibValue* owner) : ibRuntimeModuleDataObject() { helper.BindTail(&ExportThunk, owner); }`.
- A descriptor-value passes `(m_methodHelper, this)` to that base in its init list —
  `ibValueDynamicMembers` is the FIRST base, so `m_methodHelper` is already built when
  the LAST base `ibRuntimeModuleDataObject` runs. No per-value Bind boilerplate, no
  `ExportNamesToHelper` call in any filler.
- **Tail, not a normal binder** — `ibValueMethodHelper::m_tailBinder` runs after all
  `m_binders` in `Build()`. Required because `CallAsFunc` switches on the method
  INDEX: the leaf's fixed methods must occupy 0…N, exports come after. A normal
  early bind (base/descriptor ctor runs before the leaf's `FillMethods`) would put
  exports first and break `IsNew == 0`.
- Each migrated class sets `eProcUnit = g_aliasExport` in its helperAlias enum so its
  existing `alias == eProcUnit` dispatch routes the thunk's entries unchanged.
- Classes whose old "exports" came from `CopyMethod`/the common module (managers),
  NOT from `ExportNamesToHelper`, do NOT use this — their filler keeps its own logic
  and they don't pass the helper to a descriptor ctor (managers have no descriptor).

---

## Per-class migration recipe

1. `.h`: base slot `: public ibValue` → `: public ibValueDynamicMembers`
   (type-invariant value → `ibValueStaticMembers<&free_fn>` instead). Remove the
   class's own `ibValueMethodHelper* m_methodHelper;` and its `DoGetPMethods`
   override. Change `virtual void PrepareNames() const;` → `void FillXxx(ibValueMethodHelper&) const;`.
2. `.cpp` ctor: drop `m_methodHelper(new …)`; init the base
   `ibValueDynamicMembers(<the SAME ibValueTypes the old ctor used>)` —
   **watch the type**: a class that used the implicit `ibValue()` default is
   `TYPE_EMPTY`, not `TYPE_VALUE` (e.g. Constant). Add `m_methodHelper.Bind(this, &Class::FillXxx);`.
3. `.cpp` dtor: drop `wxDELETE(m_methodHelper)`.
4. `.cpp` filler: rename `PrepareNames`→`FillXxx(ibValueMethodHelper& helper)`,
   drop `ClearHelper()` (Build clears), `m_methodHelper->`→`helper.`,
   `ExportNamesToHelper(m_methodHelper,…)`→`ExportNamesToHelper(&helper,…)`. Keep
   Append* order identical (dispatch indices).
5. `.cpp` dispatch (SetPropVal/GetPropVal/CallAs*): `m_methodHelper->`→`m_methodHelper.`.
6. Remove any explicit `PrepareNames();` call in the class's `InitializeObject`
   (lazy Build replaces it). It currently resolves to the base no-op until step C.

`mutable ibValueMethodHelper m_methodHelper` is the base member; CreateIterator
that read `m_methodHelper != nullptr ? …GetNProps() : 0` becomes
`GetPMethods()->GetNProps()` (force-builds).

---

## Status

**Migrated to bases (build-pending):**
- Static family (`ibValueStaticMembers<&free_fn>`): Array, Point, Size, Colour,
  Font, File, DatabaseLayer, PreparedStatement, SpreadsheetDocument,
  TypeDescription. (Contributor free fns now take `(ibValueMethodHelper&, const ibValue*)`.)
- Map (`ibValueContainer`) → `ibValueDynamicMembers`, free `BindContainerNames`
  bound with `Bind(&BindContainerNames, this)` (free flavour — fine).
- **Records (composition):** `ibValueRecordDataObject` (FillDataMembers +
  FillBaseMethods), Catalog/Document/ChartOfAccounts/ChartOfCharacteristicTypes
  (own FillMethods), Document (+FillRegisterBinds), Ext ctor binds FillBaseMethods.
  Virtual `IsDataReference(default false)` added to base metaobject.
- **Constant** (`ibValueRecordDataObjectConstant`) → `ibValueDynamicMembers(TYPE_EMPTY)`;
  exports-only, surfaced by the descriptor tail; explicit PrepareNames() removed from InitializeObject.
- **Manager family (composition by ctor chain):** `ibValueManagerObject` →
  `ibValueDynamicMembers(TYPE_VALUE, true)`; `ibValueManagerDataObject::FillMembers`
  (surfaces module methods via the descriptor — see below), `…Predefined::FillPredefined`
  (predefined props, composes), standalone managers bind `FillManagerMethods` in their
  ctor (drop the old base `::PrepareNames()` call). Done: **catalog, document, chartOfAccounts,
  chartOfCharacteristicTypes, accounting/accumulation/informationRegister, enumeration**.
  - **CopyMethod wart — RESOLVED 2026-06-17.** `ibValueManagerDataObject::FillMembers`
    no longer copies the wrapper's helper table; it surfaces the module's exported
    methods straight from its runtime descriptor via the public
    `ibRuntimeModuleDataObject::ExportMethodsToHelper(helper, g_aliasExport)` (the
    method-only half of `ExportNamesToHelper`, which now composes the two public halves
    `ExportMethodsToHelper` + `ExportPropsToHelper`). The bytecode-function index it
    keys on is exactly what `CallAsProc/Func` feed back into `pRefData`, so dispatch
    lines up with no duplicated table. `pRefData` is typed (`ibValueModuleUnit*`) — no
    `dynamic_cast`. The two **external** DP/Report managers in `moduleManagerExt.cpp`
    keep `CopyMethod` (mirror an external *object value*'s surface, not a module
    descriptor — separate shape).
    - **NB — a manager-module export handler not resolving (`Catalogs.X.BeforeWrite`)
      is a SEPARATE bug, not this refactor.** Reverting to the old `CopyMethod` did NOT
      fix it: when `FillMembers` runs, the wrapper's ProcUnit/bytecode may not be
      compiled yet (modules register with `compileNow=false`), so BOTH the bytecode
      surface and the old helper copy come up empty. Root cause is manager-module
      compile timing vs. first manager access, tracked separately.
  - **⚠️ Build-watch snapshot below — SUPERSEDED, kept as design history.** Everything
    from "Still TODO" through the "FORCED next targets" / "STILL old-path" notes was
    written mid-build and is now LANDED. Verified against current code 2026-06-17:
    `ibValueFrame` (frame.h:69), `ibValueForm` (form.h:32), `ibValueModuleManager`
    (moduleManager.h:24), `ibValueModel` (tableInfo.h:88), `ibValueModelColumnCollection`
    (tableInfo.h:317) and `ibValueModelReturnLine` (tableInfo.h:390) are all
    `ibValueDynamicMembers`; `ibValueRecordSetObject` rides the migrated `ibValueModel`
    chain; the static-helper managers (constant / external DP / report) dropped their
    `static` helper for the by-value base. The ONLY live `PrepareNames()` left in the
    tree is the separate `CMethodHelper` CValue* dialog hierarchy (valueGrid / Font /
    File / Colour dialogs) — a different base class, out of scope for this arc.
  - **Still TODO (static-helper managers — different shape):** `constantManager`,
    `…ExternalDataProcessor`, `…ExternalReport` use a `static ibValueMethodHelper` +
    own `DoGetPMethods` (type-invariant, no base call). Convert to either
    `ibValueStaticMembers<&free_fn>` OR the dynamic pattern (adds CopyMethod). And the
    NON-external `…DataProcessor`/`…Report` managers (dynamic, base call) — migrate
    like the 9 above. (All four are currently broken by the base change.)

**value.h core:** binder list holds free OR member pmf; `m_tailBinder` runs last in
`Build()` (out-of-line in value.cpp); `HasBinders()` gates Build/EnsureBuilt;
`Shared<>`/`SharedByOwner<>` unchanged (free flavour).

**Export autobind landed (records + constant):** `g_aliasExport` + `ExportThunk` +
`BindTail` (see Architecture §5). `ibRuntimeModuleDataObject` now has a **single
ctor** `(ibValueMethodHelper& helper, const ibValue* owner, ibCompileModule* = nullptr)`
— the default + compile-module-only ctors are GONE (they would skip export wiring).

> **FORCED next targets (won't compile until done):** the single descriptor ctor
> breaks every other `ibRuntimeModuleDataObject` subclass that still default- or
> compile-module-constructs it. Migrate each to pass `(m_methodHelper, this[, compileModule])`:
> - `ibValueRecordSetObject` (commonObject.h:2090 — base `ibValueModelRamTableBase`; give it the helper + nested ColumnCollection/ReturnLine/KeyValue migrations).
> - `ibValueModuleManager*` (moduleManager.h:25/:34 — uses the eager `new ibCompileModule(obj)`, so pass it as the 3rd arg; moduleManager.cpp:22).
> - `ibValueForm` (frontend form.h:33).
> These must become `ibValueDynamicMembers` (so they HAVE `m_methodHelper` to pass).
>
> **Chain-root decision (user): migrate the value-chain root to `ibValueDynamicMembers`.**
> - **DONE: `ibValueModel` (tableInfo.h:84) → `ibValueDynamicMembers`** — additive
>   (ibValueModel had NO own helper; the nested ColumnInfo/ReturnLine do, separately).
>   This gives the whole table chain (`…ModelTableBase`→`…RamTableBase`→recordset,
>   `ibValueModelTable`, `…TreeBase`) a by-value helper from the base; subclasses with
>   their own pointer-helper + DoGetPMethods shadow it (dormant) until migrated.
>   `ibValueModel::ibValueModel()` ctor switched to `ibValueDynamicMembers(TYPE_VALUE)`.
> - **`ibValueFrame` (frame.h:69) — NOT additive** (it has its own `m_methodHelper(new…)`),
>   so it's a full frame migration (drop own helper/DoGetPMethods, PrepareNames→fillers,
>   dispatch ->→.). Base-swap reverted for now; do it as the frame migration proper.
> - `ibValueRecordSetObject` is now UNBLOCKED (base helper available) but still needs its
>   full migration: drop own `m_methodHelper`(commonObject.h:2454)+`DoGetPMethods`(2272),
>   init `ibRuntimeModuleDataObject(m_methodHelper, this)`, `eProcUnit = g_aliasExport`,
>   PrepareNames→fillers, dispatch ->→.

**commonObject register/record cluster (this pass):** DONE — `ibValueRecordKeyObject`,
`ibRecorderRegister`, `ibValueRecordManagerObject` (base, abstract), outer
`ibValueRecordSetObject` (descriptor single-ctor via the ibValueModel→Dynamic helper),
recordset-nested `…RegisterKeyValue` + `…RegisterKeyDescriptionValue`. Document's
`FillRegisterBinds` no longer calls `m_registerRecords->PrepareNames()` (lazy now).
STILL old-path (independent own-helpers, not broken): recordset-nested
`…RegisterReturnLine` (base `ibValueModelReturnLine`) + `…RegisterColumnCollection`
(base `ibValueModelTableBase::ibValueModelColumnCollection`) — need those two
tableInfo chain-roots swapped to `ibValueDynamicMembers` first. recordManager LEAVES
(register record-managers in informationRegister/accounting/accumulation) still TODO.

**Manager family: DONE** — base `ibValueManagerObject`→`ibValueDynamicMembers`,
`ibValueManagerDataObject::FillMembers` (CopyMethod wart), `…Predefined::FillPredefined`,
and standalone `FillManagerMethods` for: catalog, document, chartOfAccounts,
chartOfCharacteristicTypes, accounting/accumulation/information-register, enumeration,
DataProcessor, Report, ExternalDataProcessor, ExternalReport, constantManager. (The
External/constant managers dropped their `static m_methodHelper` for the base by-value one.)

---

## DONE so far (session 2, build-pending)

Whole-file clean (no DoGetPMethods / PrepareNames / pointer-helper left):
- **value.h / value.cpp / moduleInfo.h** — binder machinery (member-pmf + tail +
  single descriptor ctor + ExportThunk/g_aliasExport + IsDataReference virtual).
- **commonObject.h/.cpp** — records (Catalog/Document/Charts/DP/Report), Constant,
  managers (base/Predefined), recordKey, recorderRegister, recordManager, recordset
  outer + nested (ColumnCollection/ReturnLine/KeyValue/KeyDescriptionValue).
- **tableInfo.h/.cpp** — `ibValueModel`→Dynamic (chain root, additive),
  `ibValueModelColumnCollection`/`ibValueModelReturnLine`→Dynamic (additive roots),
  `ibValueModelColumnInfo` migrated.
- **All managers** (catalog/document/charts/constant/dataProcessor/dataReport/
  enumeration + External + registers' managers).
- **All registers** (info/accounting/accumulation RecordSet + info RecordManager).
- **reference.{h,cpp}**, **tabularSection.{h,cpp}** (Base + ReturnLine + ColumnCollection).
- **enumUnit**, **enumFactory**, **codeRunner/outputMessage**, **globalContextManager**.

## CLOSED (verified 2026-06-06)

`ibValue::PrepareNames` (the virtual) is **gone**; all LIVE classes migrated to the
two Members bases + `FillMembers`/`Shared<Binder>` (including list/objectList.h and
selector/objectSelector.h — the list below is historical). `valueFactory.cpp` has no
PrepareNames calls. The only survivors:
- **Dead CValue dialogs** (valueGrid / valueColourDialog / valueFontDialog /
  valueFileDialog) — still carry `static CMethodHelper m_methodHelper` + their own
  `PrepareNames()`, but they are NOT in any vcxproj (same dead-legacy hierarchy as
  CDocMDIFrame; user: "long gone"). Left untouched.
- **Factory eliminated (2026-06-06).** `ibValue::CreateAndPrepareValueRef<T>` existed
  only to call `PrepareNames()` at creation; with PrepareNames gone its body was just
  `::new T(...)`. The whole factory is now removed (193 call sites across 59 files):
  - Most sites → plain `new T(...)` (public ctors).
  - `ibValueReferenceDataObject` → its own static `Create()` factory (does `new` +
    `PrepareRef`), the intended construction path.
  - `ibValueRecordDataObjectConstant` / `ibValueRecordDataObjectDocument` had
    protected ctors (factory-only via `friend class ibValue`) — these were just an
    inconsistency vs Catalog's `CreateObjectRefValue` (already `new`), so their ctors
    were made public.
  - The two generic `CreateAndConvertObjectValueRef<T>` wrappers (value.h / metaData.h)
    → `new T(...)`. ibValue's is friend-safe for any T; ibMetaData's generic path has
    no explicit-`<T>` callers (carries only public value types: Font/Colour/Size/…).
  No more `CreateAndPrepareValueRef` / `NewValueRef` anywhere.

The functional arc is **closed**. Original migration inventory kept below for history:

Still to migrate (each derives an already-Dynamic root → own helper is dead, just
drop DoGetPMethods + own member, PrepareNames→FillMembers bound in ctor, dispatch ->→.):
- **list/objectList.h (~10 classes):** ibValueListDataObject (+nested
  ColumnCollection/ColumnInfo/ReturnLine), …EnumRef, …Ref, ibValueListRegisterObject,
  ibValueModelTreeDataObject (+nested), …TreeDataObjectFolderRef. (All derive
  ibValueModelTableBase/TreeBase → Dynamic; all own helpers are dead.)
- **selector/objectSelector.h (×2).**

Inventory (grep `virtual void PrepareNames() const` minus value.h, and dead-legacy
`CValue` dialogs which are a SEPARATE hierarchy — do NOT touch:
valueColourDialog/valueFileDialog/valueFontDialog/valueGrid):

- **commonObject.h/cpp:** ibValueManagerDataObject(+Predefined), ibValueRecordKeyObject,
  ibValueRecordSetObject + nested (RegisterColumnCollection, ReturnLine, KeyValue,
  KeyDescriptionValue), ibValueRecordManagerObject, ibRecorderRegister.
- **managers:** catalog/document/dataProcessor/dataReport/constant/enumeration/
  accountingRegister/accumulationRegister/informationRegister/chartOfAccounts/
  chartOfCharacteristicTypesManager.
- **registers:** accountingRegister, accumulationRegister, informationRegister.
- **list/objectList.h (×6), selector/objectSelector.h (×2), tabularSection.h (×2),
  reference/reference.h.**
- **moduleManager:** moduleManager.h (×4), moduleManagerExt.h (×2), globalContextManager.h.
- **system:** systemManager (single-instance — keep on free Shared or convert
  carefully), valueDatabase (×3), valueOLE, valueSpreadsheet (×3 nested),
  valueTable (×3 nested), valueType (leftover decl?), tableInfo.
- **enum:** enumFactory (DYNAMIC — iterates global ctor registry; member filler +
  Invalidate, NOT Shared), enumUnit.
- **metaObject.h:280** — the metadata base (designer property surface). Deepest; do
  with care.
- **codeRunner/outputMessage.h.**
- **frontend:** form.h (×2), frame.h (×2), gridBox, htmlBox, notebook, tableBox,
  textBox, widgets (×2). value.h changes already cross into frontend.

### Final step C — remove PrepareNames

After every owner is migrated: delete `ibValue::PrepareNames` (virtual + value.cpp
body), the factory calls (`CreateAndPrepareValueRef` in value.h:746,
`CreateObjectRef` in valueFactory.cpp:36), and the explicit calls in
compileCode.cpp:187/205 + metadataConfiguration.cpp + enumUnit.h:233 etc. Then the
single `Debug|x86` build.

---

## Build watch (first `Debug|x86`)

- member-pmf: `static_cast<ibNameFiller>(&Class::Fill)` (derived→base pmf, C++17 ok,
  needs `ibValue` complete at instantiation — it is, in the .cpp ctors).
- `Build()` out-of-line dereferences the pmf on `ibValue` — must stay in value.cpp.
- by-value `m_methodHelper` (~154 B) embedded in every record/Map → **kill running
  OES processes + clean rebuild** (record object layout changed).
- Possible C4251 (std::vector / unordered_map / pmf in a dllexport class) —
  pre-existing pattern.
