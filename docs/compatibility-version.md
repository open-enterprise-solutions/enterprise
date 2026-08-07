# Compatibility version — the user's step, not our migration

A configuration **declares which version of the platform it expects to behave like**. The declaration
lives in the metadata, travels with it, and is meant to be read by any code whose behaviour has
changed since. Today it is groundwork: the value is stored, serialised and readable, and **nothing
branches on it yet**. This document exists so the first branch is written the way the mechanism was
designed, rather than beside it.

## The idea

When the platform ships to people who did not write it, "we changed how this works" stops being a
sentence anyone can say. Their configurations are already written; their data is already stored. The
answer is not to rewrite what they have — it is to keep the old behaviour available and let them
**raise the compatibility mode when they are ready**. The switch is theirs.

So: version 2 lands, the platform learns both behaviours, and a configuration that still says `1.0.1`
keeps getting the old one. The user opens the configuration properties, moves the mode up, tests, and
lives with the new behaviour from that point. Nobody's data is touched to make our release possible.

## Where it lives

| Piece | Location |
|---|---|
| The ladder | `backend/backend_core.h` — `enum ibProgramVersion`, built by `version_generate(major, minor, release)` = `major*1000 + minor*100 + release`. Rungs today: `version_oes_1_0_0`, `version_oes_1_0_1`, and `version_oes_last` aliasing the newest |
| The property | `metaCollection/metaObjectMetadata.h` — category **Compatibility**, property `Version` (`ibPropertyEnum<ibValueEnumVersion>`) on the configuration ROOT, defaulting to `version_oes_last` |
| The enumeration | `metaCollection/metaObjectMetadataEnum.h` — `ibValueEnumVersion`. The newest rung is labelled **"Don't use compatibility"**: not a version to pin to, but "behave as new as you are" |
| The read | `ibMetaData::GetVersion()` — pure virtual. A configuration answers with the root property; an external DataProcessor / Report keeps its own `m_version` and writes it into its file header (`metadataDataProcessor.cpp`, `r_u32` / `w_u32`) |
| The neighbour | `Syntax` (ves / ces) sits in the same category and works the same way — a configuration property that changes how the platform treats the configuration |

## Writing a branch

```cpp
// The configuration says what it expects; the code offers both.
if (metaData->GetVersion() < version_oes_1_1_0) {
    // what 1.0.x configurations were built against
} else {
    // the new behaviour
}
```

Two rules keep this honest:

**A rung per behaviour change, not per release.** The ladder is not a changelog. A rung earns its
place when some code has to answer "which way for this configuration?" — if nothing branches, nothing
is added.

**The default is the newest.** A configuration created today gets `version_oes_last` and never sees a
compatibility branch. The old rungs exist for configurations that were written when that rung was the
newest, and for nobody else.

## The branch reaches the SCHEMA, not only the code

This is the half that is easy to miss, and it is where the mechanism earns its keep.

The physical schema is not written by hand: a configuration contributes an `ibSchemaSnapshot`, and
`DiffSnapshots` turns baseline-vs-target into the steps restructuring executes. So a version gate
placed where the **target snapshot** is built decides what the base is allowed to grow:

```cpp
// Contributing the target schema — an old configuration does not declare the new table at all.
if (GetMetaData()->GetVersion() >= version_oes_1_1_0)
    snapshot.AddTable(...);        // absent for older configurations
```

An old configuration therefore produces a target without that table, the diff finds nothing to do,
and the base stays exactly as it was — no new table, no new column, no step in the log. Raise the
compatibility mode and the same code contributes it; the very next apply creates it. **The user's
switch is what turns the schema on**, which is what makes "raise the mode when you are ready" a real
promise rather than a label.

Two consequences worth stating, because they are features and not accidents:

* the same configuration file yields **different diffs** on different compatibility modes — that is
  the mechanism working, not a differ bug;
* a gate omitted here but present in the runtime code produces the worst pairing available: the code
  refuses to use a table restructuring has already created, or asks for one that was never made. Gate
  the schema and the behaviour **together**, in the same change.

## Raising the mode IS the migration

Put the two halves together and the user-facing story closes on itself. The mode is a **property of
the configuration**, so changing it is an ordinary metadata edit. Saving that edit runs the ordinary
apply: a fresh target snapshot — now built through the newer branches — is diffed against the
baseline, and the difference is exactly the tables, columns and seeded values the newer version
needs. The base migrates as the direct consequence of the switch.

Which means **there is no migration engine to write, and there must not be one.** The differ already
is it. A separate upgrade script per release would be a second mechanism describing the same
difference, and the two would drift the first time someone edited one of them.

What the release actually ships, then, is: a new rung, the branches that read it (schema *and*
behaviour), and — where the new shape cannot be derived from the old data alone — the value the
branch seeds. The user's part is one property and one apply, at a moment of their choosing, with
their existing configuration still working until then.

## Why this matters — the case that made it concrete

On 2026-08-07 a change to a *physical* constant escaped without any of this. `ibReference` lost its
trailing metaID (20 bytes → 16, the type moved to the `_RTRef` column), and the width of every
`_RRRef` column is derived from `sizeof(ibReference)`.

Tables created **before** the change kept their 20-byte columns, with the old metaID tag sitting in
the trailing bytes of every stored value. Tables created **after** it got 16. The running code builds
16-byte keys, the DBMS pads them with zeroes, and they match the new rows and never the old ones. In
one live base that meant rows which could be read and deleted (DELETE keys off `uuid`) but never
updated (UPDATE keys off `_RRRef`) and never placed in a hierarchy — invisible in the tree, silently
unwritable, and reporting "data was changed by another user" on the second save of an object nobody
else had touched.

The change had **no path into an existing base at all**: restructuring compares metadata, and no
metadata had changed — a C++ constant had. That is precisely the hole a declared version closes. A
format or behaviour already reflected in somebody's stored data needs a rung and a branch, not a hope
that everyone rebuilds their base.

See also: [restructure-plan.md](restructure-plan.md), [metadata-lifecycle.md](metadata-lifecycle.md).
