# The query language

**What.** A query is text in the configuration's words - `SELECT … FROM Document.Sales.Goods WHERE
Product.Manufacturer = &Maker TOTALS SUM(Amount) BY Product` - with parameters written `&Name`. The text is
read, written and rewritten in the core (`core/query`), so the designer's query constructor and the server
hold the same tree. On the server it is lowered into the one door every read goes through, and from
there into the SQL of the base's DBMS.

**Why.** The author of a configuration names objects and attributes, not tables and columns: a reference
stored in several columns, a composite type in a tag and a value, a table whose name is an id. The text
says the meaning; the engine knows the storage. And one door under every front-end - this text, LINQ in
a script, a report, a list - is one place for rights, row restrictions, locks and the dialect.

## The language

- **Sources.** Objects and their tabular sections; registers and their readings (`Balance`, `Turnovers`,
  `SliceLast`, … - [registers.md](registers.md)); a temporary table made earlier in the package or held by
  a temporary tables manager (`FROM Goods`); a value table handed in as a parameter (`FROM &Goods`).
- **References.** `Product.Manufacturer.Name` follows the reference and joins by itself. `REFS` tests a
  reference's kind; `VALUE(Catalog.Units.Piece)` names a predefined value; `IN HIERARCHY` takes a group
  and everything under it.
- **Expressions.** `CASE`, `CAST` (a narrowing - a composite to one kind, a number to a precision - not
  a conversion), `ISNULL`, `LIKE`, `BETWEEN`, `IN` a list or a subquery; date parts `YEAR` … `SECOND`,
  `BEGINOFPERIOD`, `ENDOFPERIOD`, `DATEADD`, `DATEDIFF`, `DATETIME`; `SUBSTRING`, `VALUETYPE`, `TYPE`,
  `PRESENTATION`, `REFPRESENTATION`.
- **Grouping and windows.** `SUM`, `COUNT`, `MIN`, `MAX`, `AVG`, `GROUP BY`, `HAVING`, `DISTINCT`,
  `TOP n`; window functions over `OVER (PARTITION BY … ORDER BY … ROWS | RANGE …)`, `ROW_NUMBER`, `RANK`,
  `DENSE_RANK`.
- **Totals.** `TOTALS SUM(Amount) BY Product HIERARCHY, (Partner, Contract), Period PERIODS(Month, &From,
  &To)` turns the result into a tree of subtotals: `OVERALL` for the grand total, a bracketed level keyed
  by several fields, a level that fills its calendar, and `SPLIT` to fold the same rows several ways
  beside the main tree.
- **A package.** Statements separated by `;`. `INTO` makes a temporary table (`INDEX BY` its columns),
  `DROP` releases it, `ONTO` names a result, `LINK` relates named results by joins.
- **`FOR UPDATE`** holds the rows a select returned until the transaction ends
  ([record-locks.md](record-locks.md)). **`SELECT ALLOWED`** reads rows the user may not see as none,
  instead of a refusal ([access-rights.md](access-rights.md)).

## What it guarantees

- **One door.** Text, LINQ, reports, dynamic lists and object reads lower into the same door; rights and
  row restrictions are enforced there, for all of them alike.
- **No SQL is written by hand.** A query becomes a structured description that each DBMS's dictionary
  spells; a value is always bound, never pasted into the text.
- **One answer wherever it is computed.** What the database cannot compute (a reading built in memory, a
  table handed in) is joined and compared in memory by the same rules - `NULL`, comparison and identity
  as SQL defines them - and a computed side's keys are pushed into the database side's read, so only rows
  that can join are fetched.
- **A refusal says where.** A form the language does not take is refused at its position with a sentence
  - `INDEX BY` without `INTO`, `TOTALS` with `INTO`, a hierarchy over a level of several fields - never
  read as something else.
- **Measured.** Each query's road and the time of each stage go to the technology journal.

## Where it stops

- The keywords are English; the names in a query are the configuration's.
- A named result is a server-side `WITH` where the DBMS reads one and the select has a server-side form; a
  named result with `TOTALS` or `UNION`, or one over a source computed in memory, is taken as rows.
- A dialect is sent only the forms it promised to take. The generic ODBC dialect promises neither window
  functions nor `WITH`; a query that needs them there is computed in memory or refused with a sentence.
- A periodised calendar is built on the server up to 200 periods; past that, in memory.
