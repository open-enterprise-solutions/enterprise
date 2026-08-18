# Data transfer — saving a database to a file and loading it back

`SaveDatabase` / `LoadDatabase` (`backend/appData.cpp`) move a whole base between databases. This is
not a backup of the *file* — it is a transfer of the *configuration plus its data*, so it works
between engines (Firebird → PostgreSQL) and between machines.

---

## 1. The file

A zip with three entries, written in this order:

| entry | content | written by |
|---|---|---|
| `config` | the whole configuration (metadata tree) | `SaveConfigToBuffer` |
| `user` | users + their access | `SaveUserInfoToBuffer` |
| `data` | every declared table's rows, framed by the table's metaID | `DumpDataToBuffer` → `ibDataMover::Dump` |

**One source of truth for what to move.** The dump walks the SAME schema snapshot the DDL differ
consumes (`BuildSchemaSnapshot`), so a table is dumped exactly when it is declared. A pure
scaffold / seed table (an enumeration) is skipped — its rows are declared, not entered.

## 2. The load is structure-then-data, and the order is enforced

1. `ClearDatabase` → `ReCreateDatabase`: the builder drops every table of the current configuration
   and recreates + seeds them.
2. entry `config` → `LoadConfigFromBuffer`, then **`SaveDatabase(saveConfigFlag)` applies it** —
   creating the very tables, with the very column ids, the rows are about to be written into.
3. entry `user`.
4. entry `data` → `RestoreDataFromBuffer` → `ibDataMover::Restore`, per table, by metaID.

Because the structure arrives in the same file as the rows, **column ids match by construction** —
they are not matched by name and not guessed.

Two failures used to hide in this sequence and are now closed:

- The answer of the apply was **discarded**. A failed apply went unnoticed and the load carried on,
  pouring rows into tables that were absent or still shaped like the base being replaced: nothing
  restored, nothing said, a base left half-replaced. A refusal now stops the load.
- The entry order was **not required**. Our writer emits `config` first, but a file that said
  otherwise would have filled the structure being replaced. Data before configuration is now refused.

## 3. What identifies a row on the wire

`ibDataMover::KeyOf` answers with a **COLUMN**, never with a name:

| table | key | unique | restore |
|---|---|---|---|
| reference object (catalog / document / charts / enums) | its own reference (`_TYPE`/`_RTRef`/`_RRRef`) | yes | UPSERT matching on the key's fields |
| tabular section | the owner reference (`ibOwnerRefField`) | no — repeated per owner | INSERT |
| `sys_const` (external) | `RECORD_KEY` | yes | UPSERT |
| register | none (composite identity lives in the dimensions) | — | INSERT |

**Why a column and not its name.** A name is what a column is *called*; what a table *has* are the
fields the column spreads into. While every key was a single raw scaffold field the two were the same
string — so the mover carried the name and read it back with `GetResultString`. A reference key
spreads into three fields and its bare name is a field NOTHING has: the first dump of a catalog died
on *"Field 'fld1009' not found in the resultset"*, while those same three fields were being read
perfectly well one loop below, as an ordinary column.

**The key is not dumped twice.** When it rides as one of the columns (a reference object), it travels
as that column. Only a key that is NOT a column (a section's owner, a constant's `RECORD_KEY`) gets a
chunk of its own — and it goes through the same codec as every cell, so identity never travels as
text.

## 4. The cell codec knows both kinds of column

`BinaryFromResult` / `BinaryToStatement` open with the `_TYPE` discriminator — which a **raw** column
does not have. That is why the key used to be carried as a string: there was no other route. Both now
branch first on `IsRawColumn()` — raw goes straight by its declared `RawType`, a metadata column goes
through the spread. Same split the write door (`BindWriteValue`) already used.

## 5. Wire rules that cost time to learn

- **One chunk is one cell — read it once.** The restore loop used to read a cell `while (!eof)`,
  which assumes every read consumes something. Zero's encoding is "write nothing", so the cursor
  never moved and the same cell was re-read forever: a load that hangs on a table of numbers is not
  slow and not deadlocked.
- **An EMPTY chunk is indistinguishable from an absent one** — `ibReaderMemory::r_chunk` answers "not
  found" for size zero. So zero stays unwritten and the READER treats a missing chunk as zero
  (`ibNumber::SetBuffer`). Writing an empty chunk "to be explicit" only puts bytes on the wire that
  nobody reads back.
- **A column the file has and this configuration does not is an ordinary event** — the file was
  written by another configuration. Its cell is skipped with a log line; the row still restores. (It
  used to be a `std::map` subscript, which INSERTS a null for the missing id and hands it on to be
  dereferenced.)

## 6. Honest remainder

- Registers restore by INSERT, so loading into a base that already holds movements appends rather
  than merges. The intended use is a load into a **cleared** base, which is what `LoadDatabase` does.
- Derived (totals) tables are never moved — they are regenerated from the movements they summarise.
- The transfer carries no journal (`oeslog`) and no session state; those belong to the base, not to
  the configuration.
