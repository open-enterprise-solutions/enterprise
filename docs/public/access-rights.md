# Access rights

**What.** What a user may do in a base is said in two parts, both on the configuration's **roles**: the
**rights** - may this user read, write, delete objects of this kind at all - and the **row restriction** -
which of their rows. A user is given roles; a session carries them from login to logout.

**Why.** A right checked only where an object is opened is skipped by every road that does not open one: a
query, a report, a list. So both parts are enforced at the one door every read and write goes through,
and choosing another road does not avoid them. A row restriction is a script handler on the role, not an
SQL template woven into queries: when a user does not see a row, the debugger can stop in the handler and
show why.

## Rights

- **On an object kind:** `Read`, `Write`, `Delete` on catalogs, documents, charts and registers; `Use` on
  commands, forms, sections and predefined jobs.
- **On the configuration:** `Administration`, `DataAdministration`, `UpdateDatabaseConfiguration`,
  `ActiveUsers`, `ExclusiveMode`, `ModeAllFunctions`, and `Assistant` / `DataAssistant` - the two an
  assistant over MCP needs, off until granted.
- **Full access.** A configuration that declares no roles is not governed by them; a user with
  `Administration` has every right.
- **The right comes first.** A query names its tables; each is asked for its right before any row is
  read, and the first refusal ends the query. A refusal is an error naming what was refused, never an
  empty result - unless the query says `SELECT ALLOWED`, which asks for exactly "what I may see": a
  refusal then reads as no rows.

## Row restrictions

A role may have `OnAccessRead` and `OnAccessWrite`. The engine calls them on every query, once for each
table it reads, with the source, the operation (`Read`, `Create`, `Write`, `Delete`) and a verdict to set.
The handler narrows the source and grants:

```
Procedure OnAccessRead(Source, Operation, Allowed)
{
    if (Source = "Document.Invoice") {
        Source = Source.Where(Function(x) { Return x.Organisation = SessionParameters.Organisation; });
    }
    Allowed = True;
}
```

A join is written with `restrict`: `Source = restrict s in Source join a in <table> on s.Warehouse =
a.Warehouse where …;`. What it compares against comes from **session parameters**, filled once by the
session module and refused to every later write, so the restricted code cannot widen its own filter. Over
MCP, `role_restrict` composes a handler from its clauses and answers what the compiler made of it.

## What it guarantees

- **No road around it.** Text queries, LINQ, reports, dynamic lists and object reads and writes all pass
  the same door; code that must read unfiltered (the engine's own totals, say) says so where it reads.
- **A mistake closes, never opens.** A role with no handler imposes nothing. A handler that throws, falls
  through or leaves `Allowed` unset denies.
- **A write is checked as one statement.** A create is a guarded `INSERT … SELECT`, a rewrite a guarded
  `UPDATE`, a delete a guarded `DELETE`; an object row that the filter keeps the statement off is refused.
  A register's set is addressed by its recorder and may be empty, so for it nothing written is no verdict.
- **Several roles compose by their mode.** A permitting role (`CompositionMode = Union`, the default) adds:
  one admitting a row or a right is enough. A restricting role (`Intersection`) grants nothing and
  subtracts: its restriction holds whatever the others admit, and an explicit `True` in it means "no
  objection", not a grant. So an organisation separator is declared once, not repeated in every role, and
  the verdict does not depend on the order roles were given.
- **Live, not cached.** A handler runs on every query and reads the constants and settings of the moment;
  only which handlers a user has is resolved once per session.
- **The same filter on reads and writes.** A condition through references (`x.Contract.Organisation`) and a
  `restrict … join` become a correlated `EXISTS`: a filter that does not multiply rows.

## Where it stops

- One handler serves every table: a condition on a field one source has throws on the others, and the
  role then denies. Branch on `Source`.
- `restrict … where` sees only the source; a joined table is filtered by its own `.Where` inside the join.
- On Firebird, a join over a computed table or several sources is read through an inner join: it filters
  reads only and can repeat a row.
- A condition on a left-joined table, folded into the query's `WHERE`, turns that join into an inner one.
