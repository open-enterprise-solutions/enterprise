# Metadata storage: per-entry container

Status: **design detailed; backlog until the size / partial-save triggers fire** (see end).
Builds directly on the landed node-serialization walk (`metadata-serialization-arc.md`):
`SaveSubtree`/`LoadSubtree` already (de)serialize one node + its subtree — that is exactly
"fill / read one container entry".

## Core model

A configuration is, conceptually, a **container of named entries** — a folder tree of files
(`Catalogs/Catalog1`, `Documents/Document1`, …) plus a manifest. The engine loads entries
**from the container**; how the container is physically stored is an implementation detail
behind one interface, `ibConfigContainer`. Two backends of the **same** model (not two
designs):

- **live config → DB rows** (`sys_config_entry`): the table *is* the container, the PK index
  the directory, a transaction the atomic commit, a row the entry;
- **file boundary → an archive** (`.mcf` / export; ZIP by default, any format — tar/7z/ISO/…):
  the archive's table of contents is the directory, a file the entry, per-entry deflate the
  compression.

Same entries, same per-entry `SaveSubtree`/`LoadSubtree`; only the **directory + physical
storage** differ. Everything below is this one model specialized to the two backends.

## Problem (current state)

- The whole configuration is **one blob row** in `sys_config` (committed) / `sys_config_save`
  (designer working copy): `(file_name PK, file_guid, binary_data BLOB)`, one row,
  `file_name = 'sys.database'`, `binary_data` = the entire recursive chunk tree.
- Save rebuilds the **whole** blob → one UPSERT; load parses the **whole** blob.
- Change detection = one MD5 over the whole blob (`m_md5Hash`): any edit re-hashes and
  rewrites everything; `IsConfigSave` = compare that one MD5 (working vs committed).
- Does not scale: a large config rewrites all of it to flip one field, and there is no
  metadata-level lazy load.
- **NB the config blob is METADATA** — object types, forms, modules — **not table data**
  (table rows round-trip through `DumpTable`/`RestoreTable`, a separate path). 200 MB of
  metadata is pathological, but large embedded forms / module text / pictures bloat it.

## Live backend (DB rows): the table is the container

OES always has a database, so do **not** build a custom container-blob with an internal
directory + free-list for `sys_config`. A relational table already gives, for free, what a
container format hand-rolls: a directory (the PK index), per-entry addressing, atomic
updates (a DB transaction), and block allocation. The **file export** variant (`.mcf`, no DB)
gets the same "directory + per-entry blocks" for free from a **ZIP archive** — see the file
variant below; no hand-rolled container format on either side.

### Schema

Replace the single-blob tables with per-entry rows (two tables keep the existing
working/committed split):

```
sys_config_entry        -- committed
sys_config_entry_save   -- designer working copy
  guid          CHAR(36) PRIMARY KEY   -- entry identity = object GUID (root has the config GUID)
  parent_guid   CHAR(36)               -- = root for every top-level object (flat today);
                                        --   reserved for deeper nesting later
  clsid         BIGINT                 -- factory class id to recreate the node
  metaid        INTEGER                -- node meta id
  content_hash  CHAR(32)               -- per-entry MD5 — replaces the whole-blob m_md5Hash
  binary_data   BLOB                   -- this entry's SaveSubtree (own data + its inline subtree)
```

Promote (apply) = per-row copy of the **changed** rows from `_save` → committed, not a
whole-blob swap.

### Granularity: one row per top-level object

- Entries = the Configuration **root** + each **top-level** metadata object (Catalog /
  Document / Constant / Enumeration / Information&AccumulationRegister / ChartOf* /
  AccountingRegister / DataProcessor / Report). **Not every node** — a top-level object's
  attributes / tabular sections / forms / modules stay **inside** that object's blob (its
  `SaveSubtree` already serializes the whole subtree).
- Rows are therefore effectively flat: `parent_guid` = root for every object; the tree depth
  lives inside each entry blob.
- The **root entry** blob = the root's own data + the children that are **not** separate
  entries (Language, Roles, common modules / common forms — the non-object children). A new
  predicate `IsContainerEntry(clsid)` (mirrors the existing `FilterChild`) decides
  "separate entry vs inline in the root".

## Essence of the changes (component by component)

1. **Schema** — new `sys_config_entry[_save]` tables; `CreateConfigTable` /
   `CreateConfigSaveTable` rewritten. `sys_sequence` stays as-is.

2. **Save path** (`OnSaveDatabase` / `SaveConfigToBuffer`):
   - was: `SaveCommonTree(whole tree)` → one UPSERT into `sys_config_save`.
   - now: serialize the **root entry** (root + inline children, excluding container-entry
     objects) → UPSERT its row; then for each top-level object `SaveSubtree(object)` →
     compute `content_hash` → **UPSERT only if the hash changed** (per-entry dirty); DELETE
     rows whose object was removed.
   - The header (sign + config GUID) moves into the **root entry** row, not a per-blob prefix.

3. **Load path** (`LoadDatabase` / `LoadCommonTree`):
   - **runtime**: `SELECT guid, clsid, binary_data FROM sys_config_entry` → load the root
     entry into `m_commonObject`, then for each object row factory-create by `clsid` +
     `LoadSubtree(blob)` as a child of root. Runtime still needs the **full** tree
     (`RunDatabase` registers ctors over everything), so runtime load is **not** lazy.
   - **designer**: load the root entry eagerly (cheap); **fault-in object entries by GUID on
     first access** — this is where lazy load actually pays off.

4. **Change detection** — `m_md5Hash` (whole blob) → per-entry `content_hash`. `IsConfigSave`
   compares the set of `(guid, content_hash)` working vs committed instead of one MD5. This
   is the main everyone-wins gain (the lazy-load gain is designer-only).

5. **`ibConfigContainer` abstraction** — a thin reader/writer over entries:
   `OpenEntry(guid) -> blob`, `WriteEntry(guid, clsid, metaid, hash, blob)`,
   `DeleteEntry(guid)`, `ListEntries() -> [(guid, clsid, hash)]`. The DB variant implements
   it over the rows; the file-export variant over a container-blob-with-directory. Save/Load
   talk to the interface, not the table.

6. **File export (`.mcf`)** — no DB → use a standard **archive container with one file per
   entry**, not a hand-rolled format. Any directory-of-files archive works (ZIP / tar / 7z /
   ISO / …) — that is the point of hiding it behind `ibConfigContainer`: the archive's own
   table of contents *is* the directory/manifest, and per-entry compression comes for free.
   **ZIP is the pragmatic default** only because `wx` already ships `wxZipOutputStream` /
   `wxZipInputStream` and the `.obk` format already uses it (`appData::SaveDatabase` /
   `LoadDatabase` write/read `config` / `user` / `data` entries sequentially) — so the file
   variant is the natural generalization of an existing path: replace the single `config`
   entry with one entry per top-level object. Swapping ZIP for another format later is an
   `ibConfigContainer` backend change, nothing above it moves.
   - **Folder layout mirrors the tree** → the archive is human-browsable and VCS/diff/AI
     friendly (complements the existing XML/JSON text export): e.g.
     `manifest` (sign + config GUID + the `(guid, clsid, metaid, hash)` list),
     `Catalogs/Catalog1.bin`, `Documents/Document1.bin`, … each `*.bin` = that object's
     `SaveSubtree` blob.
   - **Sequential full-load is ZIP's strength** (`GetNextEntry` loop) — the file-load case
     reads the whole config anyway, so it fits.
   - **Lazy random per-entry access from a file is weak** — ZIP's central directory is at the
     *end* and `wxZipInputStream` is a forward stream (no clean seek-by-name), so faulting in
     one entry by GUID from a file means scanning. That is fine: **lazy is a live-DB concern**
     (designer editing a large config against `sys_config`), not a file-inspection one —
     files are imported/inspected whole.
   - `SaveConfigToFile` writes the archive via temp + rename (already landed) for an atomic
     replace.
   - **Do not** store a ZIP blob in one DB cell for the live config — that is whole-blob
     rewrite again (repack the whole archive to flip one field), losing per-row partial save /
     lazy / TX / multi-user. ZIP is the *file-boundary* format; rows are the *live* format.

7. **Migration** — one-time, version-gated: detect the old single-blob `sys_config`,
   `LoadCommonTree` it, re-save as entries, drop the old row. Forward-only.

## Phases (each builds + passes the Step 0 round-trip guard)

- **P1 — schema + container interface** (no behavior change): create
  `sys_config_entry[_save]` and `ibConfigContainer` + its DB impl; keep writing the monolith
  in parallel. Inert until P2.
- **P2 — write path**: save per-entry (root + per-object, dirty-gated) into the entry tables;
  switch the working/committed compare to per-entry hashes. Still **read** from the monolith.
- **P3 — read path (runtime)**: load the full tree from entries; retire the monolith write.
  Migration runs here.
- **P4 — designer lazy load**: fault-in object entries on access; the type / reference
  resolver faults the target entry in by GUID.
- **P5 — file export container**: `.mcf` as an archive container behind `ibConfigContainer`
  (ZIP default via the existing `wxZip*` / `.obk` path; format pluggable — tar/7z/ISO/…),
  one file per top-level object; optional lazy file load.

## Risks

- **Reference integrity under lazy load** — a Document attribute references a Catalog whose
  entry is not inflated yet → the type / GUID resolver must fault the target entry in. Runtime
  is safe (loads all); only the designer-lazy path (P4) needs this.
- **Multi-user partial save** (shara 3–30): two designers editing different objects → per-row
  UPSERT conflicts far less than a whole-blob rewrite, but a per-entry version / optimistic
  check is needed (mirror the `record-locks` optimistic model).
- **AOT bytecode cache** (`sys_bytecode_cache`) — confirm its keying (descriptor + source
  hash + metadata version) is independent of the config storage format.
- **Multi-row apply atomicity** — promoting N changed entries must be one DB transaction
  (FB/PG transactional; the existing apply already wraps DDL + blob in one TX).

## When to pull from backlog

- Whole-blob rewrite on every edit / save time becomes a measurable problem, **or**
- Designer-side lazy load is needed for very large configurations, **or**
- Per-entry change detection is wanted to replace the whole-blob MD5.

Until then the single blob + node-owned walk + (detached-root) atomic swap is sufficient and
simpler. The prerequisite — node-owned `SaveSubtree`/`LoadSubtree` — is already landed, so
this arc is unblocked whenever a trigger fires.
