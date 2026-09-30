# Fix debugger shutdown race and window aggregate validation

## Summary

- Checked 20 query-language combinations against real configuration metadata: 18 validated, one drove a fix, and one remains unresolved. Recorded Release runtime/LINQ baselines.
- Found two correctness defects: a reproducible macOS debugger shutdown crash and false grouping rejection for window aggregates.
- Fixed both defects in the engine and added focused positive and negative regression coverage.
- Release test, Designer, and Enterprise targets build; targeted and related suites pass.
- Full suite result is 2,281 passed, 12 skipped, and one pre-existing timezone-sensitive failure.

## Measurements

| Scenario | Small | Large | Growth | Before fix | After fix |
| --- | ---: | ---: | ---: | ---: | ---: |
| debugger create/wait/shutdown | — | 100 rounds | — | 3/3 debug launches crashed | 100/100 listener cycles pass; ~54 ms/cycle |
| window aggregate validation | 1 query | — | — | rejected as ungrouped | accepted; mixed invalid control still rejected |
| LINQ join | 404.0 ns/row at 250 | 463.7 ns/row at 16k | 1.15x per-row | unchanged | unchanged |
| block select | 164.1 ns/row at 1k | 175.5 ns/row at 16k | 1.07x per-row | unchanged | unchanged |
| group by | 334.1 ns/row at 1k | 394.5 ns/row at 16k | 1.18x per-row | unchanged | unchanged |
| order by, one key | 385.0 ns/row at 1k | 445.1 ns/row at 16k | 1.16x per-row | unchanged | unchanged |

The database-backed 10k/100k comparison is intentionally not claimed: the installed runtime crashed on the tested debug path, while the local uninstalled app did not complete initialization. The complete baseline, including strings, method dispatch, projections, and number arithmetic, is in `perf-night-report.md`.

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
| `= UNDEFINED` | unresolved: parsed as an unknown source attribute; `IS UNDEFINED` also fails parsing |
| calendar scalars + grouping | correct |
| `ALLOWED` | correct |
| `FOR UPDATE` | correct |

These results cover parsing, metadata resolution, and validation. They do not imply database result equivalence where execution could not be completed.

## Found, not fixed

- `DateTime.TheBridgeCarriesAReadingByItsParts` reproduces a one-hour mismatch on this host: expected 12, received 13 for `1969-12-31 12:00`. Suspected historical timezone/DST conversion.
- With both database drivers disabled, `oes_tests` still compiles driver tests but excludes their implementations, producing link failures. Suspected CMake source gating.
- The local uninstalled Release `.app` exits with status 1 before its MCP endpoint appears and produces no crash report; installed/package validation remains necessary.
- Non-exact `ibNumber` division measures about 97.4 ns/op, roughly 135x its native control. No isolated regression or safe correction was established.
- Undefined comparison has no validated spelling in the tested query dialect: `P.Recorder = UNDEFINED` fails name resolution and `IS UNDEFINED` fails parsing. `IS NULL` is not assumed equivalent.

## Verification

- Release `oes_tests`, `designer`, and `enterprise` build.
- Targeted regressions: 3/3 pass.
- Related parser/grouping/socket suite: 19/19 pass.
- Full Release suite: 2,281 pass, 12 skip, one unrelated failure described above.
