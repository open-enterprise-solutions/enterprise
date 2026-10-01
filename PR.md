# Fix debugger shutdown race and window aggregate validation

## Summary

- Checked 20 query-language combinations against fresh current-build metadata: all 20 validate, and one drove a fix. Recorded Release runtime/LINQ and database-backed baselines.
- Found two correctness defects: a reproducible macOS debugger shutdown crash and false grouping rejection for window aggregates.
- Fixed both defects in the engine and added focused positive and negative regression coverage.
- Release test, Designer, and Enterprise targets build; targeted and related suites pass.
- Full suite result is 2,281 passed, 12 skipped, and one pre-existing timezone-sensitive failure.
- Built a fresh integration configuration and extended it to 1,000,000 stock movements plus a real payroll calculation register for 50,000 employees; independent query, balance, and payroll totals agree.

## Measurements

| Scenario | Small | Large | Growth | Before fix | After fix |
| --- | ---: | ---: | ---: | ---: | ---: |
| debugger create/wait/shutdown | — | 100 rounds | — | 3/3 debug launches crashed | 100/100 listener cycles pass; ~54 ms/cycle |
| window aggregate validation | 1 query | — | — | rejected as ungrouped | accepted; mixed invalid control still rejected |
| LINQ join | 404.0 ns/row at 250 | 463.7 ns/row at 16k | 1.15x per-row | unchanged | unchanged |
| block select | 164.1 ns/row at 1k | 175.5 ns/row at 16k | 1.07x per-row | unchanged | unchanged |
| group by | 334.1 ns/row at 1k | 394.5 ns/row at 16k | 1.18x per-row | unchanged | unchanged |
| order by, one key | 385.0 ns/row at 1k | 445.1 ns/row at 16k | 1.16x per-row | unchanged | unchanged |
| DB aggregate | 14.49 ms at 10k | 64.25 ms at 100k | 4.43x | — | current build |
| DB group by product | 139.10 ms at 10k | 905.95 ms at 100k | 6.51x | — | current build |
| DB join + group | 33.25 ms at 10k | 261.02 ms at 100k | 7.85x | — | current build |
| DB window + order | 267.39 ms at 10k | 3,740.60 ms at 100k | 13.99x | rejected by validator | executes correctly |
| DB aggregate, 1m | — | 172.70 / 192.36 ms min/median | — | — | exact oracle match |
| DB group by product, 1m | — | 2.70 / 2.91 s min/median | — | — | exact oracle match |
| DB join + group, 1m | — | 0.80 / 0.86 s min/median | — | — | exact oracle match |
| DB window + order, 1m | — | 10.33 / 30.75 s min/median | — | — | temp-sort/spill hotspot |

The database timings are direct Release MCP round trips after warm-up (five measured samples; methodology and worst values are in `perf-night-report.md`). The current-source Designer and Enterprise also completed an attached-debugger launch and a real stock `SpreadsheetDocument` report composition without the former shutdown crash.

The payroll contour contains 50,000 employees and 63,000 calculation rows (50,000 salary, 10,000 bonus, 3,000 leave/storno), totaling 281,750,000. Replacing per-employee `FindByCode` calls (49.967 s for 1,000 employees) with one set selection reduced normal 10,000-employee batches to roughly 2.0-6.1 s. A full 63,000-row payroll cross-table composition exceeded the MCP request timeout, so the PR does not claim an end-to-end report time.

## Fixes

### Debugger listener shutdown race

Symptom: `app_run {debug:true}` crashed 3/3 times on macOS in `CFRunLoopRemoveSource` while stopping the debugger server.

Cause: `ShutdownServer` destroyed a wx listener from the main thread while `EntryClient` could still be using it in the worker. The destruction existed to interrupt `Accept(true)`.

Change: use bounded `WaitForAccept` plus `Accept(false)` in the worker and let shutdown join the worker before the connection owner destroys the listener (`src/engine/backend/debugger/debugServer.cpp:231`, `:1041`).

Test: `SocketLockFix.ServerShutdown_WhileWaitingForClient_IsBounded` performs 100 real loopback listener lifecycles and bounds every shutdown below one second.

Commit: `2474492a Fix debugger listener shutdown race on macOS`.

### Window aggregate grouping validation

Symptom: `SUM(x) OVER (PARTITION BY ... ORDER BY ... ROWS)` caused otherwise plain projected fields to be rejected as neither grouped nor aggregated.

Cause: the grouping collector treated an aggregate with `OVER` like an ordinary group aggregate and did not traverse window keys.

Change: window aggregates no longer fold the SELECT for group validation; their argument, partition, and ordering expressions still obey an ordinary `GROUP BY` beside the window (`src/engine/backend/query/queryLowering.cpp:5688`).

Tests: `QueryGrouping.AWindowAggregateDoesNotTurnTheSelectIntoAGroup` and negative control `QueryGrouping.AWindowArgumentStillObeysAnOrdinaryGroupBesideIt`.

Commit: `349606e2 Keep window aggregates out of group validation`.

## Query keyword matrix

| Combination | Result |
| --- | --- |
| `TOP + DISTINCT + ORDER BY` | correct |
| `BETWEEN + LIKE` | correct |
| `GROUP BY + HAVING + CASE` | correct |
| `LEFT JOIN + IS NULL` | correct |
| `INNER JOIN + IN (subquery)` | correct |
| `UNION ALL` | correct |
| `Balance()` | correct |
| `Turnovers() + GROUP BY` | correct |
| accounting `TOTALS + HIERARCHY` | correct as module query; composer limitation is explicit |
| temp table package + `TOTALS` | correct as module query; composer limitation is explicit |
| `RIGHT JOIN` | correct |
| `FULL OUTER JOIN` | correct |
| `REFS` | correct |
| `CAST` + compound recorder field | correct |
| window `SUM + PARTITION + ORDER + ROWS` | fixed |
| `TOTALS + PERIODS(Month)` | correct |
| `= UNDEFINED` | correct in the current build |
| calendar scalars + grouping | correct |
| `ALLOWED` | correct |
| `FOR UPDATE` | correct |

These results cover parsing, metadata resolution, and validation. Database execution was separately verified on the synthetic 100,000-row register, including raw totals, `Balance()`, grouped report totals, and the window query.

## Found, not fixed

- `DateTime.TheBridgeCarriesAReadingByItsParts` reproduces a one-hour mismatch on this host: expected 12, received 13 for `1969-12-31 12:00`. Suspected historical timezone/DST conversion.
- With both database drivers disabled, `oes_tests` still compiles driver tests but excludes their implementations, producing link failures. Suspected CMake source gating.
- Direct uninstalled GUI launch requires the Firebird client runtime beside the binaries. With it present, current-source Designer and Enterprise initialize and complete the integration run; packaging/runtime discovery can still be hardened.
- Non-exact `ibNumber` division measures about 97.4 ns/op, roughly 135x its native control. No isolated regression or safe correction was established.
- `NOT IN (SELECT ...)` can be expanded into an unbounded parameter list; the payroll run crossed Firebird's 32,767-parameter limit at roughly 31k existing employees. `LEFT JOIN ... IS NULL` is the working shape; query lowering still needs a general fix.
- MCP boolean metadata writes can report a value-kind error despite the persisted flag, and newly enabled calculation-register derived sources may require metadata reinitialization before source discovery sees them.

## Verification

- Release `oes_tests`, `designer`, and `enterprise` build.
- Targeted regressions: 3/3 pass.
- Related parser/grouping/socket suite: 19/19 pass.
- Full Release suite: 2,281 pass, 12 skip, one unrelated failure described above.
- Fresh current-build integration: 1,000,000/1,000,000 stock rows and 63,000/63,000 payroll rows read back; independent totals and balances match; 20/20 query matrix cases validate; stock report composition into `SpreadsheetDocument` succeeds.
