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

## 1a. Unblocked and verified — 2026-08-02 session

Everything in this section was **built and run**, not reasoned about. Solution `Debug|x86`
compiles with 0 errors; `oes_tests` **855/855**; `oes_frontend_runtime_test` **26/26 in ~2 s**.

| What | State before | State now |
|---|---|---|
| **Frontend GUI harness** | Target did not compile (API drift), 17 tests `DISABLED_`, each surviving test ~30 s | Compiles, **26/26 green**, whole suite ~2 s |
| **Report — platform action** | `dataReportAction.cpp` empty: no command, no body | **Compose** standard command → object module's declared `Composing(StandartProcessing)` handler |
| **Register totals — numeric parity** | Unmeasured; nothing compared the two paths | **Green under a live engine** — and the check immediately found two bugs that made materialisation impossible on SQLite |
| **Form attribute binding** | Surface unspec'd, runtime reference chain untested | *Surface contract* written; runtime chain + the non-owning-cell rule pinned by tests |
| **JSON provider** | `Read` a silent no-op | Full parser (unwired by design), with its lossy boundary pinned in tests |
| **WrapSizer** | Implemented end to end, registered nowhere — dead on both platforms | Registered; reachable on desktop and web |
| **CI** | None | `.github/workflows/ci.yml` — **four** jobs: Linux suite, Windows build+suite, GUI under Xvfb, and macOS 14 arm64 (added 2026-08-03). Every job also **links the applications**, which no job did before — the first run that tried failed on both platforms at once (`codeRunner` had 21 unresolved externals, its CMake file having never listed `frontend`). State on 2026-08-03: **all four green — Linux, Windows and macOS arm64 at 919/919 each, GUI 26/26**, every application linked on every platform. macOS is also the fastest job (~22 min vs 33 and 37) and the only one that RUNS the suite on AArch64, where unsigned `char`, a weaker memory model and different alignment are invisible to any compiler. See [portability.md § 3](portability.md) |
| **Portability** | GCC/Linux had not compiled the tree in a long time | Fourteen CI rounds took it from "does not compile" to green build + 95% of the suite; the rules are written down in [portability.md](portability.md) |

**Defects surfaced, none of them in the new code** — they had been sitting in paths nothing
exercised. The first four came from the test work, the rest from the first GCC build in a long
time (full list and the rules that prevent them: [portability.md](portability.md)):

1. **`ibMaterializeSql::Apply` could not install a bundle on SQLite at all.** A driver
   reports failure by THROWING, so the return-code test never saw it; and the error
   sentinel is `0`, which is also the affected-row count of successful DDL — so with the
   throw caught, every successful `CREATE` then read as a failure. Firebird masked both
   (its drops carry existence guards). See [register-totals-strategy.md](register-totals-strategy.md) §1.
2. **`ibValueFrame::Init` gated on `lSizeArray < 2` while reading `paParams[2]`** — an
   out-of-bounds read that never fired only because the one live caller passes 3.
3. **A control built with no parent silently belongs to no tree** (`Init` only calls
   `AddChild` when given a parent) — the actual cause of the 17 disabled tests.
4. **A stack-allocated `ibDocument` is a use-after-free**: `OnChangedViewList` does
   `delete this` when the last view detaches.
5. **The driver options were fiction.** `BUILD.md` advertises `OES_USE_*` as "all default OFF",
   but `appData.cpp` named Firebird and PostgreSQL unconditionally while CMake dropped an
   off driver's sources — so any such build failed to link. Compounded by MSVC instantiating an
   exported class in every TU that merely *includes* its header, which made guarding the code
   without guarding the include useless.
6. **The GTK guid path had never compiled**: `guid.cpp` included `<guid/guid.h>`, a path that
   exists nowhere (it is `<uuid/uuid.h>`), and libuuid was not linked at all.
7. **The web server's POSIX branch was half-written**: its `#if defined(_WIN32)` block pulled in
   winsock, while the `#else` contained only `<csignal>` — no socket headers for the
   `setsockopt` calls right below.
8. **Two 64-bit alias families that are the same type on Windows** (`ibClassID`/`ibPictureID`
   over `wxLongLong_t` vs `s64`/`u64` over `<cstdint>`) and different on LP64, so ids could not
   bind to the reader/writer layer's references. Now one base, `uint64_t`, with no change to any
   serialised width.

---

## 2. In flight — landed but not finished

| Area | Doc | What remains |
|---|---|---|
| **Form attribute binding** | [form-attribute-binding.md](form-attribute-binding.md) | "in development, experimental". **Surface spec'd 2026-08-02** (§ *Surface contract*: entry points, guarantees, and the test pinning each rule). Plumbing is covered by four gtest TUs — `test_sourceDescription` / `test_tabularHop` / `test_sourceExplorer` / `test_sourceHopChain` (the last closes the runtime reference chain + the non-owning-cell hazard). Still unharnessed: anything needing a live form — holder façade, `IsWritableBinding`, designer paths. |
| **Runtime facade** | [runtime-facade.md](runtime-facade.md) | 16 of 17 steps; **Step 12** (cross-bc metadata) open |
| **Compute server** | [compute-server-tiering.md](compute-server-tiering.md) | Phase 1 (pool) + Phase 2 (in-process worker) done; later tiers open |
| **Firebird mesh** | [firebird-mesh-driver.md](firebird-mesh-driver.md) | phases 1, 2, 4, 6 production-validated; rest open |
| **LINQ** | [linq.md](linq.md) | experimental, on top of lambda + iterator |
| **Paging** | [paging-design.md](paging-design.md) | the universal `Get*Fetch` contract (§8) is what lives |
| **Access policy / RLS** | [access-policy-rls.md](access-policy-rls.md) | read + write live; semi-join / temp-promote landed |
| **Job manager** | [job-manager.md](job-manager.md) | scheduled + background work through the worker pool. Landed: per-session pool choice, `ibJobManager` on appData, `RunScheduledJobs` / `RunJob` + the file-base timer, `totals.fold` as first tenant, cross-process claim via `sys_lock`, job state/outcome, and the `ibValue::IsTransferable` gate. Background jobs are live: `RunBackground("Module.Method", args)` returns a vended `BackgroundJob` value (`IsComplete` / `Wait` / `Result` / `Error` / `Activity` / `Cancel`), runs on its own session under the caller's identity, and shows in Active Users as its own session kind (BackgroundJob / ScheduledJob / SystemJob). `ibFirebirdMaintenanceScheduler` folded in — it lost its thread and is now the `firebird.maintenance` job. Full schedules (`ibJobSchedule`: interval + time window + weekdays + month days + months + validity range, with `ToString` / `NextAllowedAfter`), the manager's own tick thread, and cross-process synchronisation (`sys_lock` claim owned by the job's session + shared `sys_job.lastRun` through L2 upsert). Open: background fetch for reports; naming a *different* user than the caller (needs a role gate); metadata for configuration-declared scheduled jobs |
| **Register totals** | [register-totals-strategy.md](register-totals-strategy.md) | reading works (three virtual tables via live aggregation); the trigger-maintained bundle is declared by the accumulation register and applied on a live Firebird, surviving a kind switch (2026-07-29). Numeric parity of the two paths is now **verified GREEN under a live engine** (2026-08-02): `tests/test_totalsNumericParity.cpp` installs the real rendered bundle on in-memory SQLite and compares maintained totals against re-aggregated movements — accumulation, month truncation, updates across both key columns, deletes, backdated entries, fractional values, mixed traffic. It paid for itself on the first run by exposing **two bugs in `ibMaterializeSql::Apply` that made a first apply on a clean SQLite database impossible** (a driver signals failure by throwing, so the return-code test never saw it; and the "error" sentinel is 0, which is also the affected-row count of successful DDL) — see [register-totals-strategy.md](register-totals-strategy.md) §1. Not retired: real traffic volume and Firebird's own trigger family. Open: the accounting register declares no totals; boundary-row Fill and the routing of recorder / sub-day readings to the movements are absent |

---

## 3. Design only — no code

Do not treat these as "nearly done"; they are captured thinking.

| Area | Doc | Status as stated there |
|---|---|---|
| Data policy (declarative platform policies) | [data-policy-arc.md](data-policy-arc.md) | **DESIGN — no code yet** (2026-06-11 session) |
| Metadata hot reload (change classes + tombstone door) | [metadata-hot-reload.md](metadata-hot-reload.md) | **PROPOSAL — NOTHING implemented** (2026-07-15 session) |
| Memory allocator | [memory-allocator.md](memory-allocator.md) | **design note / NOT STARTED** |
| Metadata storage container | [metadata-storage-container-arc.md](metadata-storage-container-arc.md) | **FOLDED (2026-06-17)** into [schema-first-metadata.md](schema-first-metadata.md) — "single-blob → per-entry rows" *is* file-per-object; pursue it through that direction, not as a standalone storage refactor |
| Metaobject naming (designer labels, script names, tree order) | [metaobject-naming.md](metaobject-naming.md) | **PLAN — nothing applied** (2026-07-27). The script-visible half is near-free now and gets dearer with every configuration written. |
| Payroll | [payroll-arc.md](payroll-arc.md) | **DESIGN — no code yet** (2026-08-02). No calculation-register metatype: the one platform piece is an `Intervals(from, to)` reading companion beside `SliceLast` / `SliceFirst`; displacement is a configured table plus interval arithmetic in the run. Phase 0 is worth doing on its own (prices, rates, deferred reserve). |

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
data. The metaobjects, metadata and forms exist; the execution does not. The strategy that
backs it is no longer hypothetical — it is declared and applied for the accumulation register
— but the accounting register contributes no totals of its own yet. See
[register-totals-strategy.md](register-totals-strategy.md).

### 4.2 Web — the control surface is roughly one third ported

Re-verified against `frontend/wfrontend.vcxproj` + each control's `OES_USE_WEB` branch on
2026-07-27. The gap is **wider than "renders as a placeholder"** — most controls are not in
the web build at all, so their clsid never registers and the form logs *unregistered
clsid* rather than drawing a placeholder.

Three distinct states, not one:

| State | Controls | What a user sees |
|---|---|---|
| **Ported** — real `ibWeb*` widget (`frontend/web/webWindow.h`) | StaticText, Button, CheckBox, TextCtrl, ToolBar + ToolBarItem/Separator, and the sizers (Box / Grid / StaticBox / Item / Wrap) | Works |
| **Stub** — compiled in, returns `ibWebStubControl` | TableBox, TableBoxColumn, form object (`formObject.cpp`) | Placeholder block; metadata still loads |
| **Absent** — file not in `wfrontend.vcxproj`, no web branch | ComboBox, Choice, ListBox, RadioButton, Notebook, Gauge, Slider, GridBox, HtmlBox, ChartBox, TextBox, StaticLine | clsid unregistered |

Two corrections to that table, both found by re-verification on 2026-07-29:

- ~~**WrapSizer is a fourth state — implemented end to end, never registered.**~~ **FIXED
  2026-08-02.** It had a desktop implementation, an `OES_USE_WEB` branch (`ibWebWrapSizer`,
  `web/webSizer.h`) and JS (`webClient.cpp`), but no `CONTROL_TYPE_REGISTER` — unreachable on both
  platforms. It was not "one macro line": `ibCtorControlType<T>::GetClassIcon` calls
  `T::GetIconGroup()` unconditionally, so registering without an icon does not compile. Fixed with
  `wrapsizer_res.cpp` (XPM + `GetIcon`/`GetIconGroup`, matching the four sibling sizers), the two
  declarations in `sizer.h`, and `CONTROL_TYPE_REGISTER(ibValueWrapSizer, "Wrapsizer", "Sizer")` —
  the 3-arg form, since a never-registered control has no legacy `CT_*` key to preserve.
- **ComboBox / Choice / ListBox are green-field on BOTH sides, not "a desktop control awaiting a
  port".** Their `.cpp` files are in the desktop `frontend.vcxproj` but carry no
  `CONTROL_TYPE_REGISTER` either, and the bodies are empty shells (`OnCreated` is a no-op). §6 below
  orders them first because they are cheap on the web; read that as writing them, not porting them.

Read the consequence plainly: **data entry on the web today is TextCtrl and CheckBox.** A
real document form — which needs at minimum a ComboBox/Choice for reference fields and a
TableBox for line items — does not assemble. Layout, commands and navigation are further
along than input.

> ~~Two comments in `wfrontend.vcxproj` describe the toolbar as a stub~~ — **fixed 2026-08-02**;
> the comment now states what is true: `toolBarItem.cpp` is in the project and `ibWebToolbar` /
> `ibWebToolBarItem` / `ibWebToolBarSeparator` are real classes, so `CT_TLBR` / `CT_TLITM` /
> `CT_TLSP` all register on the web.

### 4.3 Report — generate action ~~missing~~ LANDED 2026-08-02

`backend/metaCollection/partial/dataReportAction.cpp` now registers the **Generate**
standard command and routes it to the object module's `Generating(cancel)` handler — the
report's twin of a document's `Post` → `Posting`. The script still owns data and
presentation both; the platform owns only the command. See
[report-engine.md § 5](report-engine.md).

### 4.4 JSON provider — Read implemented 2026-08-02, deliberately unwired

`ibJsonProvider::Read` is now a complete recursive-descent parser (nested children,
arrays, base64 binaries, structural + synthetic keys; malformed input throws with a byte
offset). **Nothing calls it** — `ibBinaryProvider` remains the round-trip format — because
the *view* is lossy by design and three things cannot come back: Fields and Properties
flatten into one key set (a scalar property returns as a field; a Child value does return
to the property area), Date degrades to an ISO String, and `TypeDesc` is synthetic (parsed
and dropped). A name-form `NodeType` needs the new `SetTypeLookup` inverse to become a
clsid again. Closing the first two means a *separate lossless emitter*, not more parser.
Both halves — what survives and what does not — are pinned in `tests/test_jsonProvider.cpp`.

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

Reordered 2026-08-02: items 2 and 4 came off this list (done — see §2 and §4.3), which moves
web controls and accounting execution up.

1. **Web controls** — §4.2 is the widest gap between "the platform can" and "a user can".
   Desktop forms work; on the web the controls a real application form is made of are
   either stubs (tablebox) or **absent from the build entirely** (combobox / choice /
   listbox / notebook). Suggested order — ComboBox and Choice first (reference fields are
   in every form and are cheap: a select element over an existing fetch), then TableBox
   (expensive, and the one that unblocks documents), then the rest. Note what §4.2 now
   says about the first two: they are **green-field on both sides**, not a port — the
   desktop files are empty shells that register no clsid either.
2. **Accounting execution** — §4.1 is a whole advertised metatype that does not execute.
   Either finish the migration or state the register as out of scope. The register-totals
   primitives it was waiting on are now not only built but numerically verified (§2), so
   the reason to defer it has thinned.
3. Design-only arcs (§3) stay parked until a concrete need pulls them.

~~Form attribute binding → spec + tests~~ — **done 2026-08-02**: the surface is spec'd
(*Surface contract*) and the plumbing carries four gtest TUs. What is left is the live-form
half, which needs the GUI harness rather than a decision.

~~Report generate action~~ — **done 2026-08-02** (§4.3).

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
- **`dlgs/` (31 files, 27 of them `.cpp`/`.h`)** and **`theme/`** — undocumented; low leverage.
- **`ctrls/charts/`** — 246 vendored wxCharts files, deliberately not documented
  ([wx-fork.md § 3](wx-fork.md)).
- `backend/utils/`, `backend/diagnostics/` — small, no doc.

## 8. Lineage — what came from where

Foundation code has parents, and knowing them changes estimates. Recorded so a reader does
not mistake inherited shape for original design:

| Subsystem | Origin | State |
|---|---|---|
| **Form editor** | **wxFormBuilder** — also the source of the *property* idea | ~5% of the original remains; the attribution survives in 12 files — seven in `designer/…/visualEditor`, the three `frontend/visualView` hosts, and two `frontend/win/dlgs` dialogs ([form-editor.md § 1](form-editor.md)) |
| **Database layer** | **wxDatabaseLayer** (wxCode, last release 2009) | ~40% resemblance; architecture kept, drivers reworked. **Oracle / MSSQL exist upstream** ([database-layer.md § 1](database-layer.md)) |
| **UI kit** | wxWidgets fork (wxUniversal + Luna) | the fork is a teacher/substrate, not debt ([uikit.md](uikit.md)) |
| **Designer + debugger** | **written from scratch** | TCP replaced the era's DDE approach; the debuggee is the server ([debugger-architecture.md § 1-2](debugger-architecture.md)) |
| `ibNumber` | ttmath dependency **removed** | self-contained; ttmath credit retained ([../CLAUDE.md](../CLAUDE.md) §2a) |
