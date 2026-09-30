# OES correctness and performance run — 2026-09-30

## Outcome

- Branch `perf-night-2026-09-30` is based on fetched `origin/develop` commit `2df09c6e`.
- Two correctness defects were reproduced, fixed, covered by tests, and committed separately.
- Twenty query-language combinations were checked against real configuration metadata. Eighteen validated; the window-aggregate case exposed and drove one fix, while undefined-value comparison remains unresolved.
- Release runtime microbenchmarks and LINQ scaling benchmarks were recorded. The checked in-memory pipelines remain approximately linear through 16,000 rows.
- Release `designer` and `enterprise` targets build. The full test executable has one pre-existing, unrelated date/time failure; 2,281 tests pass and 12 are skipped.

Host: macOS 15.6, arm64, AppleClang 17, CMake Release. The configuration used for MCP discovery was the local file database `oes-ds-test-conf` with installed Designer build 3180. The MCP bearer token remains outside the worktree.

## Fix 1: debugger listener shutdown race on macOS

### Symptom and reproduction

Running the application through `app_run {debug:true}` on the unmodified installed develop build crashed 3 out of 3 times. The generated reports were:

- `enterprise-2026-09-30-184727.ips`
- `enterprise-2026-09-30-184925.ips`
- `enterprise-2026-09-30-192258.ips`

The clearest stack was `CFRunLoopRemoveSource -> wxSocketBase::Close -> wxSocketBase::Destroy -> ibDebuggerServer::ShutdownServer`. Another run faulted in the connection worker in the same CoreFoundation operation while the main thread was joining it.

### Cause and correction

`ShutdownServer` destroyed `m_socketServer` on the main thread while `EntryClient` could still be blocked on that wx socket in its worker. On macOS, both paths can remove the same CFRunLoop source. The listener destruction had been added to wake the worker's blocking `Accept(true)`.

The worker now polls with the existing 50 ms bounded `WaitForAccept` path and performs a non-blocking `Accept(false)`. Shutdown only requests thread deletion and joins it; the connection owner releases the listener after the worker exits. See `src/engine/backend/debugger/debugServer.cpp:231` and `:1041`.

### Verification

- Before: 3/3 application debug launches crashed.
- After: 100/100 real wx listener create/wait/shutdown cycles passed; total test time 5.39 s, about 54 ms per cycle, and every cycle remained under its 1 s bound.
- Regression: `SocketLockFix.ServerShutdown_WhileWaitingForClient_IsBounded` at `tests/test_socketLock.cpp:214`.
- Commit: `2474492a Fix debugger listener shutdown race on macOS`.

The locally built uninstalled `.app` exits with status 1 before opening its MCP endpoint and does not produce a crash report. Therefore the post-fix claim above is deliberately limited to the real-socket regression rather than claiming a packaged-app end-to-end run. Both GUI targets do link successfully.

## Fix 2: window aggregate incorrectly turns SELECT into a group

### Symptom and reproduction

The valid query below was rejected during name/group validation with `'Склад' is neither grouped nor aggregated`:

```sql
SELECT P.Склад,
       P.Period,
       SUM(P.Стоимость) OVER (
           PARTITION BY P.Склад
           ORDER BY P.Period ROWS) AS RunningAmount
FROM AccumulationRegister.ПартииТоваров AS P
```

### Cause and correction

`CollectFoldedAndFree` treated every aggregate function as folding the whole SELECT, including an aggregate carrying an `OVER` clause. A window aggregate preserves one result per input row, so it must not activate ordinary `GROUP BY` validation. Window partition and order expressions were also outside the normal operand traversal.

Window aggregates no longer set the group-fold flag. Their arguments, partition keys, and order keys remain free with respect to an ordinary `GROUP BY` beside the window, preserving the rejection of genuinely invalid mixed queries. See `src/engine/backend/query/queryLowering.cpp:5688`.

### Verification

- Before: valid window-only query rejected during `query_check`.
- After: `QueryGrouping.AWindowAggregateDoesNotTurnTheSelectIntoAGroup` passes.
- Negative control: `QueryGrouping.AWindowArgumentStillObeysAnOrdinaryGroupBesideIt` still rejects an ungrouped window argument.
- Commit: `349606e2 Keep window aggregates out of group validation`.

## Query-language matrix

The matrix was checked through `query_check` against the real configuration schema. `Correct` means parse, metadata resolution, and validation succeeded; it is not presented as an execution/result-equivalence measurement.

| Scenario | Result | Notes |
| --- | --- | --- |
| `TOP + DISTINCT + ORDER BY` | Correct | Grammar order is `TOP ... DISTINCT` |
| `WHERE + BETWEEN + LIKE` | Correct |  |
| `GROUP BY + HAVING + CASE` | Correct |  |
| `LEFT JOIN + IS NULL` | Correct |  |
| `INNER JOIN + IN (subquery)` | Correct |  |
| `UNION ALL` | Correct |  |
| accumulation register `Balance()` | Correct |  |
| accumulation register `Turnovers() + GROUP BY` | Correct |  |
| accounting `BalanceAndTurnovers() + TOTALS + HIERARCHY` | Correct | Module query; composer intentionally refuses this form |
| temporary-table package + `TOTALS` | Correct | Module query; composer intentionally refuses packages |
| `RIGHT JOIN` | Correct |  |
| `FULL OUTER JOIN` | Correct |  |
| `REFS` type test | Correct |  |
| `CAST(Recorder AS Document...).Number` | Correct | Compound reference selection |
| window `SUM + PARTITION + ORDER + ROWS` | Defect fixed | Previously false grouping error |
| `TOTALS ... BY Period PERIODS(Month)` | Correct |  |
| `= UNDEFINED` | Error | `UNDEFINED` is parsed as a source attribute and fails name resolution; `IS UNDEFINED` is also rejected by the grammar |
| `YEAR + MONTH + GROUP BY` | Correct |  |
| `SELECT ALLOWED` | Correct |  |
| `FOR UPDATE` | Correct |  |

The resolver also correctly rejected a nonexistent `Catalog.Товары` and an unset virtual-table parameter instead of silently returning an empty result.

## Release performance baseline

Each row is the minimum measured by the benchmark's repeated runs (normally 5). No performance-sensitive production code was changed, so these are baselines rather than invented before/after deltas.

| Scenario | Result | Native/control | Ratio or scaling |
| --- | ---: | ---: | ---: |
| string append | 23.1 ns/append | 1.8 ns | 12.6x |
| LINQ build + pipe | 167.7 ns/element | — | — |
| LINQ pipe, two lambdas | 82.9 ns/element | — | — |
| LINQ pipe, one lambda | 41.4 ns/element | — | — |
| LINQ join, 250 rows | 404.0 ns/row | — | — |
| LINQ join, 1,000 rows | 410.4 ns/row | — | 1.02x per-row vs 250 |
| LINQ join, 4,000 rows | 438.6 ns/row | — | 1.09x per-row vs 250 |
| LINQ join, 16,000 rows | 463.7 ns/row | — | 1.15x per-row vs 250 |
| array indexed read | 62.9 ns/read | — | — |
| method resolution/call | 75.3 ns/call | — | — |
| `ibNumber` add | 2.7 ns | 0.7 ns | 3.8x |
| `ibNumber` multiply | 3.2 ns | 0.7 ns | 4.5x |
| `ibNumber` compare | 0.8 ns | 1.0 ns | 0.8x |
| `ibNumber` non-exact division | 97.4 ns | 0.7 ns | 134.9x |
| `ibNumber` exact division | 3.5 ns | 0.7 ns | 4.8x |
| `ibNumber` 30x30-digit multiply | 85.8 ns | — | — |
| `ibNumber` 200-fraction-digit multiply | 81.7 ns | — | — |
| `ibNumber::ToString` | 112.8 ns | 114.3 ns | 1.0x |
| `ibNumber::FromString` | 44.6 ns | 18.1 ns | 2.5x |

Additional scale probes:

| Pipeline | Small | Large | Interpretation |
| --- | ---: | ---: | --- |
| block select | 164.1 ns/row at 1k | 175.5 ns/row at 16k | approximately linear |
| join, fixed 2k inner | 529.0 ns/outer row at 1k | 143.5 ns/outer row at 16k | fixed build cost amortizes |
| group by | 334.1 ns/row at 1k | 394.5 ns/row at 16k | approximately linear |
| order by, one key | 385.0 ns/row at 1k | 445.1 ns/row at 16k | expected `n log n` growth |
| order by, two keys | 490.6 ns/row at 1k | 597.3 ns/row at 16k | expected `n log n` growth |
| projection width | 274.0 ns/row, one field | 447.9 ns/row, three fields | +63.5% for two fields |

The requested 10k/100k database execution table, independent result oracle, SQL pushdown journal, and user-path report timings could not be produced reliably after the installed debug runtime repeatedly crashed and the uninstalled local app did not initialize. Parser success is not substituted for those measurements.

## Builds and tests

- Clean Release configure: `BUILD_TESTING=ON`, Firebird and PostgreSQL enabled for test linkage.
- `oes_tests`, `designer`, and `enterprise` targets build successfully.
- Targeted regression: 3/3 passed.
- Related query/parser/socket selection: 19/19 passed.
- Full Release suite: 2,294 tests; 2,281 passed, 12 skipped, 1 failed.

The sole failure is pre-existing and unrelated: `DateTime.TheBridgeCarriesAReadingByItsParts` expects hour 12 and receives 13 for `1969-12-31 12:00`. It reproduces alone and appears dependent on local historical timezone/DST conversion. The skips are driver/timezone environment skips.

A configure with both database drivers disabled also exposes an existing CMake issue: `oes_tests` still includes Firebird/PostgreSQL tests while excluding their implementations, so it fails to link.

## Found but not fixed

1. Historical date/time bridge test differs by one hour on this host. Reproduction: `oes_tests --gtest_filter=DateTime.TheBridgeCarriesAReadingByItsParts`.
2. `oes_tests` cannot link when both database drivers are configured off because driver tests remain in the target.
3. The uninstalled Release `.app` bundles link but exit with status 1 before exposing MCP; no diagnostic report is generated. Installed/package testing is required for a true post-fix application launch check.
4. Non-exact `ibNumber` division is the largest measured primitive hotspot at about 135x its native control. It was measured, not changed, because neither correctness nor a localized regression was established in this run.
5. Undefined-value comparison has no validated query-language spelling in the tested build. `P.Recorder = UNDEFINED` fails names with `unknown attribute 'UNDEFINED' on source 'P'`; `IS UNDEFINED` fails parsing. `IS NULL` works for SQL null semantics but is not assumed to be equivalent.
