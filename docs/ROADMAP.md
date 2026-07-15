# Roadmap — state of the platform

> **How this file is built:** every row below is taken from a **doc's own status line** or
> **verified against the code**, not from recollection. It is a map of *where things
> stand*, so a new session (human or AI) can see the whole board without opening 60 docs.
>
> **Priorities (§6) need the owner's review** — the state is factual, the ordering is a
> proposal.
>
> **Keep it honest:** when an arc's status changes, change it in the arc doc first — this
> file quotes, it does not decide.

---

## 1. Landed — the foundation

These are done and verified; treat them as ground truth, not as open questions.

| Area | Doc | Status as stated there |
|---|---|---|
| Compiler — lambdas | [lambda.md](lambda.md) | landed 2026-05-10 |
| Compiler — closure capture | [closure-capture.md](closure-capture.md) | landed 2026-05-11..12, phases A/B/C/D/F |
| Compiler — eval/scope resolver | [eval-scope-refactor.md](eval-scope-refactor.md) | landed 2026-05-02 |
| Compiler — `PrepareNames` → bind | [preparenames-bind-arc.md](preparenames-bind-arc.md) | landed 2026-06-05 |
| Name binding | [name-binding.md](name-binding.md) | landed 2026-06-02 |
| Query language (full ladder L1–L5) | [query-language-arc.md](query-language-arc.md) | landed (experimental working copy) |
| Dynamic list unification | [dynamic-list.md](dynamic-list.md) | migration DONE (2026-07-11) |
| Composer / model split | [ram-composer-decoupling.md](ram-composer-decoupling.md) | steps 1–4 landed + runtime-verified (2026-07-01) |
| Metadata — const-meta refactor | [const-meta-refactor.md](const-meta-refactor.md) | landed 2026-05-04 (133 files) |
| Metadata — MM decoupling | [metadata-mm-decoupling.md](metadata-mm-decoupling.md) | landed |
| Attribute indexing + register UNIQUE | [attribute-indexing.md](attribute-indexing.md) | landed 2026-07-14 |
| Record-object Write/Delete | [record-object-refactor.md](record-object-refactor.md) | landed 2026-05-25 |
| Record locks | [record-locks.md](record-locks.md) | B.1–B.5 + polish landed |
| Audit log | [audit-log.md](audit-log.md) | landed 2026-05-25 |
| Connection pool | [connection-pool.md](connection-pool.md) | landed |
| Firebird driver hardening | [firebird-driver-hardening.md](firebird-driver-hardening.md) | landed, shipped v1.3.0 |
| Doc/View fork | [docview-fork.md](docview-fork.md) | landed in working tree |

Undocumented-until-now subsystems, mapped 2026-07-15:
[report-engine.md](report-engine.md) · [command-interface.md](command-interface.md) ·
[property-system.md](property-system.md) · [metadata-tree.md](metadata-tree.md) ·
[form-editor.md](form-editor.md).

---

## 2. In flight — landed but not finished

| Area | Doc | What remains |
|---|---|---|
| **Form attribute binding** | [form-attribute-binding.md](form-attribute-binding.md) | "in development, experimental"; first hardened slice landed via designer exercise, **not** a test harness. Surface not spec'd or tested. |
| **Runtime facade** | [runtime-facade.md](runtime-facade.md) | 16 of 17 steps; **Step 12** (cross-bc metadata) open |
| **Compute server** | [compute-server-tiering.md](compute-server-tiering.md) | Phase 1 (pool) + Phase 2 (in-process worker) done; later tiers open |
| **Firebird mesh** | [firebird-mesh-driver.md](firebird-mesh-driver.md) | phases 1, 2, 4, 6 production-validated; rest open |
| **LINQ** | [linq.md](linq.md) | experimental, on top of lambda + iterator |
| **Paging** | [paging-design.md](paging-design.md) | the universal `Get*Fetch` contract (§8) is what lives |
| **Access policy / RLS** | [access-policy-rls.md](access-policy-rls.md) | read + write live; semi-join / temp-promote landed |

---

## 3. Design only — no code

Do not treat these as "nearly done"; they are captured thinking.

| Area | Doc | Status as stated there |
|---|---|---|
| Data policy (declarative platform policies) | [data-policy-arc.md](data-policy-arc.md) | **DESIGN — no code yet** (2026-06-11 session) |
| Register totals (trigger-maintained) | [register-totals-strategy.md](register-totals-strategy.md) | **proposal — NOTHING implemented** |
| Memory allocator | [memory-allocator.md](memory-allocator.md) | **design note / NOT STARTED** |
| Metadata storage container | [metadata-storage-container-arc.md](metadata-storage-container-arc.md) | design detailed; **backlog** until size / partial-save triggers fire |

---

## 4. Known non-functional — verified in code

These are the places where the platform currently **does not do what its shape implies**.
Each was checked against source on 2026-07-15.

### 4.1 Accounting registers — DB execution disabled

`backend/metaCollection/partial/accountingRegisterManager_impl.cpp` carries **four**
`#if 0` blocks, each labelled:

```
#if 0   // accounting NON-FUNCTIONAL — DB execution disabled pending the register's migration to …
```

Consequence: the accounting read path does not execute — Balance / Turnovers do not return
data. The metaobjects, metadata and forms exist; the execution does not. See
[register-totals-strategy.md](register-totals-strategy.md) for the strategy that would
back it.

### 4.2 Web — controls are stubs

`tableBox.cpp` / `tableBoxColumn.cpp` return `new ibWebStubControl(wxT("tablebox"))` /
`("tableboxcolumn")` (`frontend/web/webWindow.h:240`). Per
[release-notes/v1.3.0.md](release-notes/v1.3.0.md), checkbox / combobox / choice / listbox
/ radiobutton / notebook / tablebox-gridbox render as placeholders so metadata loads
cleanly; JS renderers are filled in tier by tier. Forms work to the extent of the ported
controls.

### 4.3 Report — no platform generate action

`backend/metaCollection/partial/dataReportAction.cpp`: `GetActionCollection` returns an
empty collection, `CallAsAction` has an empty body. Running a report is entirely the object
module's script. See [report-engine.md § 5](report-engine.md).

### 4.4 JSON provider — write-only

`ibJsonProvider::Write` emits JSON for diff/inspection; `Read` is a no-op. Do not rely on
JSON import (see [../CLAUDE.md](../CLAUDE.md) § Configuration Serialization).

### 4.5 Open defect

[debugger-per-session.md](debugger-per-session.md) — one heisenbug, status "open,
deferred".

---

## 5. Drivers

Firebird (default, embedded), PostgreSQL, SQLite (always embedded), MySQL, ODBC —
all behind `ibDatabaseLayer` ([database-layer.md](database-layer.md),
[../CLAUDE.md](../CLAUDE.md) §1).

**Oracle and MSSQL are not in this tree — but they are a PORT, not a green field.**
`ibDatabaseLayer` is a fork of wxDatabaseLayer, which shipped Oracle and MSSQL drivers
against this same architecture ([database-layer.md § 4](database-layer.md)). Estimate the
work as "port a known-good reference driver", not "write one from scratch".

---

## 6. Proposed ordering — needs owner review

The state above is factual. This section is a **proposal** and the one part of this file
that should be argued with.

1. **Web controls** — §4.2 is the widest gap between "the platform can" and "a user can".
   Desktop forms work; the web renders placeholders for exactly the controls a real
   application form is made of (tablebox above all).
2. **Form attribute binding → spec + tests** — §2 says it is exercised through the
   designer, not a harness. It is the substrate everything else in the form editor stands
   on; leaving it unspec'd taxes every arc above it.
3. **Accounting execution** — §4.1 is a whole advertised metatype that does not execute.
   Either finish the migration or state the register as out of scope.
4. **Report generate action** — §4.3; small, and it turns reports from "script it" into a
   platform feature.
5. Design-only arcs (§3) stay parked until a concrete need pulls them.

---

## 7. Not covered by any doc

**The 2026-07-15 pass closed the structural gaps.** The foundation — the code that predates
most arcs here and that everything rides on — is now mapped:

[property-system.md](property-system.md) (the skeleton: 5 surfaces) ·
[compiler-pipeline.md](compiler-pipeline.md) (translate → compile → execute + runtime
assembly) · [factories.md](factories.md) (ctor registries + two-phase `Init`) ·
[enumerations.md](enumerations.md) (+ the recipe) ·
[descriptions.md](descriptions.md) (storage-shape pattern) ·
[source-object.md](source-object.md) · [script-value-types.md](script-value-types.md) ·
[system-functions.md](system-functions.md) · [serialization-io.md](serialization-io.md) ·
[pictures.md](pictures.md) · [database-layer.md](database-layer.md) ·
[database-modes.md](database-modes.md) · [debugger-architecture.md](debugger-architecture.md) ·
[main-frame.md](main-frame.md) · [metadata-tree.md](metadata-tree.md) ·
[form-editor.md](form-editor.md) · [spreadsheet-editor.md](spreadsheet-editor.md) ·
[designer-editors.md](designer-editors.md) · [report-engine.md](report-engine.md) ·
[command-interface.md](command-interface.md) · [wx-fork.md](wx-fork.md).

Remaining thin spots (known, deliberate):

- **`roleEditor` / `interfaceEditor`** — read at class level only
  ([designer-editors.md § 5](designer-editors.md)); the two smallest Designer surfaces.
- **`dlgs/` (35 files)** and **`theme/`** — undocumented; low leverage.
- **`ctrls/charts/`** — 246 vendored wxCharts files, deliberately not documented
  ([wx-fork.md § 3](wx-fork.md)).
- `backend/utils/`, `backend/diagnostics/` — small, no doc.

## 8. Lineage — what came from where

Foundation code has parents, and knowing them changes estimates. Recorded so a reader does
not mistake inherited shape for original design:

| Subsystem | Origin | State |
|---|---|---|
| **Form editor** | **wxFormBuilder** — also the source of the *property* idea | ~5% of the original remains; traces confined to `designer/…/visualEditor` ([form-editor.md § 1](form-editor.md)) |
| **Database layer** | **wxDatabaseLayer** (wxCode, last release 2009) | ~40% resemblance; architecture kept, drivers reworked. **Oracle / MSSQL exist upstream** ([database-layer.md § 1](database-layer.md)) |
| **UI kit** | wxWidgets fork (wxUniversal + Luna) | the fork is a teacher/substrate, not debt ([uikit.md](uikit.md)) |
| **Designer + debugger** | **written from scratch** | TCP replaced the era's DDE approach; the debuggee is the server ([debugger-architecture.md § 1-2](debugger-architecture.md)) |
| `ibNumber` | ttmath dependency **removed** | self-contained; ttmath credit retained ([../CLAUDE.md](../CLAUDE.md) §2a) |
