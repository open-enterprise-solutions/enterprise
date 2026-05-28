# Archive — collapsed session handoffs

These session handoffs (2026-04-30 .. 2026-05-12) have been collapsed
into topic docs. Index here for the date-curious; if you need the raw
day-by-day chronology, it's in git history before the trim.

| Date | Arc | Canonical doc |
|---|---|---|
| 2026-04-30 | Bytecode self-sufficiency + FB driver hardening + DDL exclusive gate + ibNumber landing | `eval-scope-refactor.md`, `firebird-driver-hardening.md`, `runtime-facade.md` |
| 2026-05-02 | Eval / scope refactor — bc.m_listVar vector + kind enum, ibCompileEval, ExportNamesToHelper | `eval-scope-refactor.md` |
| 2026-05-05 | Paging architecture kickoff — universal `Get*Fetch`, control-driven prefetch, lying scrollbar | `paging-design.md` |
| 2026-05-06 | Paging — drill RecalculateDisplay, Tree+selection chainHead cursor, TabularSection RAM e2e | `paging-design.md` |
| 2026-05-07 | FB BINARY(20) parent-ref bind + hierarchy scroll fix | `paging-design.md`, `firebird-driver-hardening.md` |
| 2026-05-08 | gtest scaffold + datavgen.cpp split | `BUILD.md` (test target) |
| 2026-05-09 | Paging regression sweep + cell-mode polish + lambda kickoff | `paging-design.md`, `lambda.md` |
| 2026-05-10 | CES default + iterator refactor + shutdown deadlocks + lambda-eval guards | `lambda.md`, `linq.md` |
| 2026-05-11 | LINQ JOIN / GROUP BY (terminal), closure capture Phase A+B, AOT cache iterations | `linq.md`, `closure-capture.md` |
| 2026-05-12 | Closure Phase D/F (chain LINQ unlocked), 31-op chain surface, typed-tag specialise, `group ... into g` continuation | `linq.md`, `closure-capture.md` |

Active session-handoff: `session-2026-05-25.md` (audit-log subsystem
+ adjacent fixes — still recent, not yet migrated). Collapse into
topic docs when audit-log arc is stable.

Decisions to remember (cross-cutting):
- **Persistence layer** for AOT (`SerializeAOT`/`DeserializeAOT`, `byteCodeAOT.cpp`) landed
  2026-05-02; format version drifts as opcodes shift — currently v10 after `OPER_CALL_LINQ`.
  Pending DB schema + Attach batch-load — see `project_bytecode_aot_cache.md`.
- **Resolver work** (kind-driven bytecode, no compile-context dep at runtime) — landed 2026-05-02.
  See `eval-scope-refactor.md` "Bytecode resolver" §.
- **Per-driver NoWait** (PG/MySQL/MSSQL) + `HoldRowLocks`/`TryProbeRowLock` virtuals — concrete
  impl only for FB; others state-only. See `session-registry.md` + `firebird-driver-hardening.md`.
- **Module rename** `GetModuleObject/Manager → GetObjectModule/ManagerModule` — landed
  2026-05-04 (collision with runtime `ibValueModuleManager`).
- **ses_query macro** — session-bound DB; `db_query` for DDL/service only. Landed 2026-05-04.
- **`commonObjectQuery.cpp` split** into 4 files by responsibility — 2026-05-04.
- **`/admin/diag` HTTP endpoint** + per-driver NoWait + sys_session schema (pid/address/
  currentActivity) — 2026-05-04.
