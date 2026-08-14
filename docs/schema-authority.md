# Schema authority — the diff carries everything, and nothing checks it against the base

> **Scope:** where the physical database schema comes from, what the restructuring mechanism
> guarantees, whose database that guarantee covers, and the one thing the mechanism is forbidden to
> do about it. Stated as policy by the project owner on **2026-08-14**, at the end of two days of
> apply failures ([register-shared-machinery.md § 4d](register-shared-machinery.md)).
>
> Companions: [schema-first-metadata.md](schema-first-metadata.md) (the node model the declaration
> rides), [query-engine-layers.md](query-engine-layers.md) (which floor renders the DDL),
> [exceptions.md § 5a / § 5b](exceptions.md) (how a refusal travels out of an apply),
> [connection-pool.md](connection-pool.md) (whose connection it runs on),
> [compatibility-version.md](compatibility-version.md) (the version the user steps up, which IS the
> migration).

---

## 1. The rule

> ⭐⭐ **THE DIFF BETWEEN TWO CONFIGURATIONS CARRIES ALL THE INFORMATION.** Applying it brings the
> database to a schema that matches the new configuration **exactly** — not approximately, and not
> "plus whatever the base happened to have". That is the **goal** of the mechanism, the thing every
> part of it exists to deliver. It is not a claim to be double-checked against the database
> afterwards.

The apply is a function of exactly two arguments: the **baseline** (the active configuration, re-read
from `config`) and the **target** (the edited one). `ibSchemaSnapshot` projects each into tables,
columns and indexes; `DiffSnapshots` turns the pair into DDL and knows nothing about metadata
(`query/schemaSnapshot.{h,cpp}`). Matching is by **identity, not name** — a table is its metaID, a
column its model id — so a rename is an `ALTER` and a vanished id is a `DROP`. Indexes match by name
between the two snapshots (`AlterTable`, `schemaSnapshot.cpp:396–433`); at no point is the live
database asked what it has.

The corollary is the whole reason to write this down: **because the apply is a function of
(baseline, target), the only way it can produce a wrong schema is to be told something that is not
so** — and a baseline that does not describe what stands in the database is the commonest form of
that. Every apply defect recorded on 2026-08-13/14 is a variation on it:

| The defect | What it made the mechanism believe |
|---|---|
| `Correspondence` / `SplitTotals` named in neither `ReadData` nor `WriteData` | that the register's schema settings were at their defaults — a setting that builds the schema was never saved, so every re-read produced a baseline nobody edited ([register-shared-machinery.md § 4d](register-shared-machinery.md)) |
| `config` written in the same transaction as the DDL | that the apply had finished: a *correct* baseline made durable before the deferred phase ran, i.e. ahead of the physical schema (§ 4.2 below) |
| `ibMaterializeApply::Failed` read by nobody | that a refused `CREATE TRIGGER` had succeeded — so the configuration was saved over the old one and the next diff had nothing to say ([exceptions.md § 5a](exceptions.md)) |
| a `m_beforeChange` rule answering PASS out of a `catch` | that the change was allowed — a rule that could not run granted the strongest permission the system has ([exceptions.md § 5b](exceptions.md)) |

None of those is a differ bug. In all four the differ did its job perfectly over the facts it was
handed, and a fact was wrong.

---

## 2. Whose database — the scope of the guarantee

⭐⭐ **The warranty case is a base WE created and have maintained through restructurings.** It was
created by this mechanism and every change since went through this same mechanism, so the diff
describes the physical schema exactly **by construction**: the schema after step *N* is what the
configuration at step *N* declares, because that is the only thing that ever wrote to it. The
induction even has its base step in the code — `DiffSnapshots(nullptr, target)` is create-all, so a
fresh database is the degenerate case of the same function rather than a separate path.

A base changed **from outside** — a hand-run `ALTER TABLE`, a foreign migration script, a dump
restored from another tool, a column dropped in an admin GUI — is **explicitly not a warranty case**.
We do not detect it, do not reconcile it and do not repair it. That is a stated boundary, not an
omission: whoever edits the schema by hand has taken ownership of the schema, and there is no
question the platform could ask that would tell an intentional hand-edit from a defect in its own
apply.

---

## 3. Therefore: physical introspection is banned

> ⭐⭐ **INSURING AGAINST THE PHYSICS LEGALISES THE DRIFT.** If the apply reads the live column list
> and repairs whatever it finds, then a database that disagrees with its configuration stops being a
> defect and becomes a state the code handles — a **normal** state. That disagreement is the only
> signal that the mechanism is broken. Insurance deletes the signal and keeps the breakage.

Read § 1's table again with introspection in place: all four defects would have applied "cleanly".
The unsaved setting, the too-early publication, the unread `Failed`, the guard that could not run —
each would have shown up as nothing at all, or at most as a slightly longer apply, and all four roots
would still be live. What was found in two days by *reading the failure* would have been converted
into a permanent, invisible tax.

There is a second cost, structural rather than diagnostic. An apply that repairs from the physics has
**two sources of truth for the same fact** and must choose between them at every column. A differ
over two descriptions is readable and testable; a differ over two descriptions *and* a live catalogue
is a three-way merge nobody specified, in which "the base has a column the target does not declare"
has no correct answer — drop it (and destroy a hand-made change deliberately), keep it (and never
converge), or ask (in a batch apply with no user).

⚠ **This was written three times on 2026-08-14 and removed three times** — table and column
introspection each time, and on the last pass the pre-existing *index* introspection went with it:
`ibDialectDictionary::m_indexListQuery` no longer exists, and indexes now diff between the two
snapshots like everything else. The comment at `firebird/firebirdDatabaseLayer.cpp:56` still
describes the old behaviour and is stale, as is the 2026-06-09 update block in
[query-language-arc.md](query-language-arc.md). **Do not revive it.**

### What is NOT introspection in this sense

Three physical reads stay, and the line between them and the banned thing is sharp: **none of them
decides what the diff is.**

| Read | Where | Why it is legitimate |
|---|---|---|
| `m_viewExistsQuery` / `m_triggerExistsQuery` | dialect probes, `databaseLayer.h:414–426` | they make a `DROP` survivable where a failed DDL is not harmless — Firebird rolls the whole restructuring back, so a drop of something never created would destroy the first apply. The question is "will this statement kill the transaction", not "what should I change" |
| `ibDatabaseQueryBuilder::TableExists()` in a `m_beforeChange` guard | e.g. the chart's analytics ceiling, the hierarchy rule | a rule that reads rows has one legitimate reason to find none — the table does not exist yet, on a base nothing has been applied to — and it must tell that from "I could not establish it" ([exceptions.md § 5b](exceptions.md)) |
| `TableExists` / `GetColumns` in `appDataQuery.cpp` | the platform's OWN tables (`sys_user`, `sys_session`, the job table, the bytecode cache) | no configuration declares them, so no diff covers them: they are **bootstrapped**, not restructured, and the code that creates them is the code that reads them |

The test for anything new: **if the answer changes what DDL is emitted, it is banned.** If it only
changes whether a statement is safe to issue, or concerns a table no configuration describes, it is
not.

---

## 4. What the rule already forces

Four consequences, all landed 2026-08-14, each documented where it lives. They are not four separate
policies — they are this one rule, said four times about four moments in an apply.

**4.1 An apply must be interruptible and fully rollback-able.** A refusal raises and unwinds to the
apply's `catch`, which rolls back and rethrows; a helper that answers with a `bool` is read by its
caller, which raises. Half-applied is not a state the system may be left in, because a half-applied
base is precisely a base whose configuration no longer describes it —
[exceptions.md § 5a](exceptions.md).

**4.2 The ACTIVE configuration is published LAST.** `config` is written in `OnAfterSaveDatabase`,
after the restructuring transaction has committed and the deferred phase has drained; `config_save`
(the edited configuration) still travels with the DDL, because that pair is atomic. Publishing
`config` beside the DDL declared the schema finished while a phase of it had not run, and the next
diff then compared the new configuration against itself —
[register-shared-machinery.md § 4d](register-shared-machinery.md).

**4.3 The second phase is compensated.** On Firebird views and triggers compile at `COMMIT`, so the
maintenance and the seeds cannot live in the transaction that creates the tables they address — they
are deferred past it and therefore cannot be undone by rolling that transaction back. What replaces
the rollback is **repeatability**: nothing durable declares the apply finished until the deferred
drain has run, so a failure anywhere leaves `config` at the previous configuration and the next diff
has real work to do. A refused commit is itself rolled back (once, in `ibDatabaseLayer::Commit`, for
every driver), so a failure does not leave a live transaction holding locks that the next apply meets
as a deadlock.

**4.4 Anything that participates in the SCHEMA must be serialised.** A property read by
`ContributeTables` and absent from `ReadData` / `WriteData` cannot survive a save, and a setting that
cannot survive a save is a baseline that lies. The check is mechanical: compare the `m_property*`
members declared in a metatype's header against the ones named in its `ReadData` / `WriteData`
(predefined *attributes* are exempt — they serialise as attribute objects, not as properties).

---

## 5. How to use this rule when something goes wrong

When an apply and the database disagree, the question is **not** "how do we make the apply notice".
It is **which of the two configurations is wrong, and what wrote it that way** — the baseline that
was re-read, or the target that was saved. Every root cause found so far has been in that answer, and
every proposed fix that reached for the physical catalogue was a way of not asking it.
