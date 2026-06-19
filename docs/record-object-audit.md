# Record-object audit — pre-refactor snapshot 2026-05-25

> **⚠ Pre-refactor snapshot.** This document captures the state of
> Write/Delete duplication that existed **immediately before** the
> consolidation landed. The refactor itself shipped on the same day
> in commit `fc4efa55` — see
> [`record-object-refactor.md`](record-object-refactor.md) for the
> landed status. The "15 scaffolds" inventory in Part 1 below is
> **no longer accurate** — there are now 2 base scaffolds
> (`HierarchyRef::WriteObject/DeleteObject` and
> `RecordSetObject::WriteRecordSet/DeleteRecordSet`) plus Document's
> own state machine on the new `RecorderRef` intermediate.
>
> This file is preserved as the empirical baseline that motivated the
> refactor — useful when reviewing the diff against `fc4efa55`'s
> parent. Do NOT use it as a guide to current code.
>
> **Convergence target** (user observation 2026-05-25, **realised in
> `fc4efa55`**): the consolidation belongs on
> **`ibValueRecordDataObject`** (runtime side) +
> **`ibValueMetaObjectRecordData`** (meta side). These are the two
> bases through which the 4 ref-objects already pass; the registers
> live in a parallel `ibValueRecordSetObject` +
> `ibValueMetaObjectRegisterData` pair that follows the same shape.
> Document landed on a new intermediate
> `ibValueRecordDataObjectRecorderRef` rather than absorbing its
> state machine into the universal scaffold.

---

## Part 1 — Runtime: 15 Write/Delete scaffolds

### Inventory (post-record-locks)

| Family | Class | File | Write method | Delete method | Lines (Write) | Lines (Delete) |
|---|---|---|---|---|---|---|
| Ref | `ibValueRecordDataObjectCatalog`    | catalogObject.cpp                  | `WriteObject()`            | `DeleteObject()`     | ~98  | ~80  |
| Ref | `ibValueRecordDataObjectDocument`   | documentObject.cpp                 | `WriteObject(wm, pm)`      | `DeleteObject()`     | ~180 | ~80  |
| Ref | `ibValueRecordDataObjectChartOfAccounts` | chartOfAccountsObject.cpp     | `WriteObject()`            | `DeleteObject()`     | ~40  | ~32  |
| Ref | `ibValueRecordDataObjectChartOfCharacteristicTypes` | chartOfCharacteristicTypesObject.cpp | `WriteObject()` | `DeleteObject()` | ~82 | ~85 |
| Register | `ibValueRecordSetObjectAccumulationRegister` | accumulationRegisterObject.cpp | `WriteRecordSet(replace, clearTable)` | `DeleteRecordSet()` | ~64 | ~58 |
| Register | `ibValueRecordSetObjectAccountingRegister`   | accountingRegisterObject.cpp   | same | same | ~60 | ~58 |
| Register | `ibValueRecordSetObjectInformationRegister` | informationRegisterObject.cpp | same | same | ~62 | ~58 |
| Constant | `ibValueRecordDataObjectConstant`   | constantObject.cpp                 | `SetConstValue(cValue)`    | — (no Delete)        | ~115 | — |

**Totals:** 8 ref-object methods + 6 register methods + 1 constant = 15 scaffolds.

### Canonical scaffold — observed shape (mutable-refs, post-record-locks)

```cpp
bool ibValueRecordDataObjectXxx::WriteObject()
{
    if (!appData->DesignerMode())
    {
        ibConnectionScope scope = ibSession::Current()->OpenConnectionScope();

        if (!scope || !scope->IsOpen())
            ibBackendCoreException::Error(_("Database is not open!"));

        if (!ibBackendException::IsEvalMode())
        {
            if (!m_metaObject->AccessRight_Write()) {
                ibBackendAccessException::Error();
                return false;
            }

            {
                ibBackendValueForm* const valueForm = GetForm();
                {
                    scope.SafeBeginTransaction();

                    TryAcquireFormLock();                         // <-- Phase B.3 add
                    LockAndCheckDataVersion(/*bump=*/true);       // <-- Phase A add

                    /* BeforeWrite + cancel */
                    /* === per-type body (codegen, mode args, posting) === */
                    /* SaveData + rollback */
                    /* OnWrite + cancel + rollback-on-cancel */

                    scope.SafeCommitTransaction();

                    if (newObject && valueForm != nullptr) valueForm->NotifyCreate(GetReference());
                    else if (valueForm != nullptr) valueForm->NotifyChange(GetReference());
                }
                m_objModified = false;
            }
        }
    }

    return true;
}
```

The shape stayed identical to the original `record-object-refactor.md`
prediction. Record-locks added two lines per site (`TryAcquireFormLock`
+ `LockAndCheckDataVersion`) — exactly the cost the doc warned about.

### Per-family variation

| Family | Variation point | What's specific |
|---|---|---|
| Catalog | code-gen middle | `SetNewCode` script → `GenerateUniqueIdentifier` → `ResetUniqueIdentifier` rollback closure |
| Document | **state machine + register cascade** | See "Document is an outlier" below — not "Catalog with two arguments". |
| ChartOfAccounts | same as Catalog | Identical to Catalog logically; condensed formatting (one-line statements) |
| ChartOfCharacteristicTypes | same as Catalog | Identical to Catalog logically; verbose formatting |
| Registers (3) | none meaningfully | `LockByKeys()` instead of `LockAndCheckDataVersion`; `SaveData(replace, clearTable)` action. The 3 register subclasses are diff-clean — same bytes modulo class-name + one comment line about Document-recorder serialization. |
| Constant | snapshot + UPSERT | inline `SELECT 1 FOR UPDATE` (no DataVersion column), m_constValue snapshot+restore on rollback, per-driver UPSERT SQL (FB `UPDATE OR INSERT MATCHING` vs PG/SQLite `ON CONFLICT`), `FillCheck` required-field guard |

### Document = Catalog minus hierarchy plus movements

User's reframe (2026-05-25): "по сути, документ похож максимально
на справочник, у него иерархии нет, зато есть запись по движениям".

This is more accurate than "outlier" — the difference is a **parallel
variation along an orthogonal axis**, not a fundamentally different
shape. Existing class tree already encodes one such axis (the
`Hierarchy` layer that wraps simple-ref to add folder/item mode);
Document represents the *other* axis (movements/posting).

| Axis | Catalog | ChartOfAccounts | ChartOfCharacteristicTypes | Document |
|---|---|---|---|---|
| Hierarchy (folder/item mode) | ✓ | ✓ | ✓ | ✗ |
| Movements (register-recorder) | ✗ | ✗ | ✗ | ✓ |
| Code/Number identifier | Code | Code | Code | Number |
| Posted state | n/a | n/a | n/a | ✓ |

Implication for refactor: the scaffold should host **both** axes as
optional hooks. Catalog leaves the movements-hook no-op; Document
leaves the hierarchy-related ones no-op (it doesn't even derive from
the Hierarchy intermediate today). This makes Option A (single
template-method with optional hooks) the right shape — not "interface
with too many holes" but "single scaffold with axis-feature hooks".

### Document's concrete extras vs Catalog

`Document.WriteObject(writeMode, postingMode)` is roughly **2× the
surface area** of Catalog's scaffold. The extra weight breaks down
into the "movements axis":

| Aspect | Catalog | Document |
|---|---|---|
| Args | none | `writeMode : ibDocumentWriteMode`, `postingMode : ibDocumentPostingMode` |
| Pre-TX checks | DesignerMode / eval / access | + DeletionMark guard when `writeMode == Posting` |
| Attribute mutations inside TX | none mandatory | `DocumentPosted` set to true/false based on `writeMode` |
| New-object pre-save | code-gen | code-gen + DocDate default-fill |
| Script hooks | `BeforeWrite`, `OnWrite` | `BeforeWrite(wm, pm)`, `Posting(pm)`, `UndoPosting()`, `OnWrite` |
| Register cascade inside TX | none | `if newObject: CreateRecordSet`; `if Posting: WriteRecordSet`; `if UndoPosting: DeleteRecordSet` |
| Post-commit | notify | notify + `RefreshRecordSet` |
| `DeleteObject` order | `BeforeDelete → DeleteData → OnDelete` | `BeforeDelete → DeleteRecordSet → OnDelete → DeleteData` |

Document's Write is a **state machine** with branches that conditionally
fire script hooks AND cascade register-side I/O — not "Catalog with
extras." The Delete path is similarly distinct (registers must be
cleared *before* the recorder row disappears — likely intentional,
not a bug).

This has direct refactor implications — see "Convergence target"
section below for the three options.

### Drift / bug findings

1. **ChartOfAccounts formatting drift** — same logic as Catalog but
   compressed into one-line `if`/`else` statements (no braces, no
   newlines). Pure copy-paste-then-minify. Maintenance cost
   per-cross-cutting change is the same; readability is worse.

2. **Document.DeleteObject order mismatch** (documentObject.cpp:449-470)
   — runs `DeleteRecordSet` → `OnDelete` script → `DeleteData`, while
   Catalog/ChartOf* run `BeforeDelete` → `DeleteData` → `OnDelete`.
   Document is the odd one. May be intentional (registers must clear
   before the recorder row disappears) but breaks the "uniform shape"
   assumption.

3. **Copy-paste bug in Document.DeleteObject** (documentObject.cpp:451)
   — DeleteRecordSet failure branch uses
   `ibBackendCoreException::Error(_("Failed to write object in db!"))`
   inside a Delete path. Copy-paste from Write. Cosmetic, but a
   confused operator gets the wrong word.

4. **Lowercase "failed" in constantObject.cpp:374** — `_("failed to
   write object in db!")` in BeforeWrite-cancel branch; everything
   else uses "Failed". Translation table will dedupe but it's a
   localization gotcha.

5. **Register comment drift** —
   `accumulationRegisterObject.cpp` Write has a multi-line comment
   about Document.Write re-entrance and recorder serialization;
   `accountingRegisterObject.cpp` Write has only the one-line "Lock by
   key set (recorder for AccountingRegister)" comment. Same code,
   different docs.

6. **No `TryAcquireFormLock` on Constant.SetConstValue** — Phase B.3
   wired the soft-lock into form-open via `ibValueRecordDataObject
   Constant::TryAcquireFormLock` (Polish). But the SetConstValue
   *path itself* doesn't call it — a script doing `Const.MyConst =
   value` directly skips the long-held lock check. The DB-side
   `SELECT 1 FOR UPDATE` still protects against concurrent SetConstValue,
   so this is "no proactive notify for script path" not "lost
   update", but worth flagging.

### What changes if a new cross-cutting concern arrives

| Concern | Files touched (today) | Files touched (after refactor) |
|---|---|---|
| Audit log on Write | 15 sites | 1 (scaffold) |
| Retry-on-deadlock | 15 sites | 1 |
| Pre-write validation hook | 15 sites | 1 |
| Telemetry tracing span | 15 sites | 1 |
| Soft-delete instead of hard-delete | 15 sites | 1 |

Every cross-cutting change pays 15×.

---

## Part 2 — Metaobject hierarchy

### Tree

```
ibValueMetaObject (root, metaObject.h)
├── ibValueMetaObjectCompositeData
│   └── ibValueMetaObjectGenericData                       (commonObject.h:157)
│       ├── ibValueMetaObjectRecordData                    (commonObject.h:267)
│       │   ├── ibValueMetaObjectRecordDataExt             (commonObject.h:410)
│       │   │   ├── ibValueMetaObjectDataProcessor         (dataProcessor.h:6)
│       │   │   │   └── ibValueMetaObjectExternalDataProcessor (dataProcessor.h:123)
│       │   │   └── ibValueMetaObjectReport                (dataReport.h:6)
│       │   │       └── ibValueMetaObjectExternalReport    (dataReport.h:126)
│       │   └── ibValueMetaObjectRecordDataRef             (commonObject.h:453)
│       │       ├── ibValueMetaObjectRecordDataEnumRef     (commonObject.h:557)
│       │       │   └── ibValueMetaObjectEnumeration       (enumeration.h:6)
│       │       └── ibValueMetaObjectRecordDataMutableRef  (commonObject.h:631)
│       │           ├── ibValueMetaObjectDocument          (document.h:12)
│       │           └── ibValueMetaObjectRecordDataHierarchyMutableRef (commonObject.h:744)
│       │               ├── ibValueMetaObjectCatalog       (catalog.h:11)
│       │               ├── ibValueMetaObjectChartOfAccounts (chartOfAccounts.h:14)
│       │               └── ibValueMetaObjectChartOfCharacteristicTypes (chartOfCharacteristicTypes.h:11)
│       ├── ibValueMetaObjectRegisterData                  (commonObject.h:975)
│       │   ├── ibValueMetaObjectAccumulationRegister      (accumulationRegister.h:7)
│       │   ├── ibValueMetaObjectAccountingRegister        (accountingRegister.h:8)
│       │   └── ibValueMetaObjectInformationRegister       (informationRegister.h:7)
│       └── ibValueMetaObjectConstant                      (constant.h:8)
```

7-level deep tree, 13 leaf classes, 3 intermediates carrying state
(Generic / Record / RecordDataRef / RecordDataMutableRef + RegisterData).

### Shared but duplicated machinery

1. **Access-right roles** —
   `ibValueMetaObjectRecordDataMutableRef` declares `m_roleRead /
   m_roleWrite / m_roleDelete` + `AccessRight_Read / Write / Delete`
   helpers. `ibValueMetaObjectRecordDataExt` declares its own
   `m_roleUse` + `AccessRight_Use`.
   `ibValueMetaObjectRegisterData` declares **another copy** of
   `m_roleRead / m_roleWrite / m_roleDelete` + helpers, identical
   to MutableRef. Could be a `ibValueMetaObjectRWDRole` mixin or
   hoisted to a common ancestor (probably `RecordData` since
   `RegisterData` lives in a parallel branch).

2. **`Wrire` typo in role name** — the stored metadata name was
   `"Wrire"` (display `_("Write")` is correct). **Still present**
   (verified 2026-06-19, now `commonObject.h:745` via the
   `IB_DECLARE_RWD_ROLE_TRIPLET("Wrire")` macro) — kept until a
   migration arc; fixing it migrates stored role definitions.

3. **`Dimention` typo throughout `RegisterData`** — **RESOLVED**
   since this snapshot: the methods are now correctly spelled
   `GetGenericDimensionArrayObject` (`commonObject.h:1129`) etc. No
   `Dimention` spelling remains in `commonObject.h`.

4. **`FillArrayObjectByPredefinedAttribute` requires subclass override**
   in catalog/document/chartOf* (we just added DataVersion there
   in the record-locks arc). The base declaration lists
   `{Reference, DeletionMark}`; subclasses must repeat the base list
   plus their own additions. This is the **trap class** documented
   in [memory: reference_predefined_attr_subclass_lists](../memory/reference_predefined_attr_subclass_lists.md):
   "base declares a column, subclass list must also include it or
   runtime won't touch the slot". Cause of the original DataVersion
   subclass-list bug fixed in Phase A.

5. **Form-creation scaffold (`CreateAndBuildForm`)** lives on
   `ibValueMetaObjectGenericData` but each leaf (Catalog/Document/
   etc.) overrides `CreateObjectForm` with its own factory call
   pattern. Common shape, slightly different sources.

6. **Tabular-section / table-aware methods** (`GetGenericTableArrayObject`,
   `FindTableObjectByFilter`, `FillArrayObjectByPredefinedTable`) live
   on `ibValueMetaObjectRecordData` — Constants and Registers don't
   use them. Could be pushed down (to RecordData where it logically
   belongs) — already there. ✓

7. **`OnCreateMetaObject / OnLoadMetaObject / OnSaveMetaObject /
   OnDeleteMetaObject` lifecycle hooks** —
   MutableRef overrides Generic's overrides; ext branches similar.
   Each subclass adds its own tweaks. Probably correct as-is but
   cross-family consistency unclear.

8. **Per-leaf code-gen seeds** — `Catalog.GenerateNextIdentifier`,
   `Document.GenerateNextIdentifier`, `ChartOfAccounts.Generate
   NextIdentifier` all live in `ibValueRecordDataObjectRef` base
   (commonObject.h:1561). Good — already consolidated, but the
   call sites in subclasses each handle the result differently
   (Catalog assigns to one attribute, Document to another). Could
   be parametrized.

### What's truly type-specific (after dedup)

| Class | Genuine specificity |
|---|---|
| Catalog | Hierarchy + owner chain; Code identifier |
| Document | Date + Number + Posting/UndoPosting state machine |
| ChartOfAccounts | Hierarchy + accounting-specific Subconto table |
| ChartOfCharacteristicTypes | Hierarchy + characteristic-type domain |
| Enumeration | Enum values; no DB instance objects |
| DataProcessor / Report | No DB persistence; ext file loading |
| AccumulationRegister | Resources, dimensions; recorder ref |
| AccountingRegister | Account/Subconto/Sum/Direction (Dr/Cr) |
| InformationRegister | Periodicity (None/Subordinate/Year/Quarter/etc), dimensions |
| Constant | Single-row table per metaobject |

The genuine variation is **rich** — each leaf has real domain logic.
What's duplicated is **scaffolding** (lifecycle, roles, form-creation,
write/delete) not domain.

---

## Part 3 — Cross-cutting observations + priorities

### Pattern observation: "scaffold growth ratchet"

Every cross-cutting concern in OES history has added 15 sites of
boilerplate without then extracting:

| Era | Concern | Added at | Extracted? |
|---|---|---|---|
| early | raw `m_procUnit->CallAsProc` | every site | yes → `ExecAsProc` helper |
| middle | `SafeBeginTransaction` / `SafeCommitTransaction` | every site | partial (counter layer in scope) |
| middle | `ibSession::Current()->OpenConnectionScope` | every site | no |
| middle | `IsEvalMode` skip | every site | no |
| middle | `DesignerMode` skip | every site | no |
| late | `AccessRight_Write` check + Error | every site | no |
| 2026-05 | `LockAndCheckDataVersion()` / `LockByKeys()` | every site | no |
| 2026-05 | `TryAcquireFormLock` | every site | no |

Pattern: **add the line everywhere, never extract**. The cost has
accumulated. Refactor is now-or-never before the next two cross-
cutting concerns lock the cost in further.

### Convergence target — `ibValueRecordDataObject` + `ibValueMetaObjectRecordData`

The user's intuition is correct: these two are the convergence
points. Concretely:

- **`ibValueRecordDataObject`** (runtime base for all ref-objects)
  should own the Write/Delete scaffold via template-method.
  Catalog/ChartOf*/ChartOfChar* override only the **per-type body**
  (codegen) and the **rollback closure** (`ResetUniqueIdentifier`).

- **`ibValueMetaObjectRecordData`** (meta base) should own the
  shared role machinery (Read/Write/Delete), the predefined-attribute
  contract, and the form-creation scaffold. RegisterData should
  share the role machinery via mixin or a sibling base.

The 3 registers can collapse to **zero overrides** of `WriteRecordSet
/ DeleteRecordSet` (their bodies are byte-identical mod comment text).

Constants stay separate — single use, single file, doesn't pay to
extract.

### How to handle Document — three options

Document is the outlier (see "Document is an outlier" in Part 1).
Three ways to fit it into the template-method:

**Option A — single scaffold, many optional hooks.** Add
`OnPostingHook` / `OnUndoPostingHook` / `OnAfterSaveBeforeRegisterHook`
/ `OnRefreshAfterCommitHook` to `ibValueRecordDataObject`. Catalog
leaves them no-op. Result: an "interface with too many holes" —
most overridable points unused by Catalog/ChartOf*, signals
muddled. **Not recommended.**

**Option B — two-layer template-method.** `ibValueRecordData
Object` owns the universal 70% (designer/eval/access/scope/lock/
BeforeWrite/SaveData/OnWrite/commit/notify). Introduce a thin
intermediate (`ibValueRecordDataObjectPostable` or just keep the
extra layer inside `ibValueRecordDataObjectDocument` itself) that
wraps the base scaffold and slots in the posting state machine
between SaveData and OnWrite. Catalog ignores this layer. The
public entry `Write(wm, pm)` lives on Document, sets member-state
side-channel (m_writeMode/m_postingMode), then calls into the
base scaffold; hooks read the members atomically. **Recommended.**

**Option C — leave Document inline, template-method only the
simple ones.** Catalog + ChartOfAccounts + ChartOfCharacteristic
Types collapse to a shared scaffold (4 sites → 1 base + 3 thin
overrides). Document keeps its current ~180-line inline body.
Register family collapses separately. Lowest risk, but Document
keeps paying the cross-cutting tax (next concern still adds the
line in Document's inline scaffold). Reasonable v0; can promote
to Option B later.

**Decision recommendation:** Phase A goes with Option C — extract
shared helpers from the simple ref-family (Catalog/ChartOf*/Chart
OfChar*) and the register-family, leave Document inline. Re-
evaluate Document after Phase A lands. If the helpers prove
generalize-able to Document's outer shell (TX/lock/access/scope
boilerplate is the same), promote to Option B in Phase B.

### Recommended priority order

1. **Phase A — Option C scope** — `Begin*Scope` / `Commit*Scope`
   helpers on the two bases (`ibValueRecordDataObject` for
   Catalog/ChartOf*/ChartOfChar*; `ibValueRecordSetObject` for
   registers). Each subclass calls them; per-type middle stays
   inline. Document stays inline entirely. ~70% dedup on the
   simple ref family + ~100% on registers, behavior-preserving.
   ~1 week.

2. **Bug fixes during Phase A** — bundle the cosmetic findings
   (Document Delete error message, lowercase "failed", comment
   drift) into the same arc. Zero risk; clean up the audit's red
   flags. Document Delete order stays as-is until verified
   intentional via gtest.

3. **Phase B — full template-method on simple refs + registers**.
   Registers go first (3×2=6 collapses, byte-identical proof).
   Simple refs second (3×2=6 collapses with real codegen
   variation — Catalog and ChartOf* share the SetNewCode pattern
   exactly). Reduces to ~10-line hook bodies per simple-ref leaf.
   ~1-2 weeks.

4. **Phase C — Document promotion (optional)** — only if Phase A+B
   helpers prove generalize-able to Document's outer shell. Adds
   the posting state-machine layer between SaveData and OnWrite
   in a new intermediate or inside Document itself. Public
   `Write(wm, pm)` keeps its existing signature via
   member-state side-channel. ~2 weeks. Skip if Phase B's
   helpers are sufficient and Document's inline scaffold is
   tolerable for one more cross-cutting concern cycle.

5. **Meta-side cleanup** (parallel or after) — hoist role machinery
   to a common base, fix `Wrire` typo (with migration), audit the
   `FillArrayObjectByPredefinedAttribute` contract for a less
   error-prone shape (the subclass-list trap caused the
   DataVersion bug; will cause the next one too).

6. **Defer indefinitely** — `Dimention` typo rename (wide blast
   radius, low value), Constant scaffold extraction (single use).

### Risks

| Risk | Mitigation |
|---|---|
| Hook ordering changes break edge cases | Capture current behavior in gtests **before** refactor — at minimum Catalog new+edit, Document new+edit+repost, ChartOfAccounts. Use as regression baseline. |
| Document's `writeMode/postingMode` arg-passing | Member-state side-channel inside the public `Write(wm, pm)` entry, scaffold reads members atomically before any hook fires. |
| Catalog/ChartOf*/Document `ResetUniqueIdentifier` rollback | `PrepareForSaveHook` returns `std::function<void()>` invoked on EVERY rollback site (BeforeWrite cancel, SaveData fail, OnWrite cancel, Posting/UndoPosting fail, register WriteRecordSet/DeleteRecordSet fail). Audit all paths. |
| Phase A helpers conflict with future record-locks observability | Hooks should expose lock-state to the rollback path so future audit-log / metrics can see "TX rolled back because LockConflict at OnWrite". Design the hook signatures with that in mind. |

### What this audit does NOT cover

- `ibValueRecordSetObject` model-layer methods (`GetRow`, `Add`, paged
  table contract) — out of scope; this audit is Write/Delete only.
- `ibValueRecordDataObjectExt` (DataProcessor/Report) Write/Delete —
  they don't persist, no scaffold there.
- Manager-side code (`ibValueManagerDataObject`, `Catalog.FindByCode`)
  — separate concern from object Write path.
- Form-builder / `CreateAndBuildForm` chain — separate refactor surface.

---

## Notes for the refactor

When `docs/record-object-refactor.md` Phase A is scheduled, this
audit's findings should be cross-referenced:

- Use the **drift / bug findings** table as the Phase A.0 fixup list.
- Use the **convergence target** section to confirm scope.
- Use **scaffold growth ratchet** as the elevator-pitch
  justification.

Memory notes to create on landing:
- `project_record_object_audit` — pin this doc as the empirical
  baseline; reference from `project_record_object_refactor`.
- `reference_role_naming_typos` — `Wrire` + `Dimention` historical
  spelling, with migration cost notes.
