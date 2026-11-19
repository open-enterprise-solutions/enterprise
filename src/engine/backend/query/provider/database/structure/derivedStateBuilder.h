#ifndef __DERIVED_STATE_BUILDER_H__
#define __DERIVED_STATE_BUILDER_H__

// L3-4 — the REGENERATION floor. Rebuilds a derived table (a register's totals) from the source it
// is derived from, for the moments a trigger cannot have covered.
//
// It is NOT a fourth query primitive and NOT a cadence. It COMPOSES the floors below: read the
// source aggregated through the L3-1 door, write the result back through the same door's write
// core. The three actors stay distinct, and conflating them is the seam that rusts:
//
//   trigger  — UPDATES, per movement, in the DB, transaction-atomic, unbypassable, never invoked.
//              Steady state, and the only thing that maintains totals.
//   L3-4     — REGENERATES, on discrete events, invoked. Never per movement.
//   L3-2     — BUILDS the structure (table + triggers + view).
//
// The moment regeneration runs per movement, the drift-proof property is gone and we are back to
// bypassable managed-code totals.
//
// WHEN IT RUNS. Full rebuild on a FULL LOAD — restore, migration, a first Apply over a base that
// already holds movements — because there the totals start empty while the source does not. An
// empty totals table is not a neutral state: it reads as "no stock of anything", which is a
// plausible-looking wrong answer. Otherwise regeneration is decided by what actually changed
// (NeedsRegeneration): most structure changes do not need it, and on a large register a needless
// rebuild is minutes of exclusive Apply window spent reproducing correct numbers.
//
// (docs/private/register-totals-strategy.md § Engine integration)

#include "backend/backend.h"

struct ibSchemaTable;
class ibSchemaSnapshot;
class ibDatabaseConnectionHolder;
class ibRestructureInfo;
class ibValue;

namespace ibDerivedState {

// Rebuild ONE derived table from its source. Clears it, reads the source grouped by the declared
// key with the declared accumulations, writes the result back.
//
// The aggregate runs SERVER-side: what crosses to the client is one row per KEY, not per movement,
// so the cost scales with the number of distinct (period, dimensions) combinations rather than
// with history. That is what makes this read-then-write shape viable and leaves a server-side
// INSERT…SELECT as a later optimisation rather than a prerequisite.
//
// A driver with no materialization dialect is a no-op success: nothing was ever materialised, so
// there is nothing to rebuild — its registers read from live aggregation.
BACKEND_API bool Regenerate(const ibSchemaTable& derived, ibDatabaseConnectionHolder* holder = nullptr);

// Every derived table in the snapshot — the FULL LOAD path. Returns how many were rebuilt, -1 on
// failure.
BACKEND_API int RegenerateAll(const ibSchemaSnapshot& target, ibDatabaseConnectionHolder* holder = nullptr,
                              ibRestructureInfo* report = nullptr);

// Fold a SPLIT table's shards back into one row per key — the cheap counterpart of Regenerate.
//
// A split totals table spreads one logical key across N physical rows so concurrent writers stop
// queueing on it (docs/private/register-totals-strategy.md §6). The read sums them, so the answer is exact
// at any distribution — but it sums N rows forever, and that read tax is what keeps the split from
// being a sane DEFAULT. Collapsing removes the tax for every period nobody writes to any more: a
// elapsed period folds to one row and stays there, so the split is paid for only where it is
// earning something.
//
// It is NOT a rebuild and must not be confused with one. Regenerate reads the MOVEMENTS and
// recomputes the figures — a full scan of history. This reads the TOTALS TABLE and only re-packs
// what is already there: same numbers, fewer rows, source never touched. That is why it is cheap
// enough to run on a schedule Regenerate could never afford.
//
// TAKES NO BOUNDARY, deliberately. It folds, per stored unit, the period that closed LAST — yesterday,
// last month, last year — and works it out itself from the unit and the clock. A period splits while it is
// current; the first pass after it closes folds it, and later passes find it folded. So a pass costs one
// period per unit and never the history behind it, there is no "how far have we folded" state to keep
// anywhere, and a caller says "fold", not "fold up to here". A posting dated back into an older period may
// split a key there again: it reads exactly right, one row wider, and the recompute packs it. A table with
// no period dimension has no frontier and is folded whole.
//
// THE DATABASE FOLDS, one SLICE (a unit and a period) in one MERGE (ibFoldShards): nothing is read into the
// process, so a slice of any size costs the engine's work and no memory here. The fold that bucketed every
// (key, shard) in a map died at 3.5 GB on four million rows (2026-11-16).
//
// SAFE UNDER LIVE WRITERS — each slice is ONE SNAPSHOT TRANSACTION THAT NEVER WAITS. A row a writer changed
// meanwhile, or holds, is a conflict, and the slice rolls back and folds on the next pass; a row a writer
// inserted meanwhile is invisible and stays. So the cleanup always yields: a
// posting waits on it at most one slice long, and it never waits on a posting. Nothing here needs a quiet
// range or an exclusive window — which is what makes it runnable in the background while people work. Two
// folds on one table meet the same way, as a conflict on the second.
//
// A caller that already has a transaction open keeps its own boundary: nested scopes collapse onto one.
//
// What the caller still owes is DOSAGE — this reads and writes the same database everyone else is
// using. Pace the passes; each slice is self-contained, so stopping between them is always safe.
//
// Not running it at all is also safe: shards read exactly right at any distribution, so a skipped
// fold costs a slightly wider read and nothing else. This is housekeeping, never correctness.
// The HOLDER IS REQUIRED — no default, and null does nothing. It is what says whose connection this
// runs on, and a background job has no ambient answer to that: it may hold no session at all, and
// falling back to "the calling thread's" would silently borrow somebody else's. The job is handed a
// holder by whoever started it and passes it down; that is the only context this floor has or wants.
BACKEND_API bool Collapse(const ibSchemaTable& derived, ibDatabaseConnectionHolder* holder);

// THE ENTRY POINT a scheduled task calls: fold every split totals table in `target`. Returns how
// many were folded, -1 on failure; an unsplit table is skipped, not an error.
//
// The SCHEMA COMES IN — this floor does not go looking for it. It is metadata-blind by construction
// (its sibling Regenerate takes a ready ibSchemaTable for the same reason), and for a background job
// "the active application" is not even a well-defined thing: a web process holds many sessions and
// a cluster many nodes, so whose application is current at tick time has no answer. The caller owns
// that context and hands down the ContributeTables snapshot built from it.
//
// It DOES own the transactions — one per table, so an interrupted run leaves whole tables done and
// the rest simply not yet done, never a table half-folded. Safe to run while people work, safe to run
// twice, safe never to run: folding is housekeeping, and a table that missed a pass is a slightly
// wider read and nothing else.
BACKEND_API int CollapseAll(const ibSchemaSnapshot& target, ibDatabaseConnectionHolder* holder,
                            ibRestructureInfo* report = nullptr);

// Check the trigger against the arithmetic: re-aggregate the LAST ELAPSED period from the movements
// and compare it, key by key, with what the totals table holds for it. Returns the number of keys
// that disagree — 0 means the two paths produced identical figures — or -1 if the check could not run.
//
// This is the parity question the whole design rests on, asked as a routine rather than as a one-off
// test. The trigger maintains totals in the same transaction as the movement, so they CANNOT drift by
// themselves; what this catches is a write that reached the movements WITHOUT the trigger — a dropped
// or half-installed bundle, a restore, a bulk load, direct SQL against the base. Those are the only
// ways the two sides can part, and they are all events rather than accumulation.
//
// ONE PERIOD, not the history, and that is what makes it affordable to run regularly. Verifying
// everything costs exactly what rebuilding everything costs — the same full scan and the same
// grouping — so a full check is never the cheap half of a decision, it IS the expensive half. A
// single elapsed period is a day's worth of movements, and a discrepancy anywhere usually shows up in
// the recent ones first.
//
// A table that keeps several units checks each coarser one too: its last elapsed period against the stored
// grain's rows of the same span — the rows it is the sum of, not the movements again.
//
// A non-zero answer does NOT say what to do: the fix is Regenerate, but deciding to run it against a
// large register is a scheduling question, not this function's. It reports; the caller decides.
BACKEND_API int VerifyLastPeriod(const ibSchemaTable& derived, ibDatabaseConnectionHolder* holder);

// What one maintenance pass did — for the job's journal and for a status panel.
struct ibTotalsMaintenance
{
	int  m_checked    = 0;   // derived tables examined
	int  m_folded     = 0;   // tables whose shards were folded
	int  m_mismatched = 0;   // KEYS that disagreed between the two paths (0 = the trigger is honest)
	bool m_failed     = false;
};

// THE ONE CALL A SCHEDULED JOB MAKES — no arguments beyond its context, no modes, no policy.
// Everything needed to keep an application's totals honest and compact, in the right order,
// deciding per table what it needs. Call it and forget it.
//
// Per derived table:
//   1. VERIFY the last elapsed period — cheap (one period's movements), and first, because a fold
//      over figures nobody checked is just tidier wrong numbers.
//   2. FOLD the shards — quiet, per-key transactions, safe beside live writers.
//
// NO REBUILD HERE, deliberately. Regenerating a register is a full scan of its history — minutes to
// hours — and nothing that long should start by itself while people work. It belongs to the
// Designer's "recompute totals" command, where an administrator chooses the moment and watches it
// finish (RegenerateAll is that command's body).
//
// So this pass never fixes a disagreement; it REPORTS one, in m_mismatched. That separation is the
// design, not a limitation: the quiet half runs unattended precisely because the loud half is not
// hiding inside it, and a non-zero count is exactly the signal that sends someone to the Designer.
BACKEND_API ibTotalsMaintenance MaintainTotals(const ibSchemaSnapshot& target,
                                               ibDatabaseConnectionHolder* holder);

// Does this change require a rebuild? `old` null = the table is new.
//
//   new table                  -> YES. The source may already hold movements no trigger ever saw.
//   a column ADDED             -> no.  A resource that did not exist has no history, so its correct
//                                      value everywhere is zero — which the ALTER default already
//                                      wrote. This is the case worth skipping: it makes adding a
//                                      resource to a large register instant instead of an outage.
//   a column DROPPED / CHANGED -> YES. Dropping a dimension coarsens the grouping (rows must merge);
//                                      a changed column invalidates what accumulated under it.
//   the KEY SHAPE changed      -> YES. Every existing row is keyed the old way and cannot be re-keyed.
//
// The asymmetry is load-bearing: "added" is skippable because its effect on existing totals is
// provably nothing. Everything else is not provably nothing, so it rebuilds — skipping a needed
// rebuild yields silently wrong totals, running a needless one only costs time.
//
// 🛑 A type that leaves a column is not a case here: the differ clears it in the totals exactly as in
// the movements, and they stay equal. Not the place for a metadata case — see schemaSnapshot.h.
BACKEND_API bool NeedsRegeneration(const ibSchemaTable* old, const ibSchemaTable& cur);

} // namespace ibDerivedState

#endif // !__DERIVED_STATE_BUILDER_H__
