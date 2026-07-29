# Record-object Write/Delete scaffold refactor

> **Status:** **LANDED 2026-05-25 in commit `fc4efa55`** (Phase A +
> Phase B together — the original "wait for record-locks to settle"
> caveat dropped, both arcs landed back-to-back). Build clean
> Debug|x86, smoke-validated on Catalog / Document (new + edit,
> repost, parallel-write version conflict) + the 3 register kinds.
>
> What actually shipped vs the proposal below:
>
> - **Phase A (Begin*/Commit*Scope helpers)** — landed as
>   `RefQuery` + `RecordSetQuery` extraction. Pre/post Write boilerplate
>   (designer-skip, scope/access guard, TX begin, lock+version check,
>   commit + notify + clear-modified) lives once.
> - **Phase B (template-method on bases)** — `WriteObject` /
>   `DeleteObject` defined on `ibValueRecordDataObjectHierarchyRef`;
>   `WriteRecordSet` / `DeleteRecordSet` on `ibValueRecordSetObject`.
>   6 leaves inherit verbatim (Catalog / ChartOfAccounts /
>   ChartOfCharacteristicTypes + 3 register kinds).
> - **Document (the outlier)** — promoted to a new intermediate
>   `ibValueRecordDataObjectRecorderRef` ("ref with movements")
>   between Ref and Document. Posting state machine + register
>   cascade + SetDeletionMark-with-un-post live on this intermediate;
>   Document itself collapses to leaf hooks. Late-bind
>   `InitRegisterRecords()` in leaf ctor body avoids
>   pure-virtual-in-base-ctor on register `CreateRecordSet`.
> - **ShowFormValue / GetFormValue** — hoisted to
>   `ibValueRecordDataObject`. 6 leaves reduced to
>   `GetCurrentObjectFormID()` hook. Constant / InformationRegister
>   keep their own (manager/key-object form acquisition is
>   structurally different).
> - **Subclass-list trap closed structurally** —
>   `FillArrayObjectByPredefinedAttribute` switched from destructive
>   assign to an additive chain. The "DataVersion declared on base
>   but missing from subclass list" bug class is now extinct, not
>   just patched. See [[reference-predefined-attr-subclass-lists]] —
>   memory note remains as historical record.
> - **`SetDefaultProcedure` hooks** — 6 common hooks hoisted into
>   `MutableRef` ctor. Document keeps its 3-arg `BeforeWrite` +
>   `Posting` / `UndoPosting` / `SetNewNumber`.
>
> The proposal text below is preserved as historical record — useful
> when reading the actual diff against `fc4efa55`'s parent.
>
> **Original rationale (kept for reference):** The 2026-05-24
> record-locks arc surfaced the duplication acutely — adding one line
> of `LockAndCheckDataVersion()` / `LockByKeys()` / inline-lock had to
> happen in 15 places almost identically. The next safety / audit /
> log addition would have paid the same cost; this refactor cuts that
> cost to one site.

---

## Inventory — 15 nearly-identical scaffolds

Three families, each with its own lifecycle nuances but identical
shape inside the family:

| Family | Base class | Files | Methods per file |
|---|---|---|---|
| **Mutable-refs** | `ibValueRecordDataObjectRef` | catalogObject, documentObject, chartOfAccountsObject, chartOfCharacteristicTypesObject | `WriteObject` + `DeleteObject` |
| **Registers** | `ibValueRecordSetObject` | accumulationRegisterObject, accountingRegisterObject, informationRegisterObject | `WriteRecordSet` + `DeleteRecordSet` |
| **Constants** | `ibValueRecordDataObjectConstant` | constantObject | `SetConstValue` |

Totals: **4 × 2 = 8** mutable-ref methods, **3 × 2 = 6** register
methods, **1** constant method = **15 scaffolds**.

### Canonical scaffold (mutable-refs example)

Every one of the 15 follows this template (~50-70 lines each):

```cpp
bool ibValueRecordDataObjectXxx::WriteObject()
{
    if (appData->DesignerMode()) return true;                          // (1) designer skip

    ibConnectionScope scope = ibSession::Current()->OpenConnectionScope();
    if (!scope || !scope->IsOpen())                                    // (2) DB-open guard
        ibBackendCoreException::Error(_("Database is not open!"));

    if (ibBackendException::IsEvalMode()) return true;                 // (3) eval-mode skip

    if (!m_metaObject->AccessRight_Write()) {                          // (4) access right
        ibBackendAccessException::Error();
        return false;
    }

    ibBackendValueForm* const valueForm = GetForm();                   // (5) form-notify target

    scope.SafeBeginTransaction();                                      // (6) TX begin
    LockAndCheckDataVersion(/*bump=*/true);                            // (7) lock + version

    {                                                                  // (8) BeforeWrite + cancel
        ibValue cancel = false;
        ExecAsProc(wxT("BeforeWrite"), cancel);
        if (cancel.GetBoolean()) {
            scope.SafeRollBackTransaction();
            ibBackendCoreException::Error(_("Failed to write object in db!"));
            return false;
        }
    }

    /* === type-specific middle === */                                 // (9) variation point
    // Catalog: generateUniqueIdentifier + SetNewCode script
    // Document: writeMode/postingMode + posting logic
    // Charts: similar to Catalog without owner
    // (this is the ONLY meaningfully-different block per type)

    if (!SaveData()) {                                                 // (10) actual save
        scope.SafeRollBackTransaction();
        ibBackendCoreException::Error(_("Failed to write object in db!"));
        return false;
    }

    {                                                                  // (11) OnWrite + cancel
        ibValue cancel = false;
        ExecAsProc(wxT("OnWrite"), cancel);
        if (cancel.GetBoolean()) {
            scope.SafeRollBackTransaction();
            ibBackendCoreException::Error(_("Failed to write object in db!"));
            return false;
        }
    }

    scope.SafeCommitTransaction();                                     // (12) TX commit

    if (newObject && valueForm != nullptr) valueForm->NotifyCreate(GetReference());
    else if (valueForm != nullptr) valueForm->NotifyChange(GetReference());
    m_objModified = false;                                             // (13) post-commit notify

    return true;
}
```

Steps (1)-(8) + (10)-(13) are **identical** across all 8 mutable-ref
WriteObject/DeleteObject methods. Only step (9) varies.

Register and Constant scaffolds follow the same shape with their own
lock-helper substitution.

### Per-family variation points

| Family | Step (9) variation | Lock helper (step 7) | Action (step 10) |
|---|---|---|---|
| Mutable-refs / Catalog | `generateUniqueIdentifier` + `SetNewCode` script + rollback callback | `LockAndCheckDataVersion(true/false)` | `SaveData()` / `DeleteData()` |
| Mutable-refs / Document | `writeMode` + `postingMode` args to BeforeWrite | same | same |
| Mutable-refs / Charts | Catalog without owner; one-line condensed | same | same |
| Registers / Accumulation | none (clean) | `LockByKeys()` | `SaveData(replace, clearTable)` / `DeleteData()` |
| Registers / Accounting | none | same | same |
| Registers / Information | none | same | same |
| Constant | `m_constValue` save+restore lambda for rollback | inline `SELECT 1 FROM <tbl> WHERE RECORD_KEY = '6' FOR UPDATE` | `UPDATE` SQL |

### Why this matters

Cross-cutting changes pay the duplication tax. Concrete recent examples:

| Change | Lines added | Touched files |
|---|---|---|
| `LockAndCheckDataVersion()` insertion (2026-05-24, record-locks) | 8 ref-objects | 4 files × 2 methods |
| `LockByKeys()` insertion (2026-05-24, record-locks) | 6 register-objects | 3 files × 2 methods |
| Constant inline lock (2026-05-24, record-locks) | 1 constant | 1 file × 1 method |
| `ibSession::Current()->OpenConnectionScope()` migration (earlier, connection-pool) | all 15 sites | every file |
| `SafeBeginTransaction` / `SafeCommitTransaction` migration (earlier) | all 15 sites | every file |
| `ExecAsProc` rename from raw `m_procUnit->CallAsProc` (earlier) | all 15 sites | every file |
| Hypothetical future: audit log on Write | will be 15 sites | every file |

Every cross-cutting concern on the write path multiplies by 15.

---

## Proposed shape

Template-method per family. Base class owns the scaffold (steps 1-8,
10-13). Subclasses override **only** the variation hooks.

### Mutable-refs family

```cpp
class BACKEND_API ibValueRecordDataObjectRef : public ... {
public:
    // Scaffold methods — sealed.  Algorithm shape stays here forever;
    // subclasses only fill in the hooks below.
    bool WriteObject() final;
    bool DeleteObject() final;

protected:
    // ---- Hooks ----
    // Called inside the WriteObject TX, between LockAndCheck and SaveData.
    // Default: no-op.  Catalog overrides for code-generation;
    // Document overrides to wire writeMode/postingMode.
    //
    // Return false to abort (rollback + caller-error).  cancelMessage
    // out-param feeds the error text if cancellation came from a script
    // BeforeWrite cancel; empty for non-script aborts.
    virtual bool OnBeforeWriteHook(ibConnectionScope& scope,
                                    wxString& cancelMessage) {
        ibValue cancel = false;
        ExecAsProc(wxT("BeforeWrite"), cancel);
        if (cancel.GetBoolean()) {
            cancelMessage = _("Failed to write object in db!");
            return false;
        }
        return true;
    }

    // Pre-save step (Catalog: generateUniqueIdentifier).  Returns a
    // rollback callback that fires if anything later in the scaffold
    // fails — Catalog uses this to ResetUniqueIdentifier on cancel.
    // Default: no-op, no rollback.
    virtual std::function<void()> PrepareForSaveHook(
            ibConnectionScope& scope) {
        return []{};  // empty rollback
    }

    // Called after SaveData succeeds.  Default fires OnWrite script.
    virtual bool OnAfterWriteHook(ibConnectionScope& scope,
                                   wxString& cancelMessage) {
        ibValue cancel = false;
        ExecAsProc(wxT("OnWrite"), cancel);
        if (cancel.GetBoolean()) {
            cancelMessage = _("Failed to write object in db!");
            return false;
        }
        return true;
    }

    // Mirror trio for Delete.
    virtual bool OnBeforeDeleteHook(ibConnectionScope&, wxString&);
    virtual bool OnAfterDeleteHook(ibConnectionScope&, wxString&);
};
```

WriteObject implementation collapses to:

```cpp
bool ibValueRecordDataObjectRef::WriteObject()
{
    if (appData->DesignerMode())        return true;

    ibConnectionScope scope = ibSession::Current()->OpenConnectionScope();
    if (!scope || !scope->IsOpen())
        ibBackendCoreException::Error(_("Database is not open!"));

    if (ibBackendException::IsEvalMode())                return true;
    if (!m_metaObject->AccessRight_Write())              { ibBackendAccessException::Error(); return false; }

    ibBackendValueForm* const valueForm = GetForm();
    const bool newObject = IsNewObject();

    scope.SafeBeginTransaction();
    LockAndCheckDataVersion(/*bump=*/true);

    wxString cancelMsg;
    if (!OnBeforeWriteHook(scope, cancelMsg)) {
        scope.SafeRollBackTransaction();
        ibBackendCoreException::Error(cancelMsg);
        return false;
    }

    auto rollback = PrepareForSaveHook(scope);

    if (!SaveData()) {
        rollback();
        scope.SafeRollBackTransaction();
        ibBackendCoreException::Error(_("Failed to write object in db!"));
        return false;
    }

    if (!OnAfterWriteHook(scope, cancelMsg)) {
        rollback();
        scope.SafeRollBackTransaction();
        ibBackendCoreException::Error(cancelMsg);
        return false;
    }

    scope.SafeCommitTransaction();

    if (valueForm != nullptr)
        newObject ? valueForm->NotifyCreate(GetReference())
                  : valueForm->NotifyChange(GetReference());
    m_objModified = false;
    return true;
}
```

~30 lines instead of 60-80 per subclass. Catalog/Document/Charts each
keep ~10-20 lines of override hooks instead of full methods.

### Document — the tricky case

`WriteObject(ibDocumentWriteMode writeMode, ibDocumentPostingMode postingMode)` takes
args. Options:

A. Member-store the args before calling `WriteObject()`:
```cpp
bool ibValueRecordDataObjectDocument::Write(ibObjectMode wm, ibObjectMode pm) {
    m_writeMode = wm; m_postingMode = pm;
    return WriteObject();  // base scaffold, hooks read m_writeMode/m_postingMode
}
```

B. Override `WriteObject` to NOT be final on Document, and let
Document have its own scaffold:
```cpp
class ibValueRecordDataObjectRef {
    virtual bool WriteObject();  // virtual, not final
};
```
Document overrides fully. Loses some unification.

Option **A** is cleaner and keeps the scaffold uniform; member state is
short-lived (lives for the duration of one Write call) so no
side-effect concerns.

### Registers family

```cpp
class BACKEND_API ibValueRecordSetObject : public ... {
public:
    bool WriteRecordSet(bool replace = true, bool clearTable = true) final;
    bool DeleteRecordSet() final;

protected:
    virtual bool OnBeforeWriteHook(ibConnectionScope&, wxString& cancelMessage);
    virtual bool OnAfterWriteHook(ibConnectionScope&, wxString& cancelMessage);
    virtual bool OnBeforeDeleteHook(ibConnectionScope&, wxString& cancelMessage);
    virtual bool OnAfterDeleteHook(ibConnectionScope&, wxString& cancelMessage);
};
```

Scaffold body does steps 1-8 then `LockByKeys()` then hooks +
`SaveData(replace, clearTable)` / `DeleteData()` then commit + Modified.

Accumulation/Accounting/Information register subclasses end up with
**empty overrides** in the common case — they all default to firing
BeforeWrite/OnWrite scripts. So the override blocks vanish entirely
for the 3 register types.

### Constants — leave as-is

One method, one file. Doesn't pay to extract for one caller. The
inline scaffold stays.

If/when other constant-like singletons appear (`sys_config` write,
maybe), revisit.

---

## What stays type-specific

After the refactor, **per-subclass code** is narrowly scoped to:

- **Catalog**: `OnBeforeWriteHook` calls super then runs `SetNewCode`
  script + `generateUniqueIdentifier`; `PrepareForSaveHook` returns a
  `ResetUniqueIdentifier` rollback closure.
- **Document**: `OnBeforeWriteHook` calls `ExecAsProc("BeforeWrite",
  cancel, writeMode, postingMode)` instead of bare 1-arg; reads
  `m_writeMode/m_postingMode` set by public `Write(wm, pm)` entry.
- **ChartOfAccounts / ChartOfCharacteristicTypes**: same as Catalog
  but without owner-specific bits.
- **Registers**: typically zero overrides — default
  BeforeWrite/OnWrite script firing covers them.

Total per-subclass code is ~10-30 lines instead of 100-150.

---

## Phase plan

Two-phase landing keeps risk staged.

### Phase A — extract scaffold helpers (low risk)

Goal: ~70% dedup, control-flow visible at every call site, no
behavioral change.

Add to base classes:

```cpp
class ibValueRecordDataObjectRef {
protected:
    // Designer/access/eval/scope-open/begin-tx/lock — all the pre-action
    // boilerplate. Returns false if caller should bail (designer/eval
    // skipped, or access denied surfaced as exception).
    bool BeginWriteScope(ibConnectionScope& outScope);
    bool BeginDeleteScope(ibConnectionScope& outScope);

    // Commit + notify + clear-modified.
    void CommitWriteScope(ibConnectionScope& scope,
                          ibBackendValueForm* form, bool newObject);
    void CommitDeleteScope(ibConnectionScope& scope,
                           ibBackendValueForm* form);
};

class ibValueRecordSetObject {
protected:
    bool BeginRecordSetWriteScope(ibConnectionScope& outScope);
    bool BeginRecordSetDeleteScope(ibConnectionScope& outScope);
    void CommitRecordSetScope(ibConnectionScope& scope);
};
```

Each WriteObject/DeleteObject body becomes:

```cpp
bool ibValueRecordDataObjectCatalog::WriteObject()
{
    ibConnectionScope scope;
    if (!BeginWriteScope(scope)) return true;  // designer/eval skipped

    ibBackendValueForm* const valueForm = GetForm();
    const bool newObject = IsNewObject();

    /* === type-specific middle: BeforeWrite + codegen + SaveData + OnWrite === */
    // Same as today, but the surrounding bookkeeping is in helpers.

    CommitWriteScope(scope, valueForm, newObject);
    return true;
}
```

Control flow stays visible (`BeforeWrite` cancel handling, `SaveData`
rollback) but the pre/post bookkeeping moves out. Risk: low — just
extraction, no semantic change.

**Estimate:** ~1 week. Subclass bodies shrink from 60-80 to 30-40
lines each. Cross-cutting future changes touch 6 helpers (3 families
× Begin/Commit) instead of 15 call sites.

### Phase B — full template-method (higher risk, optional)

Goal: 100% scaffold dedup. Subclasses are pure hooks.

Mechanism per "Proposed shape" above. Requires:
- Full ordered audit of script-hook semantics (`BeforeWrite` cancel,
  arg shapes, return-value contract)
- Manual smoke per object kind (Catalog new + Catalog edit + Document
  new + Document edit + Document repost + ChartOfAccounts + ChartOf
  CharacteristicTypes + each register kind)
- Verification that rollback ordering matches today's behavior for
  Catalog's `generateUniqueIdentifier` → `ResetUniqueIdentifier`
  contract

**Estimate:** ~2-3 weeks. Subclass bodies shrink to ~10-20 lines of
hook overrides. Cross-cutting changes touch a single base scaffold.

Skip Phase B if Phase A's helpers prove sufficient in practice.

---

## Risks

| Risk | Mitigation |
|---|---|
| Reorder of operations breaks edge cases (eval-mode, designer-mode interaction with new lock check) | Manual smoke per object kind per scenario before merging. Capture the current behavior in tests **first** (Phase A.0). |
| `BeforeWrite` cancel semantics change | Keep the cancel signal path (return false → rollback → Error) bit-perfect. No alternative cancel mechanism. |
| Document's `writeMode/postingMode` arg passing through member-state side-channel surprises some script-side caller | Document's public `Write(wm, pm)` keeps its existing signature; internal scaffold reads the members atomically before any script call. |
| Catalog's `generateUniqueIdentifier` rollback callback misses one of the cancel paths | `PrepareForSaveHook` returns a `std::function<void()>` invoked on EVERY rollback site (BeforeWrite cancel, SaveData fail, OnWrite cancel). Audit all three. |
| `ibValueRecordDataObjectHierarchyRef` (Catalog/Charts hierarchical) needs additional bookkeeping (folder vs item mode) | Override hooks in HierarchyRef base, subclasses inherit. Already a layer in the hierarchy. |
| Existing gtests rely on specific exception messages from specific sites | Run full test suite before/after each phase; messages stay identical (centralized in base). |

---

## Files touched (Phase A)

| File | Change |
|---|---|
| `backend/metaCollection/partial/commonObject.h` | Add `Begin*Scope` / `Commit*Scope` decls on `ibValueRecordDataObjectRef` + `ibValueRecordSetObject` |
| `backend/metaCollection/partial/commonObject.cpp` | Impls for ibValueRecordDataObjectRef helpers |
| `backend/metaCollection/partial/commonObjectRecordSetQuery.cpp` (or commonObject.cpp) | Impls for ibValueRecordSetObject helpers |
| `backend/metaCollection/partial/catalogObject.cpp` | WriteObject/DeleteObject use Begin*/Commit* helpers |
| `backend/metaCollection/partial/documentObject.cpp` | Same |
| `backend/metaCollection/partial/chartOfAccountsObject.cpp` | Same |
| `backend/metaCollection/partial/chartOfCharacteristicTypesObject.cpp` | Same |
| `backend/metaCollection/partial/accumulationRegisterObject.cpp` | WriteRecordSet/DeleteRecordSet use helpers |
| `backend/metaCollection/partial/accountingRegisterObject.cpp` | Same |
| `backend/metaCollection/partial/informationRegisterObject.cpp` | Same |
| `backend/metaCollection/partial/constantObject.cpp` | (no change — single method stays inline) |

Net code delta: ~-300 lines (15 scaffolds collapse to subclass-specific
middles + 6 base helpers).

## Files touched (Phase B — additional)

| File | Change |
|---|---|
| `backend/metaCollection/partial/commonObject.h` | Mark WriteObject/DeleteObject `final` on base; add `OnBefore*Hook` / `On*AfterHook` / `PrepareForSaveHook` virtuals |
| `backend/metaCollection/partial/commonObject.cpp` | Full template-method bodies for the 4 scaffolds |
| Each subclass `.cpp` | Implement hooks only; remove WriteObject/DeleteObject bodies |
| Document's `Write(wm, pm)` entry point | Sets members then calls `WriteObject()` |

Net code delta: additional ~-200 lines.

## Open questions

1. **Document's `writeMode/postingMode` carrying via member state** —
   confirm no other code path reads `WriteObject()` directly expecting
   the no-args overload to mean "use defaults." Audit `ProcessAction`
   sites that dispatch Write.
2. **Phase A helpers' visibility** — `protected` (subclass-only) seems
   right. `friend class` patterns shouldn't be needed.
3. **Constants edge case** — if a second constant-like singleton ever
   shows up (e.g., `sys_config.WriteParam`), revisit single-method
   stance.
4. **Pre-refactor gtests** — Phase A should be preceded by a gtest
   that captures current Write behavior end-to-end for at least
   Catalog + Document (parallel write with version conflict, BeforeWrite
   cancel, OnWrite cancel, SaveData fail). Use as regression baseline.

## Related docs and memory

- [`record-locks.md`](record-locks.md) — the arc that surfaced the
  pain. The `LockAndCheckDataVersion()` / `LockByKeys()` helpers
  introduced there are the direct precedent for the kind of
  pre-action extraction proposed here.
- [`connection-pool.md`](connection-pool.md) — `ibConnectionScope`
  + counter-based nested TX. Scaffold helpers must preserve
  scope-RAII semantics (rollback on early return).
- [`backend-frontend-split.md`](backend-frontend-split.md) — the
  similar "chunky interface" smell pattern documented for
  `ibBackendDocFrame`. Same direction-of-travel — extract scaffold,
  leave variation points exposed.

Memory notes to create on landing:
- `project_record_object_refactor` — Phase A/B status, choice
  between member-state vs override for Document
- `reference_write_scope_helpers` — `Begin*Scope` / `Commit*Scope`
  contract + rollback semantics
- `feedback_record_object_template_method` — once Phase B lands,
  capture the chosen hook shape so future similar refactors mirror it

---

## Notes

- This refactor does not touch the **types** of objects, only the
  **write/delete scaffold around them**. Schema, metadata, script
  surface — all unchanged.
- The refactor pays off most when the NEXT cross-cutting concern
  arrives (audit log, retry-on-deadlock, write-through cache, …).
  Each such concern saves ~14 sites × ~5 lines = ~70 lines of
  change + ~14 chances for one site to drift out of sync.
- If you find yourself adding the same scaffold change to all 15
  sites BEFORE this refactor lands, that's the signal to schedule
  Phase A immediately.
