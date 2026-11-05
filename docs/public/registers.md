# Registers

**What.** A register keeps facts in rows keyed by **dimensions**, with **resources** to sum and
**attributes** to carry. Four kinds:

| Register | Keeps | Read as |
|---|---|---|
| `InformationRegister` | values by key, periodic or not; written on its own by key, or as a document's movements (`WriteMode`) | the records; `SliceLast`, `SliceFirst` at a moment |
| `AccumulationRegister` | movements of quantities and sums: receipts and expenses (`RegisterType` *Balances*), or turnovers alone | `Balance`, `Turnovers`, `BalanceAndTurnovers` |
| `AccountingRegister` | entries on a chart of accounts, one-sided or in correspondence (`Correspondence`) | `Balance`, `Turnovers`, `DrCrTurnovers`, `BalanceAndTurnovers` |
| `CalculationRegister` | calculation records on a chart of calculation types, each for an action period | the records, `ActualActionPeriod`, the base, its `Recalculation` marks |

**Why.** A document says what happened; a register says what it changed. A balance asked at a moment is
then a reading of the register, not a walk over every document that ever touched it - and a report, a
posting check and a script all ask the same reading.

## How it works

- **A recorder writes a set.** A register subordinate to a recorder takes its rows as the movements of a
  document. The movements of one recorder are one record set, written whole - the old set out, the new
  in - in the document's transaction, so a posting and its movements commit together.
- **Readings are query sources.** `Balance`, `Turnovers` and the rest are tables a query names
  (`FROM AccumulationRegister.Stock.Balance(&At, Warehouse = &Warehouse)`), reached by the query language
  and by LINQ alike;
  they lower into the same door every query goes through, rights and row restrictions included
  ([access-rights.md](access-rights.md)). A script's manager methods read the same tables.
- **Periodicity is a reading's parameter, not the register's.** One register answers by day, month or
  quarter: `BalanceAndTurnovers` takes the begin, the end, the periodicity and a filter, and reports the
  opening, receipt, expense, turnover and closing per period.
- **Totals are kept by the database.** An accumulation or accounting register declares a totals table
  beside its movements. Triggers on the movements keep it, views read it; the engine computes nothing
  in between. Where a driver keeps no totals the same readings are aggregated live from the movements,
  in the same shape.

## What it guarantees

- **Totals are a consequence of the movements.** A totals table that is created, or whose meaning
  changes (a resource added, the register's kind switched), is filled again from the movements by the
  same apply that changed it - never left empty, which would read as "no stock".
- **A zero balance is no row.** A key whose every resource balances to zero is absent from `Balance` - an
  item that took in 100 and gave out 100 included - while its turnovers still report. Zero is judged on
  the folded figure, never on the stored rows.
- **A total keeps its resource's precision.** The accumulating column is declared with the resource's own
  scale: cents stay cents when summed ([number-model.md](number-model.md)).
- **Concurrent postings do not queue on one row.** With `SplitTotals` (on by default) one logical totals
  row is spread over several physical ones; a read sums them back, as it reads anyway, and a platform
  job folds them together again.
- **A balance between two points is exact.** Stored totals are taken to the grain below the boundary,
  movements of that grain up to it - the same rule for the date and for the point in time
  (date + document), so a balance before and after a document differ by that document.
- **A period with a balance and no movement is still a row** in a periodised `BalanceAndTurnovers`: the
  calendar of the interval is generated, not read from the movements.

## Where it stops

- A periodised reading builds its calendar on the server up to 200 periods; past that it is computed in
  memory over the same periods.
- A calculation register keeps no totals. Its records are read with their displacement and base, and
  its `Recalculation` marks say which records the later ones sent back.
- A record set is the unit of writing: one recorder's movements are replaced whole, never patched line
  by line.
