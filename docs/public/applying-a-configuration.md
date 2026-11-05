# Applying a configuration

**What.** The designer edits a configuration of its own. **Update database configuration** (MCP:
`config_apply`) makes it the one the base holds: the database's tables, columns and indexes are brought
to what the edited configuration declares, and the running application is given it.

**Why.** The configuration is the description of the base; the database is its consequence. A schema
written by hand, or checked against what happens to stand in the database, makes a drift look normal and
hides the only sign that something is broken. So the structure comes from one place — the declaration —
and by one road.

## How it works

- **Two arguments.** The apply is a function of the active configuration (the baseline) and the edited
  one (the target). Each is projected into tables, columns and indexes; the difference of the two
  projections is the DDL. The live database is never asked what it has.
- **By identity, not by name.** A table is its object's id, a column its attribute's. A rename is an
  `ALTER`, a removed attribute a `DROP`, a new one an `ADD`.
- **Structure, then publication.** The DDL runs first; the active configuration is written last, once
  every phase of the structure has run. Until then the base still says the previous configuration.
- **Code needs no structure.** A changed module goes in by the same apply, with no exclusive mode; an
  application already running keeps the code it started with until it is started again.

## What it guarantees

- **The schema the target declares — exactly.** Not approximately, and not "plus what the base happened
  to have". A fresh base is the same function with an empty baseline.
- **All or nothing.** A refusal anywhere rolls the apply back: on PostgreSQL it is one transaction; on
  Firebird, whose views and triggers compile at commit, a second phase runs after the first commit and is
  undone from a ledger of what the first one did (an added column comes off, a dropped one returns, an
  altered type is restored). A half-applied base is not a state the platform leaves.
- **A failed apply is not repeated wrongly.** Since the configuration is published last, the next apply
  sees the whole difference again and converges.
- **A failure is written down.** The base's journal gets `apply_failed` with the reason.
- **You can see it before it happens.** `database_diff` says what the base does not have yet, in the
  words of the configuration ("document *Receipt* got the attribute *Warehouse*"), not in DDL. An apply
  with `confirm=false` goes all the way and rolls back, answering with every `CREATE`, `ALTER` and `DROP`
  it would have made.
- **Structure under lock.** A change to stored structure takes the base exclusively. While other sessions
  work in it the apply is refused, and says so (`OthersActive`); while it runs, nobody else comes in.
- **Taking it back.** `config_rollback` throws away what was edited since the last apply and takes the
  base's copy again — before applying; afterwards the base's copy is what was edited.
- **Loading a base from a file is structure first.** The file's configuration is applied, creating the
  tables the rows go into, and only then are the rows restored.

## Where it stops

- **A base changed from outside is not covered.** A hand-run `ALTER TABLE`, a foreign migration, a dump
  restored by another tool: the platform does not detect it, reconcile it or repair it. Whoever edits the
  schema by hand owns the schema.
- **A derived table is rebuilt, not restored.** Register totals dropped by a refused apply are regenerated
  from the movements on the next one, never put back empty.
- **A schema the engine declares by code** — a column or an index a new build of a metatype adds to an
  object already saved — is not seen by the diff, which projects both configurations with the same
  binary. Such a change has to come as a step the user takes (the configuration's compatibility
  version), not as a silent difference.
