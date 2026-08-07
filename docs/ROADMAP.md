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

## 1b. Silent failures made loud — 2026-08-07 session

Every entry here was found by RUNNING the platform, and every one had the same shape: the code
reported success while doing nothing, so the symptom surfaced far from the cause. They are grouped
because the lesson is the grouping — none of them was a wrong computation.

| What was silent | What it cost | Now |
|---|---|---|
| `Update()` returning `affected >= 0` — a rewrite that matched no row reported success | A form said "saved", the object advanced its in-memory `DataVersion`, the row never changed — so the SECOND save accused another user of a change nobody made, and the edit had evaporated | A **key-only** rewrite that touches nothing raises and names the source; a `.Where(...)`-narrowed one stays quiet (an empty set is an answer) |
| AOT bytecode cache validated **only** its format version, while its own header described a four-part fingerprint it never checked | Half a day, twice, chasing a bug in whatever was being worked on rather than in the cache | Row is looked up by `(descriptor_id, config_md5)` — the configuration's digest. A save makes every earlier row unreachable; `Invalidate()` is hygiene, not correctness. [compiler-pipeline.md § 4a](compiler-pipeline.md) |
| `ibRegisterPlatformJobs()` stood ABOVE the `CREATE TABLE sys_job` it reads, and the server path never created the table at all | `enterprise.exe` could not open ANY base, including a fresh one, and closed without a window (`CreateFileAppDataEnv` **throws**, and only "returned false" was handled) | Both paths read `sys_lock → sys_job → migrate → declare`; bring-up failure is caught and shown with the infobase path |
| `ibMetaData::GenerateNewID` recomputed `max(metaID)+1`, so a deleted object's number returned to circulation — and the physical column name is `fld<metaID>` | A new attribute asked for a column name a dropped one still owned → `RDB$INDEX_15` violation, permanently | A monotonic counter, seeded once per load and reset on `Commit()`; same for control ids (`ibValueFrame::GenerateNewID`) |
| `DATABASE_LAYER_QUERY_RESULT_ERROR == 0` read as failure — it is an affected-row COUNT | Pure-DDL restructuring changes 0 rows, so a successful apply declared itself failed and skipped re-reading the baseline; from then on the saved configuration no longer described the base it is diffed against | The three comparisons removed; errors arrive as exceptions, which is where they have arrived for a long time |
| A stored count in a blob was trusted (`ibSourceDescriptionMemory` and two siblings) | Reads past the end of the buffer — an assert in Debug, somebody else's memory presented as a clsid in Release | Bounds checked before each read; a bad blob yields an EMPTY description, not invented data |
| `sys_session.exclusive` left NULL by the INSERT and 0 by the release path | Two spellings of "not exclusive" in one table — harmless today only because the driver reports NULL as 0 | `NOT NULL DEFAULT 0` in the DDL and written explicitly when the row is born |
| A hierarchy root compared the parent only to the zero sentinel | `NULL = <blob>` is UNKNOWN, so rows whose column was added by a restructuring fell out of the ROOT level: visible flat, present nowhere in the tree | `IS NULL OR = sentinel`. A third spelling (old 20-byte layout) is named at the call site and belongs to migration, not to the predicate |
| Deleting a folder deleted one row | Children kept pointing at a guid that no longer exists | Delete clears the reference to itself in its children, in the same transaction |
| The GUI CI step had no timeout while its siblings run `ctest --timeout 300` | A hang after every test passed ate the whole 90-minute job twice, and the red said "timed out" — which reads as "too slow to compile" and was not what happened | Bounded at 10 minutes (the honest runtime is 3 seconds); crossing it dumps every thread's stack via `gdb` and fails the job |

⚠ The wxWidgets cleanup hang itself (`wxEntryCleanup` after a green suite, Linux/GTK only — the same
binary exits cleanly on Windows) is **not fixed**; the CI change makes it report instead of consuming
the job. Its diagnosis, and the two theories that died on inspection, are in
[portability.md § 4](portability.md).

### 1b-bis. The same day, the other direction — 2026-08-07 (late)

The entries above all read "it was quiet where it should have spoken". The two below are the
mirror, and they belong beside them because the lesson is that the pendulum swings both ways: a
mechanism that cannot TELL two situations apart will get one of them wrong whichever behaviour it
picks.

| What went wrong | What it cost | Now |
|---|---|---|
| `ibJobManager::WriteSharedSettings` answered `false` both for "there is no connection to write through" and for "the base looked at this row and said no". The entry above made the seed RAISE on that `false` — correct for the second case, fatal for the first | **Nine tests red on all three platforms at once** (368–376, `JobManager`), because a unit-test process has no database and every successful registration threw. The same code path is what a headless host runs before its base is open | The write answers `Written` / `NoBase` / `Refused` — L2 already knew the difference (`ibBackendQueryException::Kind::NoConnection`) and it was dying in a `catch (...)`. The seed raises only on `Refused`; a person's own `job.Write()` raises on anything that is not `Written`. Changing the return type is what forced every one of the three callers to state its own rule |
| `Register` seeded a `sys_job` row for a job with **no key** | The write had nothing to be about, answered "refused", and the raise above turned that into a failed bring-up | The read and the seed sit under `KeyOf(desc).isValid()` — the register is keyed by guid and a name is only a caption. Every job the platform declares carries one, so nothing real is skipped |
| The GUI watchdog found its victim with `pgrep -f '[o]es_frontend_runtime_test' \| head -n1` | **Two bounded runs, ~35 min each, produced a one-frame stack of `xvfb-run`** — `-f` matches the whole command line, which for the wrapper CONTAINS the binary's name, and `head -n1` takes the lower pid, which is always the parent. It killed the wrapper too, orphaning the process that actually hung | The wrapper is gone rather than the match tightened: the step starts `Xvfb` itself, so `$!` IS the process under test. It also waits on the display socket and fails if the suite skipped instead of running — a green job that asserted nothing is this job's own silent failure |

---

## 1a. Unblocked and verified — 2026-08-02 session

Everything in this section was **built and run**, not reasoned about. Solution `Debug|x86`
compiles with 0 errors; `oes_tests` **855/855**; `oes_frontend_runtime_test` **26/26 in ~2 s**.

> Numbers below are that session's readings and are kept as such. Where the suite stands now:
> **1166** in each of the three general CI jobs and **38** in the GUI one (2026-08-07), 1208
> locally with nothing excluded. See [portability.md § 2](portability.md).

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
| **Job manager** | [job-manager.md](job-manager.md) | scheduled + background work through the worker pool. Landed: per-session pool choice, `ibJobManager` on appData, `RunScheduledJobs` / `RunJob` + the file-base timer, `totals.fold` as first tenant, cross-process claim via `sys_lock`, job state/outcome, and the `ibValue::IsTransferable` gate. Background jobs are live: `RunBackground("Module.Method", args)` returns a vended `BackgroundJob` value (`IsComplete` / `Wait` / `Result` / `Error` / `Activity` / `Cancel`), runs on its own session under the caller's identity, and shows in Active Users as its own session kind (BackgroundJob / ScheduledJob / SystemJob). `ibFirebirdMaintenanceScheduler` folded in — it lost its thread and is now the `firebird.maintenance` job. Full schedules (`ibJobSchedule`: interval + time window + weekdays + month days + months + validity range, with `ToString` / `NextAllowedAfter`), the manager's own tick thread, and cross-process synchronisation (`sys_lock` claim owned by the job's session + shared `sys_job.lastRun` through L2 upsert). Open: background fetch for reports; naming a *different* user than the caller (needs a role gate). **2026-08-07:** the settings write answers `Written` / `NoBase` / `Refused` instead of a `bool` — declaring a job with no database open is not a refusal, and the seed only raises on one of the three ([job-manager.md](job-manager.md), § *A write answers with THREE outcomes*). Registration is gated on the job having a key, since the register is keyed by guid |
| **Scheduled jobs (metadata)** | [scheduled-jobs.md](scheduled-jobs.md) | BOTH metatypes live (2026-08-04): `PredefinedJob` (flat, under Common) and `ScheduledJob` (parameterized — hierarchical reference object, rows, folders, card). Registration is **per row**; `sys_job` is the **register of every job there is**, keyed by guid (name is a caption); `LastRun` / `NextRun` are generated on read; `Execute` is a background run that ignores both calendar and switch; the row owns its switch and schedule, the register owns them for kinds without a card. Cross-machine safety on a file base is the per-row claim `Job.<rowGuid>` + the shared clock. Open (with entry points, § 13 of the arc): the job list window, the schedule editor in the web frontend, dimensions + uniqueness index, "run as" and retry per row |
| **Metadata test coverage** | — | ⚠️ NEXT STEP, and the arc of 2026-08-06 is the argument for it. Every defect that day was a metatype missing from a list somebody kept by hand — the branch that could not create, the tree that did not fill, the runtime list the form is built from, a `ResolveChild` disagreeing with its own attribute lists. Shape: ONE TABLE of metatypes (11 business + `SessionParameter` / `CommonAttribute` / `PredefinedJob` / `ScheduledJob` / `SubcontoKindsTable` / `ConstantValueColumn`) × five questions asked of each — creates under the root and enumerates, survives a save/load round trip, `ResolveChild` agrees with the attribute lists, builds a schema snapshot, appears in the designer tree. A new metatype is then covered by adding a row, not a file. Started: `tests/test_commonAttribute.cpp` (10 cases over the composition mechanism, **not yet run** — the CMake test build needs the new sources). Harder and more valuable: a live-DB fixture for restructuring (as `test_totalsNumericParity` does on in-memory SQLite) — apply, fail mid-way, assert the schema came back |
| **Common attributes** | [common-attributes.md](common-attributes.md) | LANDED 2026-08-06. One declaration under Common, a real attribute inside every object checked into its composition — own metaID, own column, own restructuring. Membership is its own mechanism (`ibCompositionObject`), not the section one; the copy delegates its type and refuses to be edited or removed where it sits. Who may carry one is asked (`IsCompositionAllowed`), not listed — today the catalog/document line, registers deliberately excluded. Open: **data separation** (the reason this was built — automatic query condition, numbering key, index order), orphan copies after cross-configuration paste, no tests |
| **Session parameters** | [session-parameters.md](session-parameters.md) | LANDED 2026-08-05, verified by hand. The metatype (an attribute whose owner is the session), the manager + unit in the global context, the session module (`SetSessionParameters`, a second module property on the root) and the write window on `ibSession` — a write outside that module raises, which is what makes a policy written against one safe. Runs inside `CompileRoot` for **every** session kind, jobs included, before the policy is built. Open: no automated test of its own; no designer list window beyond the tree branch |
| **Register totals** | [register-totals-strategy.md](register-totals-strategy.md) | reading works (three virtual tables via live aggregation); the trigger-maintained bundle is declared by the accumulation register and applied on a live Firebird, surviving a kind switch (2026-07-29). Numeric parity of the two paths is now **verified GREEN under a live engine** (2026-08-02): `tests/test_totalsNumericParity.cpp` installs the real rendered bundle on in-memory SQLite and compares maintained totals against re-aggregated movements — accumulation, month truncation, updates across both key columns, deletes, backdated entries, fractional values, mixed traffic. It paid for itself on the first run by exposing **two bugs in `ibMaterializeSql::Apply` that made a first apply on a clean SQLite database impossible** (a driver signals failure by throwing, so the return-code test never saw it; and the "error" sentinel is 0, which is also the affected-row count of successful DDL) — see [register-totals-strategy.md](register-totals-strategy.md) §1. Not retired: real traffic volume and Firebird's own trigger family. Open: the accounting register declares no totals; boundary-row Fill and the routing of recorder / sub-day readings to the movements are absent |

---

| **Query constructor (B2 / B4)** | [query-constructor.md](query-constructor.md) | **BUILT AND GREEN** (last verified run 2026-08-07: Debug|x86 clean, **1102 passed / 0 failed / 5 skipped** of 1107; seven passes, each driven by Max running the window). Nine tabs over the AST — Tables and fields / Links (with a join DIAGRAM: drag a field onto a field) / Grouping / Conditions / Advanced / Unions / Order / Totals / Query package — with move columns between panes, splitters, 16×16 tool bands, and each tab showing the available fields on its left. The tab set follows the statement's KIND (a drop has no query tabs; Links appears from the second table). Both hosts live: the dynamic list's *Arbitrary query* tab, and the code editor's context menu — available ANYWHERE, editing the literal the caret is in or INSERTING a new one (multi-line `|` spelling handled). **The engine is the only judge** — render → `ibQueryParser::ParsePackage`, its words verbatim on a live status line; the small editors COMPOSE TEXT and parse it, and every keyword comes from the keyword table so a localized table cannot break them. Language added: `ALLOWED`, `FOR UPDATE`, `INTO <name>`, `DROP <name>`, the `;`-package, executed by `ibQueryLowering::ExecutePackage`. **`TempTablesManager`** (`New TempTablesManager()`, one `Close()`, `Query.TempTablesManager = …`) decides how long a temp table lives — without one it dies with its query. Read-only is ASKED of the metadata; the config is handed in, never looked up. **Passes 2–4 (§7 / §7a / §7b of the doc) are IN**: every list is an `ibDataViewCtrl` grid edited in place; ONE arbitrary-expression editor with a field tree and a language palette; the Links grid; `INDEX BY` (and the temp store really builds the lookup); the union field map and the totals grid; batch statements and union branches as side tabs; SQL-lexed `wxStyledTextCtrl` for every pane that shows query text. The ENGINE now answers every question the window used to: `ibQueryLexer::IsIdentifier` (is this a legal name), `ibQueryLowering::CheckNames` (do the names resolve, and are the output names unique), `PruneUnresolved` (drop what a removed table broke — by RE-RESOLVING, never by chasing the deletion), and `ibQueryEnsureUniqueName` / `ibQueryUniqueSourceAlias` (what a new column or table alias is called — moved out of the dialog so every host names things identically). Language grew with it: optional `FROM` (`SELECT 1` runs), `TOTALS … AS <name>`, `ExecuteBatch` with a result per statement position. The window is now four files by meaning (what is there / what it shows / what it does / what it is). **Passes 5-6 (§7c / §7d) are IN too**: the dynamic list is the MAIN TABLE with the query living OVER it (both serialised, always; ticking the box seeds a query over the table); the composer stopped refusing settings over an author query and wraps it as a nested source (which rule 2 of the optimizer folds straight back into ONE server-side SELECT); `DescribeOutput` answers "what columns does this produce" without running; every output column is a real COLUMN with its own id and TYPE, which is what lets a query field be put on a FORM and a temp table inherit a reference; `ibQueryLowering::AggregatesFor` is one door read as a REFUSAL by `CheckNames` and as WHAT TO OFFER by the constructor, so a string field is never shown `SUM`; `UngroupedProjections` likewise (the check says which field is wrong, the constructor adds exactly those). New in the language: **`CAST(Recorder AS Document.Order).Number`** — a NARROWING that makes a composite reference walkable (conversion to a primitive is refused with a message saying why), and **`FROM &Table`** — a value table handed in, readable only `INTO` a temporary table. New windows: the **CASE WHEN builder** (the one construction that is an ordered LIST) and a combo cell that offers ready calls, takes typing, and opens the editor. **Pass 7 (2026-08-07) is IN**: `TOTALS … BY OVERALL` — the level above every dimension, and a CONNECT rather than a build (the fold always rolled the whole snapshot into the tree's root; the walk started at the root's children, and the window's box sat ticked-and-disabled explaining that the grand total "is always produced" — true of the fold, false of the result). `BY OVERALL` alone is a whole totals query, which relaxed two places that assumed a dimension. **A condition over a folded value is now a HAVING wherever it was written** — a rewrite rule, per AND-term (an `OR` across the two stays whole), because the Conditions tab offers aggregate fields beside plain ones and a condition over `SUM(x)` was reaching the ROW filter; the lowering emits one `Having()` per term instead of reading the whole expression as one comparison. The grouping rule stopped asking what a projection **is** and reads its TREE (`CollectFoldedAndFree`): a column under an aggregate is folded, one outside every aggregate must be a key — which closed a hole that PRE-DATED the arc (the free `Price` in `SUM(Qty) / Price` was held to no rule at all), and `AggregatedColumns` is the mirror of `UngroupedProjections`. **`COUNT(DISTINCT x)` LANDED the same day** — first written down as a separate arc ("the L2 IR has no DISTINCT modifier"), which was a true fact and a wrong estimate: the L2 renderer emits a function as `name(args…)`, so the modifier is one field on `ibQueryExpr` and one line there, and `DISTINCT` inside an aggregate is spelled identically by every dialect this layer writes for. Keyword, parser (read before the argument, where SQL puts it), renderer, AST flag, the L3 aggregate item, a defaulted `distinct` on the three `Aggregate` verbs, four projection sites, and the RAM fold — which counts by `ibValue::GetHashKey()`, the identity, never the display string. `COUNT(DISTINCT *)` is refused with a sentence, not a syntax error. The rule moves a condition WHEN IT IS WRITTEN, not when the query runs (`ibQueryMoveAggregateConditionsToHaving` is public and the Conditions model calls it in its one write door — a window showing a `WHERE` while the engine ran a `HAVING` teaches people to distrust it), so the tab reads BOTH clauses; `HAVING` became its own clause rather than a tail of `GROUP BY` (with no `GROUP BY` the whole result is one group — the same statement `BY OVERALL` makes in the other clause, and the renderer had always written it independently: the THIRD time in two days that the write side was complete and the read side lagged); and `=` / `<>` joined the four ordered operators, which were the lowering's switch and never the mechanism's. **What the first run in three arcs found** (the suite had not been executed since 1071/0/5): four defects, THREE of them in code written during those arcs and never executed — `Document.Order` did not parse (after a `.` the reader demanded an Ident, and `Order`/`Group`/`Index`/`Value`/`Count` are all reserved; position decides, so a keyword there IS a name), `SUM(x) / COUNT(y)` did not parse (`ParseProjection` branched on the first token and read only the call — the branch was never needed, `ParsePrimary` already routes an aggregate), and `CAST(x AS T).Field` never worked (the dot was recognised and not consumed). All three live in paths **the window cannot reach by clicking**, which is the evidence for the property test (`parse(render(x)) == x` over generated ASTs) rather than more hand-written cases. **The property test LANDED the same day** (`tests/test_queryRoundTrip.cpp`) — a seeded AST generator over `render(parse(render(x))) == render(x)`, and it found two more on its first run: a STRING LITERAL COULD NOT END A LINE (the shared lexer ran its continued-string branch on a CLOSED string, clobbering the read position back inside the literal — it needed a string to be the last token on a line, which script text almost never does and the RENDERER does on every clause), and `CAST(x AS T).Order` still failed because the walk after a cast read its path from a fresh position and met the first-segment rule instead of the after-a-dot one. Its companion test asserts the generator really PRODUCES the awkward shapes, and caught the generator itself: `BY OVERALL` had never been generated once in 400 seeds (an LCG's low bits, read by `% 2`), while the property passed on all of them — a generator that quietly produces less than it claims makes a green run mean less than it looks. **Next:** the `Query builder` tab; the frontend beyond the CASE builder is still verified only by running it

### Already built — do not re-plan these

Written down because each was proposed again after it existed. Check here before scheduling
work on the query tiers.

| Thought to be missing | Actually in the tree |
|---|---|
| **AST → query text** (the "first stone" of a query constructor: read an existing query, show it, take an edit, write it back) | `query/queryRender.{h,cpp}` — complete over the AST: every `ibQuerySelect` field and all 15 expression kinds, subqueries, `TOTALS … BY [OVERALL,] … HIERARCHY`, `CAST`, `UNION`. Keywords come from the active table, nothing invented or dropped. 36 round-trip cases in `tests/test_queryL4Parser.cpp`; `composition/listFilter.cpp` uses it today. **Verified complete 2026-08-06** — the remaining constructor work is the SHELL (tabs, preview, apply), not the round trip |
| Row-level filtering that cannot be written around | `Restrict` policy on the Role, applied at the L3 door as a decorator ([access-policy-rls.md](access-policy-rls.md)). With a common attribute + a session parameter this IS data separation — a separate "separator" metatype would duplicate it |
| **A way to keep old behaviour for existing users** — "we will need a compatibility mode / an upgrade path once there are real installations" | The configuration already DECLARES its version: category `Compatibility`, property `Version`, enum `ibValueEnumVersion` over the `ibProgramVersion` ladder, read through `ibMetaData::GetVersion()` and serialised with the metadata ([compatibility-version.md](compatibility-version.md)). ⚠ Groundwork: **nothing branches on it yet** (verified 2026-08-07), so the first branch sets the pattern — gate the SCHEMA contribution and the behaviour in the same change |
| **A migration engine** — a per-release upgrade script that brings an existing base to the new shape | `DiffSnapshots` + `ibStructureBuilder` already are one. The compatibility mode is a metadata property, so raising it re-runs the ordinary apply: the target snapshot is rebuilt through the newer branches and the diff IS the migration. A second per-release mechanism would describe the same difference twice and drift from it |

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
| Web client protocol (batching, frames, acknowledged counters) | [web/client-protocol.md](web/client-protocol.md) | **DESIGN — no code yet** (2026-08-05). The transport half of §6.1's web work: today one HTTP request per user action, each answered with the whole form tree, plus a 2-second poll — on a mobile link the cost is round trips, not bytes. The shape it proposes reuses what is already in the tree (the `Dispatch` door, `RunOnWorker`, `GET /stream`, `netFetch` as the single client chokepoint) and adds a loop and a counter over them. The counter is the exchange plan's, deliberately: sent / received numbers with retransmit until acknowledged. Asymmetric on purpose — commands are reliable and ordered, frames are latest-wins. The diff (sending changes rather than the whole tree) is explicitly deferred until real forms show what changes. |
| Data protection (selective field encryption) | [data-protection-arc.md](data-protection-arc.md) | **DESIGN — no code yet** (2026-08-04). The cipher is ours, on the value-crossing floor (`ibColumnCodec` **and** the dump codec in `dataMover.h` — instrument one and the other leaks), deliberately NOT DBMS-side TDE: one implementation instead of five, and the key never enters the database. L3 tags the column node, L2 executes, nothing above knows. **Phase 1 stands alone** — the cipher floor plus `sys_config`, one primitive and two call sites, breaks nothing, and already yields "without the key a dump carries no name of any table, attribute or object", because physical names are numeric already. Everything past it (blind index, levels, re-encryption pass) is paid for in functionality and conversion time. |

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
