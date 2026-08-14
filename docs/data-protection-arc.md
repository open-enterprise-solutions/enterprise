# Data Protection — selective field encryption with a customer-held key

> **Status:** DESIGN — **no code yet** (2026-08-04 session). Every "already true" claim in § 2 was
> measured against the tree on that date, not recalled. Builds on the landed L1–L4 query stack
> ([query-language-arc.md](query-language-arc.md)), the L3 access door
> ([access-policy-rls.md](access-policy-rls.md)), the column codec
> (`query/columnLayout.h`), the job manager ([job-manager.md](job-manager.md)) and the journal
> ([audit-log.md](audit-log.md)). Nothing here proposes a new subsystem.
>
> **One sentence:** the operator marks which fields are sensitive; the platform encrypts them with a
> key the vendor never holds, warns **before** applying what the choice will cost, and keeps the
> runtime unaware that anything happened — because there is exactly **one** door to the database and
> exactly **one** place where a value crosses it.

---

## 1. Motivation — why this is a platform property, not a feature

Field-level encryption exists as a product elsewhere (Salesforce Shield, MongoDB CSFLE / Queryable
Encryption). In those systems it is a **bolt-on**: a layer added over a data path that was not built
for it, sold as a premium add-on, and paid for in broken reports discovered months after switching
it on.

Here it is not an addition. Three properties that already exist — and were built for other reasons —
make the whole feature a matter of admitting one more tenant into seams that are already load-bearing:

- **One door.** Measured 2026-08-04: the entire backend makes **4** direct L1 calls that bypass the
  L2 query layer, and all four are `CREATE OR REPLACE FUNCTION max_uuid/min_uuid` at PostgreSQL
  bring-up. **Zero** carry user data. There is no second path into the database, so there is no
  second path to guard.
  > Re-checked 2026-08-13: still four, and they have moved to where they belong —
  > `ibDatabaseLayerPostgres::CreateMissingRoutines()`, inside the driver, rather than written out in
  > `metaCollection`. The count is unchanged and so is the conclusion; what changed is that no tier
  > above L1 spells engine-specific SQL any more.
  >
  > ⚠ And one door the SCRIPT has is not counted here, because it is not the backend's: the
  > `DatabaseLayer` value type (`system/value/valueDatabaseLayer.cpp`) hands a configuration's own
  > code the raw driver. It is a deliberate hatch (owner, 2026-08-13 — kept, with the docs corrected
  > to say so), and any statement written through it is outside every guarantee on this page. Ordinary
  > script access is a manager, a query or LINQ; see [ai-context.md](ai-context.md) § 4.1.
- **One value crossing.** `ibColumnCodec::WriteValue` / `ReadValue` (`query/columnLayout.h`) is the
  single place an `ibValue` becomes physical fields and back. It is static and stateless, and it
  already exists — as the collapse of three duplicates ("the one home for the *magic* column spread
  that used to be duplicated across the attribute metaobject, the provider codec and the DDL
  builder").
- **Metadata is a first-class value.** Which field is personal data is a declaration on the
  attribute, so one bit routes classification, encryption, blind indexing, subject export, retention
  and the auditor's report — the same shape as `ibSchemaTable::m_derived` routing L3-2 / L3-3 / L3-4.

The consequence worth stating plainly: **the place where encryption goes was cleared years earlier,
for an unrelated reason.** That is the argument for the invariant discipline, not a claim about
cryptography.

---

## 2. What is already true — measured 2026-08-04

The point of this table is that most of the work is **already done and was done for other reasons**.

| Property | State | Why it matters here |
|---|---|---|
| Direct L1 calls bypassing L2 | **4**, none carrying data | one door is a fact, not a slogan |
| Reference key | pure guid v4, type in `_RTRef` | a stolen dump's links reveal **nothing** ([reference-key-metaid.md](reference-key-metaid.md)) |
| Physical table / column names | `%s%i` — prefix + metaID, `Field7` | names carry no meaning by construction |
| Whole configuration semantics | one blob in `sys_config` / `sys_config_save`, written through L2 (`ibUpsert`) | **one lever hides every name in the base** |
| Value crossing point | `ibColumnCodec::WriteValue` / `ReadValue`, static, stateless | two functions to instrument |
| Second value crossing | `BinaryToStatement` / `BinaryFromResult` in `query/dataMover.h` (L3-3 dump / restore) | **must be instrumented too** — see § 8 |
| Row-level enforcement | `ibRuntimeAccessPolicy` on the L3 door, cannot be bypassed | the neighbour this sits beside |
| Journal | `ibLogger`, per-month sink, retention sweep | the audit surface — needs a tamper-evident chain (§ 9) |
| Mass reversible operation under monopoly | `ibJobManager` + session `m_exclusive` | the re-encryption pass needs no new machinery |
| Password hashing | PBKDF2-HMAC-SHA256, 600k, PHC format | the crypto discipline already exists |
| Primitives in tree | SHA-256, PBKDF2 | AES-GCM / AES-SIV / HMAC-based blind index are **not** present yet |
| TLS on the web host | **absent** — cpp-httplib built without OpenSSL | must land first (§ 10) |

---

## 3. The rule — encrypt what identifies, never what measures

The single decision that keeps the engine alive.

| Encrypt | Never encrypt |
|---|---|
| names, personal names | **dates — all of them** |
| addresses, phones, e-mail | amounts, quantities, prices |
| tax / passport / account numbers | flags, statuses, enum values |
| free-text comments, notes | references (the guid is already blind) |
| attached files, scans | `sys_config` — encrypted, but whole and separately (§ 5) |

**Why dates are exempt, and it is not a compromise.** Register periodicity, slices, balances as of a
date, period reports and journal ordering all stand on the date. A date is not personal data by
itself; it becomes personal only paired with a *who*. Remove the *who* — encrypt the name — and the
date is inert. This is textbook pseudonymisation, and it is what keeps `SelectAggregate`, the totals
triggers and every period report working untouched.

The same reasoning exempts amounts: an amount identifies nobody, and encrypting it would kill
server-side aggregation, which is the point of L2.

This table answers **what** is protected. **How strongly** is a second, independent choice — the
four protection levels in § 6.1, each with its own searchability and its own leak. A field in the
left column still needs a level; a field in the right column is level 0 by definition.

---

## 3a. No DBMS-side encryption — the cipher is ours, on our own floor

The obvious shortcut is to let each engine encrypt itself: Firebird's crypt plugin, SQLCipher,
PostgreSQL's forks, MySQL's keyring. **Rejected, and the reasons are the same ones that shaped every
other floor in this tree.**

| | DBMS-side (TDE) | Platform-side (this arc) |
|---|---|---|
| Implementations to build and operate | **five** — one per driver, each with its own packaging, key handling, version behaviour | **one** |
| Protects the file at rest | yes | yes |
| Protects against a DBA on the **live** base | **no — he connects and reads everything** | **yes** |
| Where the key ends up | inside the database engine | **never leaves the platform process** |
| "Your hoster cannot read your data" | false | **true** |
| Portability | a fork by vendor, in the most sensitive place there is | none — the cipher never meets a driver |

The last row is the one that decides it. This tree's whole discipline is *five drivers, one
vocabulary, zero central switch by vendor* — a driver vends its dialect and nothing above it knows
which engine is underneath. Taking encryption to the DBMS would reintroduce exactly the
multiplication that L2's dialect dictionary exists to collapse, and it would do it in the subsystem
where a per-vendor divergence is least affordable.

**And the coverage argument runs the other way from what one expects.** Because the platform hands
the driver *ciphertext*, everything the engine subsequently writes carries ciphertext too — the
transaction log / WAL, temp files from sorting, index pages of the encrypted column, replication
streams. TDE would cover the same ground and nothing more, while leaving the live connection open.
Platform-side encryption is therefore **strictly stronger**, not a compromise for portability.

What stays outside either approach is identical in both, and is already listed in § 9: row counts,
volumes, timing, and the values of columns deliberately left in the clear (§ 3).

---

## 3b. The floor itself — where the cipher lives

Not a new subsystem: a **named seam** on the crossing that already exists, in the same spirit as
`ibBackendQueryProvider` being *"the whole L3↔L2-1 layer"*.

```
ibFieldCipher            Encrypt(bytes, ctx) / Decrypt(bytes, ctx)      — pure, no wx, no driver
ibCipherContext          which key version, which algorithm, which field
ibKeyRing                the session's keys — resolved beside GetAccessPolicy()
ibBlindIndex             HMAC(key, normalize(v)) and the trigram form   — § 6
```

Two insertion points, and **both are mandatory** — instrument one and the other leaks:

| Point | File | Carries |
|---|---|---|
| `ibColumnCodec::WriteValue` / `ReadValue` | `query/columnLayout.h` | every ordinary read and write |
| `BinaryToStatement` / `BinaryFromResult` | `query/dataMover.h` | dump / restore (L3-3) — *the artefact that actually gets stolen* |

The floor knows nothing about metadata, nothing about drivers, and nothing about queries. It is fed
bytes and a context; it returns bytes. Everything above decides, it executes — which is what lets it
be tested standalone and audited on its own.

---

## 4. Layering — L3 decides, the codec executes

The invariant that must survive: **L2 knows nothing about metadata.** That is why one L2 serves five
drivers and can be tested alone. Encryption must not be the thing that breaks it.

| Floor | Role |
|---|---|
| **metadata** | the bit on the attribute: *personal data*, and the operator's *enabled* flag |
| **L3** | **decides** — tags the column node in the IR; rewrites `WHERE name = 'x'` into a blind-index probe; adds the companion column to the DDL |
| **`ibColumnCodec`** | **executes** — cipher on bind, decipher on materialisation. Byte-to-byte, no branch on metadata |
| **L2** | sees the tag as *a field on a node*, exactly like `m_isBlob` — not as knowledge about metadata |
| **L1** | sees bytes |

This is the third tenant of a pattern that already carries two: `m_asExists` (raised by the RLS
decorator, executed blindly by the provider) and `PeriodTrunc` (decided at L3, spelled by the L2
dialect dictionary). The key is read off the session, through the same seam that already vends
`GetAccessPolicy()`.

**Order of transformations is load-bearing:**

```
write:  value → spread → compress → encrypt → bind
read:   read  → decrypt → decompress → assemble → value
```

Ciphertext is statistically indistinguishable from noise and does not compress. Compressing after
encrypting wastes both space and time; assembling before decrypting reconstructs references out of
garbage. When blob compression lands, it goes *before* the cipher, not bolted on top.

---

## 5. `sys_config` first — the cheapest lever in the whole arc

All semantics of a base live in one blob. Encrypt **only** that, and a stolen dump becomes a graph
of anonymous numbers: table `Reference42`, columns `Field7`, `Field12`, links to `Document17`. Even
with every value in the clear, the analyst cannot tell what the table *is*.

Cost: milliseconds once at start-up. Broken: nothing — no query slows, no aggregate changes, no
trigger cares. It goes through L2 already (`ibUpsert(config_save_table, …)`), so the same codec work
covers it.

**This is phase 1 and it stands alone**: it is worth doing even if field encryption is never built.

---

## 6. Search over ciphertext

The governing asymmetry: **a value crosses the boundary once and symmetrically; a condition does
not.** Write ciphertext, read plaintext — that is a transformation, and the codec does it. But
`WHERE name = 'Ромашка'` cannot be served by transforming the literal: the data cipher is
randomised, so encrypting the search term produces bytes that will never equal what is stored. Zero
rows, every time.

So a condition is not *transformed*, it is **rewritten** — onto a companion column. Everything below
follows from that.

### 6.1 Protection levels — the operator picks a level, not a checkbox

One bit cannot express the trade-off, because the trade-off is a range. Each level names its own
price, and the pre-flight warning (§ 10.1) reports what the choice costs on **this** configuration:

| Level | Cipher | What still works | What leaks |
|---|---|---|---|
| **0 — clear** | none | everything: sort, ranges, aggregates, `LIKE` | everything |
| **1 — deterministic** | AES-SIV | exact match, `JOIN`, ordinary index | **equality** — two rows visibly share a value |
| **2 — protected + index** | AES-GCM + blind index | exact match, substring, type-ahead | trigram frequency (tunable, § 6.6) |
| **3 — maximum** | AES-GCM, no companion | read and write only | nothing beyond presence |

Typical assignment: counterparty name → **2** (it is searched constantly); tax number → **1** (exact
lookups and joins); free-text note → **3** (nobody searches it); amount, date → **0** (§ 3).

### 6.2 Structure is never preserved — and the padding that replaces it

The tempting shortcut is to leave spaces and punctuation in the clear so search keeps working. It
must be refused, and the reason is concrete rather than doctrinal: preserved separators expose
**word lengths**.

```
"ТОВ Ромашка"   →  ███ ███████        3 + 7
"Іванов І.І."   →  ██████ █.█.        6 + initials
```

Counterparty and organisation names live in **public registries**. An attacker takes the registry,
computes the word-length pattern of every entry, and matches it against the column. Most patterns
are unique. That is an afternoon's work to recover the bulk of a counterparty catalogue — together
with its relations, because the guid graph is still intact. The class of data this arc exists to
protect is precisely the class for which this attack is easiest.

AES-GCM preserves length, so length leaks on its own. **Pad to a 32-byte multiple before
encrypting**: a few bytes per row, and the leak the shortcut was trying to trade away disappears
entirely — in the opposite direction, with no loss of strength.

### 6.3 The three query shapes

**Exact match — the majority of real traffic** (pick from a list by code, by tax number, by full
name). Companion column holds `HMAC(key, normalize(value))`, ordinary B-tree index, lookup cost
identical to an unencrypted column.

**Substring and type-ahead.** Trigram blind index: the value is cut into trigrams, each HMAC'd and
truncated. A user typing `ромаш` yields `ром, ома, маш`; rows carrying all three are selected, and
**only those candidates** are decrypted, with false positives filtered after. Candidates are few and
decryption is instant, so the user sees no difference.

Worth stating because it inverts the expected objection: **on substring search the encrypted column
is FASTER than the clear one.** `LIKE '%x%'` on plaintext is a full scan, always; a trigram index is
an index. Encryption does not slow this search down — it forces the proper search index that was
never built.

**Everything else — refused, not faked** (§ 6.5).

### 6.4 Where the rewrite happens

L3 knows the metadata, L2 must not. So L3 tags the column node in the IR with three fields — node
attributes, exactly like `m_isBlob`, not metadata:

```
m_encrypted    = true              cipher the value
m_indexColumn  = "name_bidx"       the companion (empty = no search possible)
m_indexKind    = Exact | Trigram   how to probe it
```

L2 then executes mechanically, and nothing above L2 knows any of this happened:

| Direction | L2 does |
|---|---|
| write a value | `value → cipher → bind` |
| read a value | `read → decipher → value` |
| `WHERE col = 'x'` | substitute the companion: `WHERE name_bidx = HMAC('x')` |
| `WHERE col LIKE '%x%'` | derive trigrams, build the `EXISTS` chain |
| `ORDER BY col`, `col > x`, `SUM(col)` | **throw** (§ 6.5) |

### 6.5 Refusal is the contract — and the precedent already stands on this floor

Alphabetical sort, range comparison, aggregates and joins over a randomised column **must raise
`UnsupportedNode`**, never silently operate on ciphertext. A sort by ciphertext returns rows in a
wrong order that looks plausible — the worst failure mode there is, because nobody reports it.

This is not new behaviour on L2. A driver without `RETURNING` throws rather than emulating with
write-then-SELECT, precisely because the emulation loses the property the feature existed for. Same
discipline, same floor.

Consequence for the operator: **keyset paging anchored on an encrypted column is impossible**, so a
list ordered by a protected name must be ordered by something else. This is what § 10.1 reports
*before* the switch is thrown, rather than leaving it to be discovered in production.

### 6.6 Normalisation — one function, or the feature breaks silently

`HMAC` is byte-sensitive: `"Ромашка"`, `"ромашка"` and `" Ромашка "` produce three different digests.
So both the write path and the search path run `normalize()` — case fold, trim, collapse internal
whitespace, `ё → е`.

It must be **one function with two callers**. If the two ever diverge, search stops finding *some*
records and keeps finding others, and that symptom is close to undiagnosable. Guard: a round-trip
test that writes through the codec and finds through the search path, over a set built from case
variants, padded whitespace and `ё`.

### 6.7 Trigram storage — two shapes, and the mechanism already exists

| | Shape | For | Against |
|---|---|---|---|
| **A** | one companion column, digests concatenated: `" a1b2 c3d4 … "`, probed with `LIKE '%a1b2%' AND …` | no new table, days of work | scan over a short column — fine into the hundreds of thousands of rows |
| **B** | a table `(rowkey, digest)` with an index, probed by `EXISTS` per trigram | indexed, scales | N rows written per value |

Ship **A** first, move to **B** when volume demands; the L3 surface is identical either way.

And B costs less than it looks: the `EXISTS` semi-join it needs is **already built** — the
`m_asExists` tag raised by the RLS decorator and executed blindly by the provider. Trigram search
becomes its third tenant, alongside RLS reads and the RAM stitch's key reduction.

**The truncation width is the privacy dial.** 4 bytes → almost no false candidates, more frequency
signal; 2 bytes → many collisions the platform filters after decryption, and almost nothing to
analyse. It is set by measurement, not by guess — and it is the one number in this arc that is a
genuine policy choice rather than a technical one.

### 6.8 Declared length is not stored width — and the spread already says so

`String(20)` is a **semantic** declaration: twenty characters is what the user may type, what
validation enforces, what the form shows. It has never been a statement about bytes in a table — a
reference is declared as one attribute and stored as **two** physical fields (`_RRRef` guid +
`_RTRef` clsid). Encryption is the same decoration, applied to a different attribute.

The mechanism is already the single authority: `DescribeColumnLayout` decomposes one logical column
into `ibColumnSlot`s, each with a role, a canonical type and a suffix drawn from the one
`ibFieldSuffix(role)` map. Encryption therefore adds **no new concept** — it changes what the spread
produces:

| | Cleartext spread | Encrypted spread |
|---|---|---|
| value slot | `_S`, `ibCanonicalKind::String(20)` | `_S`, **`Binary(computed)`** |
| companion | — | **`_BIX`, `Binary(8)`** — new role `BlindIndex` |

**The stored width, computed and deterministic:**

```
width = 2 (algo + key version) + 12 (nonce) + pad32(max_bytes(declared_length)) + 16 (tag)
```

| Declared | UTF-8 worst case | Padded | Stored | Inflation |
|---|---:|---:|---:|---:|
| `String(120)` | 480 | 480 | **510** | ×1.06 |
| `String(10)` | 40 | 64 | **94** | ×2.4 |
| `String(2)` | 8 | 32 | **62** | ×7.75 |

The fixed 30 bytes vanish on a long field and dominate a short one. In absolute terms it is nothing;
what matters is that the width is *computed*, so the DDL is exact rather than guessed.

**An unbounded string is the easy case, not the hard one.** It is already `ibCanonicalKind::Blob`,
a Blob has no declared width, so `ibEncryptedWidth` is never called: encrypt the content, store it
in the same Blob. Nothing to translate.

**The binding limit is the ROW, not the column.** A `String(1024)` becomes 4 126 bytes, which fits
any engine's binary column — but fifteen such fields do not fit a *record*: Firebird caps a record
near 64 KB and MySQL at 65 535 bytes regardless of types (PostgreSQL escapes via TOAST). Worse, the
failure surfaces unevenly — at `CREATE` on one engine, at the first `INSERT` on another, i.e. after
a restructuring that appeared to succeed.

So the check is **per table, not per column**: sum every slot's width and compare against
`m_maxRowWidth`, a third tenant of the dialect dictionary beside `m_maxBinaryWidth` /
`m_maxIndexWidth`. And it is a mandatory pre-flight line — *"after enabling, a record of table X
would be 71 KB against a 64 KB limit; these fields must move to Blob."*

**Where the `Binary` → `Blob` threshold sits, and why not lower.** A Blob is not free: Firebird
stores it in a separate segment, so reading one is an extra operation — noticeably more expensive
for a field shown in a list. Hence: **`Binary` while the record budget allows, `Blob` when it does
not, or when no length was declared.** That decision needs the whole slot set of the table, so it
belongs in lowering, not in the width function. Search is indifferent either way — the companion is
8 fixed bytes and the ciphertext is never indexed.

**Megabyte values are chunked.** For attachments and scans, one GCM seal over the whole value forces
the entire plaintext into memory and forbids partial reads. Encrypt in 64 KB blocks, each with its
own nonce and the block index bound as associated data so blocks cannot be reordered or spliced.

**And an honesty the padding cannot buy.** Padding to 32 bytes makes "2 characters" and "10
characters" indistinguishable — that is what defeats the word-length attack of § 6.2. It does
nothing comparable for a 900-byte comment, where ±32 bytes still separates a short note from a long
one. Hiding that would need power-of-two buckets and a real space cost. It is not worth it: for long
fields the length joins the residual leak of § 9, alongside row counts and timing. The leak that
mattered — the pattern of word lengths in a name — is closed; the leak that remains says only how
much someone wrote.

**Where the translation lives, and the three rules it must obey.** The mapping *declared → stored* is
one pure function beside the spread — `ibEncryptedWidth(declaredLength, level, algoVersion)` — and
nothing else in the tree may compute a width:

- **Deterministic, or restructuring never converges.** Same inputs, same number, forever. If the
  width is computed even slightly differently on a second run, `DiffSnapshots` sees a type change
  where nothing changed and emits an ALTER on every apply — a table that rebuilds itself for eternity.
- **The algorithm version comes from the SCHEMA, not from the running code.** Store it in the column
  snapshot and feed *that* to the width function. Otherwise the day a cipher parameter changes
  (a different nonce size, a different tag), a platform upgrade silently recomputes every width and
  turns a routine update into a full-database restructuring for every customer at once. Migrating to
  a new algorithm version then becomes what it should be: an explicit, per-column, scheduled change.
- **Capacity is checked on write, never truncated.** Widths are computed from the worst case (4 bytes
  per character), so a real value always fits — but the codec still verifies and raises a readable
  error rather than storing a value it cannot return. On read, plaintext longer than the declared
  length is a corruption / wrong-key signal, and a cheap sanity check.

L2 sees none of this. It receives an `ibDdlColumn` carrying `Binary(510)` and renders it through the
dialect's TYPE-MAP, exactly as it renders any other canonical type — that a `String(120)` stands
behind it is a metadata question, answered above and never asked below.

Three consequences, each of which bites if missed:

- **The declared length stays the user-facing contract.** Validation, truncation, form width and
  script semantics all continue to speak of 20 characters. Nothing above the spread may ever read
  the physical width — the same discipline that keeps a reference one attribute rather than two.
- **The companion slot appends at the END of the spread.** `columnLayout.h` states the field order
  is byte-identical to the historical one and **load-bearing** — the keyset anchor must match the
  first `ORDER BY` field. Inserting a slot in the middle silently moves that anchor.
- **Flipping the bit is an ordinary schema change.** The spread changes → the snapshot changes →
  `DiffSnapshots` emits a type ALTER plus an ADD COLUMN plus an index, exactly as it would for a
  retyped attribute. No separate path, and the client-side re-encryption pass (§ 11.3) is the data
  half of the same delta.

**Width limits belong to the dialect, not to an `#ifdef`.** Firebird caps `VARBINARY` near 32 765
bytes and an index key at about a quarter page; MySQL allows 65 535 but only 3 072 bytes of index
key; PostgreSQL's `bytea` is unbounded. `ibDialectDictionary` vends `m_maxBinaryWidth` /
`m_maxIndexWidth`, and lowering refuses at apply time with a readable message instead of failing on
the first `INSERT`. One more tenant in the dictionary that exists for precisely this class of
question.

And the width question never reaches the index: **ciphertext is never indexed**, the companion is —
and a truncated HMAC is the same 8 bytes whether the field was declared as 2 characters or 120.

### 6.9 Low cardinality defeats the blind index — a property of the data, not of the cipher

A short field usually means a small value space: a region code, an operation type, a sex, a status.
The cipher stays sound and the HMAC stays uncomputable without the key — and the protection is still
worthless, because the companion column will hold **a handful of distinct digests with a recognisable
distribution**. Sex → two digests at roughly 50/50. Region code → twenty digests whose frequencies
match public statistics. The attacker does not break anything; he labels the digests by matching
distributions, in minutes.

No stronger cipher fixes this. It is a property of the data.

| Distinct values | Levels allowed |
|---|---|
| high (names, addresses, numbers) | 1 / 2 / 3 |
| **low (under ~1000)** | **3 only** (no companion) or **0** |

The platform can decide this itself — `COUNT(DISTINCT …)` over the existing rows — and report it in
the same pre-flight warning (§ 10.1):

> *"Attribute «Region code»: 24 distinct values across 180 000 rows. A search index on such a field
> exposes its distribution — only level 3 (no search) or level 0 is available here."*

This is the pre-flight warning doing something no competitor's does: warning not that a **function**
will be lost, but that the **protection would be illusory**. Allowing the switch here would ship a
false sense of security, which is worse than shipping none.

---

## 7. Keys — envelope, escrow, rotation

```
data        ← AES-GCM, symmetric, fast, with the DEK
DEK         ← wrapped with the customer's PUBLIC key
            ← and with a SECOND public key — escrow
private key ← the customer's: file, token, smart card, HSM, external KMS
```

- **The vendor never holds a key.** This is the answer to *"can you read our data?"* and it is worth
  more in a sales conversation than the cipher choice.
- **Escrow is not optional.** The customer *will* lose the key — not *may*. Without a recovery path
  the first such case is a destroyed database and a destroyed reputation. Shamir split, k-of-n.
- **Rotation is two-level.** Rotating the master re-wraps the DEK — milliseconds, no data touched.
  Rotating the DEK is a background re-encryption pass, which the job manager already knows how to
  run, resumably, under monopoly.
- **Every ciphertext carries its key version.** Without it, rotating a DEK means re-encrypting the
  entire base in one atomic step — impossible on any real volume, and impossible at all for data
  already sitting in a backup or an archive. With a version byte in the ciphertext header, old and
  new coexist: reads resolve the right key per row, the re-encryption pass converts in the
  background at its own pace, and a restored 5-year-old backup still decrypts. The key ring
  therefore holds *retired* keys as well as the current one, and retiring a key for real (removing
  the last reader) becomes a deliberate, separately confirmed act — the same rule as § 11.1.
  **This is what makes the difference between a feature that survives a decade of operation and one
  that works until the first rotation.**
- **Backups taken before the switch stay in the clear.** Nobody remembers this and it is where the
  leak actually happens. The procedure must be part of the feature, not of the manual.

### 7.1 Key custody — the vendor holds nothing, and that protects both sides

**No copy of any customer key exists on the vendor's side. Not the master, not the escrow, not "for
support".** This is an architectural commitment, not an operational policy, and it buys three things
at once:

- **For the customer** — *"nobody but you can read this"* becomes literally true rather than
  contractually true. A hoster, a cloud operator, a subcontracted administrator and the vendor are
  all in the same position: without the key there is nothing to read.
- **For the vendor** — what does not exist cannot be requested, produced by mistake, or lost. The
  vendor is structurally outside any dispute over a customer's data, in a way no confidentiality
  clause can achieve.
- **For escrow** — it belongs to the **customer**, split among the customer's own people (Shamir
  k-of-n, § 7). Vendor-held escrow would silently undo the other two points.

**The boundary, stated plainly and kept identical in the product, the documentation and the
contract.** Encryption protects against *unauthorised* access — theft of hardware or backups, a
compromised host, an insider, an intercepted transfer. It does not, and is not offered as a way to,
displace *lawful* access: where a key holder is legally required to provide access, the key holder
provides it, and the platform neither prevents that nor is described as preventing it.

One text everywhere, and it is descriptive: **"the data is unreadable without the key, and the key
is yours."** Enumerating scenarios is what turns a neutral mechanism into a claimed purpose — so the
product describes the property and leaves the applications to the customer's own judgement and
counsel.

---

## 8. Invariants — each one a guard, not a paragraph

Every rule below is silent when broken. Each therefore ships as a test, in the tradition of
[portability.md § 1.10](portability.md) — fix the root, then sweep.

| Invariant | Guard |
|---|---|
| **An encrypted column is never a materialisation source.** A trigger cannot sum two ciphertexts, and the totals bundle runs inside the DB, past every layer | walk `ContributeTables`, take every `m_derived` table's spec, assert no referenced field carries the encryption bit |
| **An encrypted column never appears in `SelectAggregate`, nor as a keyset anchor** | same walk, extended |
| **A dump of a protected base contains no plaintext.** L3-3 uses its own byte-identical codec (`dataMover.h`) — instrumenting only `ibColumnCodec` leaves the most stolen artefact in the clear | dump a base with protection on, assert no source string appears in the stream |
| **Compression precedes encryption** | round-trip test with both enabled |
| **The codec decides nothing.** No `if` about classification inside it; the tag arrives from L3 | review rule; a grep guard for metadata includes in the codec TU |
| **Disabling protection is a first-class journal event** | logger test |

---

## 9. Boundaries — stated first, by us

The claim must never be "your database is protected". It must be exact, and it must name its own
limits — a solution that describes its boundaries reads as competent; one that promises everything
reads as a sales pitch.

**Protects against:** a stolen database file, dump, disk or backup · a compromised hosting provider
or cloud · a curious or bribed DBA · seizure of hardware · access to a replica.

**Does not protect against:** a compromised application server (the key is necessarily in memory
there) · an insider with legitimate rights inside the platform (that is what RLS and the access
journal are for, not encryption) · leakage through exports and reports (DLP and rights).

**Residual metadata leak, present in every solution of this class:** row counts, table count, the
link graph over blind guids, timing and volume distribution. Unremovable. Say it first.

**Journal gap:** [audit-log.md](audit-log.md) states honestly that it is *"not a security audit trail
in the SOX/GDPR sense yet — no tamper-evident chain"*. Read access to personal data is not logged at
all today. Both are prerequisites for the auditor's report in § 10, not optional polish.

---

## 10. The two things nobody else has

Both are possible **only** because metadata is a first-class value and every query passes one door.

**10.1 Pre-flight warning.** The administrator ticks a field, and *before* anything is applied the
platform reports what the choice costs — computed from the metadata, not guessed:

```
Attribute "Name" is used in: 4 dynamic-list filters, the default sort of catalog
"Counterparties", 2 reports, 1 register total.

After enabling: search preserved (blind index) · ALPHABETICAL SORT UNAVAILABLE ·
report "Contract register" loses its grouping by name · register total unaffected.

Proceed?
```

Elsewhere you enable and then discover the breakage over the following months. This is the
difference between a mechanism and a mechanism that protects its operator, and it demonstrates in
ten seconds.

**10.2 The auditor's report, one button.** Not the mechanism — **the proof**: which fields are
protected, since when, enabled by whom, under which key version, when it was last rotated, who ever
removed protection and when. This is what a compliance officer assembles by hand over weeks and
still cannot certify.

State the priority plainly: **proof sells for more than mechanism.** Encryption everyone has;
demonstrable compliance almost nobody does, and the audit is what is actually being paid for.

---

## 11. Who controls what — two levels

| Level | Who | Decides |
|---|---|---|
| **Configuration** | the configuration's author | marks attributes as *personal data* — he knows that a tax number is a tax number |
| **Enterprise runtime** | the customer's security administrator | **picks a level** (§ 6.1) per marked attribute — he knows his own risk profile and his regulator, and the pre-flight warning tells him what each level costs here |

Neither can do the other's job. Plus, or the first auditor question lands in a void:

- a **separate right** for managing protection, distinct from *administrator*;
- every enable / disable in the journal, disable at the highest severity;
- four-eyes confirmation on **removal** — it rides on the existing role machinery.

### 11.1 The switch works both ways — and that is a feature, not a convenience

Data stops being sensitive. A contract expires, a counterparty is archived, a regulation's retention
window closes. The operator must be able to say *"this no longer needs protecting"* and take the
field back into the clear. Two reasons it belongs in the design rather than in a later wish-list:

- **It is a compliance argument, not an ergonomic one.** Storage limitation is a GDPR principle in
  its own right; a system that can only ever add protection cannot express a data lifecycle.
- **It is what makes the feature safe to try.** An irreversible switch is one nobody dares throw. The
  same logic as rollback for a configuration change: the ability to return is what makes the ability
  to change usable.

**But the operation is not symmetric, and the asymmetries are where data is lost:**

| | Enable | Disable |
|---|---|---|
| Data pass | plaintext → ciphertext | ciphertext → plaintext |
| Needs the key | to write | **to read — without it the data is gone, permanently** |
| Blind indexes | built | **dropped**; ordinary indexes and sorting restored |
| Existing backups | earlier ones stay **in the clear** — must be re-taken and the old ones destroyed | earlier ones stay **encrypted** — so **the key may not be destroyed** after disabling, or every historical backup dies with it |
| Journal severity | notable | **highest** — this is protection being removed |

The backup row is the one that bites. Enabling and disabling both leave a set of backups whose
readability depends on a decision made afterwards, and in opposite directions. The rule that falls
out: **key retirement is a separate, deliberate act with its own confirmation — never a side effect
of switching protection off.**

### 11.2 Progressive disclosure — and the gate behind it

The switch stays hidden until the feature is configured: no key, no menu, no checkbox on the
attribute. This is good UX, and it is also the safety gate that matters most.

**Encryption must be unreachable until a key is configured AND its escrow copy is confirmed.** The
failure it prevents is total and irreversible: a base encrypted with a key nobody has is not a
damaged base, it is a destroyed one. Every other guard in § 8 protects against a wrong result; this
one protects against there being no result at all.

Order of the gate, each step unlocking the next:

```
key source configured  →  escrow copy taken and verified  →  protection menu appears
                       →  attributes can be marked        →  pre-flight warning (§ 10.1)
                       →  apply, under monopoly, resumable
```

---

### 11.3 Applying the change is a restructuring pass — with one exception that is forced

The flag is not a setting beside the schema; it **belongs to the schema**. Put it in
`ibSchemaColumn`, and `DiffSnapshots` sees a flipped bit exactly as it sees a retyped column: a
matched id whose declaration changed, i.e. an ALTER. Nothing new triggers the work — the existing
config-apply path does, and it already runs under monopoly, which removes the "half the rows are
converted, what do concurrent readers see" question by construction.

The companion columns follow the same route: enabling adds the blind-index column and its index
(DDL), disabling drops them. Both are ordinary snapshot deltas.

**The exception — and it is forced by the decision in § 3a.** Every other data-shaping delta this
tree performs runs *server-side*: `ibSchemaDelta::m_regenExpr` exists precisely so a rebuild is an L3
aggregate the database executes. A re-encryption pass **cannot** be expressed that way, because the
key is deliberately not in the database. So this one delta is a **client-side read-modify-write**:

```
for each chunk (keyset-ordered, N rows):
    read chunk        → codec DECRYPTS with the old state
    write chunk back  → codec ENCRYPTS with the new state (or writes plaintext, on disable)
    commit chunk      → persist the progress cursor
```

Consequences to design for, not to discover:

- **Chunked, never one transaction.** A single TX over a large table means version bloat, an
  hours-long rollback risk, and a restart from zero — the long-transaction trap this tree already
  knows. Chunk + commit + a persisted cursor; the job manager is already resumable.
- **A third column state is required.** `Plain / Encrypting / Encrypted / Decrypting` — because a
  pass that dies midway leaves rows in both forms, and a restart with only a boolean cannot tell
  which is which. The cursor says where it stopped; the state says what the two sides of it mean.
- **The key is verified before the first row, not at the first failure.** Disabling protection
  without the key is not a slow operation, it is an impossible one.
- **It costs client-server round trips.** A million rows is minutes to hours, not seconds — so the
  operation announces an estimate up front and can be resumed, not merely restarted. This is the
  honest price of not handing the key to the database, and it is worth stating in the same breath
  as the benefit.

---

## 12. Build order

| Phase | What | Depends on |
|---|---|---|
| **0** | TLS on the web host; SBOM + CVE watch over the vendored clients | nothing — prerequisites for any security conversation |
| **1** | **The cipher floor** (§ 3b): `ibFieldCipher` + AES-GCM + context, standalone-tested — **and `sys_config` as its first tenant** (§ 5) | nothing above L1 |
| **2** | Key hierarchy + escrow + rotation (§ 7) | must precede any customer enabling anything |
| **3** | The classification bit in `ibSchemaColumn` + the codec tenant (both insertion points) + the L3 tag + the restructuring delta (§ 4, § 11.3) | phases 1–2 |
| **4** | Blind index, exact then n-gram (§ 6) | phase 3 |
| **5** | Pre-flight warning + auditor's report (§ 10) | phase 3; journal chain from § 9 |
| **6** | Crypto-shredding (per-subject key) → GDPR erasure without breaking referential integrity | phases 2–3 |

**Phase 1 alone already delivers the headline claim** — *"without the key, a dump carries no name of
any table, attribute or object"* — because the physical names are numeric already (§ 2) and all
semantics live in the one blob. It costs one primitive and two call sites, breaks nothing, slows
nothing, and is worth shipping even if no field is ever encrypted.

Everything after phase 1 buys resistance to an insider with live database access, and is paid for in
functionality (§ 3, § 6) and in conversion time (§ 11.3).

---

## 13. The demonstration — and what it specifies

The whole arc reduces to one scene, and the scene is worth writing down because it **defines the
minimum slice**: whatever it needs must be built first, whatever it does not can wait.

**The scene, in two windows side by side — not in sequence.**

| Left: the application | Right: pgAdmin / IBExpert on the same base |
|---|---|
| attach the key, tick the sensitive fields, press *Apply* | |
| restructuring runs, reports what it did | |
| work as usual: open the counterparty list, **type part of a name — it finds it**, open a document, run a period report | the same table: `Reference42`, columns `Field7`, `Field12`, rows of noise |
| nothing looks different | **and the table list itself says nothing** — no object name anywhere in the schema |

**The strongest beat is not the ciphertext.** That a protected field is unreadable is what a viewer
expects. What is not expected is the second half: *he cannot tell what the table is.* Physical names
are numeric already (§ 2) and every name lives in the one encrypted blob (§ 5), so the schema itself
stops being a map. Show the table list, not just a cell.

**The second strongest beat is the search working.** A viewer's first silent objection is "fine, but
now nothing works". Typing three letters into a list and getting the row back answers it before it
is asked (§ 6.3) — and it is worth saying out loud that this search is *faster* than the plaintext
one it replaced.

**Three things to get right, or the scene breaks on the first question:**

1. **Pick fields for which "nothing happened" is literally true** — or, better, tick one that *does*
   have a consequence and let the pre-flight warning (§ 10.1) report it. A tool that says *"this
   list can no longer sort alphabetically"* before doing anything reads as trustworthy in a way no
   flawless demo does.
2. **Show the reverse.** Untick, apply, and the field comes back to the clear (§ 11.1). The ability
   to return is what makes the ability to protect usable, and a viewer who sees it stops treating
   the switch as a one-way door.
3. **Do not say "your database is protected".** Say what is protected and what is not (§ 9), in the
   same breath. The scene is strong enough to survive its own boundaries, and naming them is what
   makes the rest believable.

**What the scene therefore requires:** phases 1–4 (the cipher floor + `sys_config`, keys, the bit
and the codec tenant with its restructuring pass, and the blind index). Phase 5 (pre-flight, auditor
report) is not required but roughly doubles the effect. Phase 6 (crypto-shredding) belongs to a
different conversation — a compliance one, not this scene.

---

## 14. Open questions

- **Where does the key live during a session?** On the session beside the access policy is the
  obvious answer; the exposure window and what a crash dump can contain are not analysed yet.
- **Blind-index truncation width** — the leak/false-positive trade-off has no measured basis yet.
- **Enabling on a large live base** — the client-side pass (§ 11.3) needs a measured throughput
  before any estimate can be shown to an operator. A base that takes a working day to convert needs
  a different story than one that takes a minute.
- **Chunk size and its interaction with keyset paging** — the pass reuses the existing keyset order;
  whether an encrypted column may ever be the anchor is already answered (§ 8), but the interaction
  of a mid-pass cursor with a concurrent restore is not.
- **Crypto-shredding vs the audit journal** — a shredded subject's name in a journal row is still a
  name. Journal rows may need per-subject keys too.
- **Interaction with configuration compare/merge** — a protected base's configuration blob is
  encrypted; comparing two configurations must decrypt both, and it is not obvious where.
