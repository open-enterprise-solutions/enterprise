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
// (docs/register-totals-strategy.md § Engine integration)

#include "backend.h"

struct ibSchemaTable;
class ibSchemaSnapshot;
class ibDatabaseConnectionHolder;
class ibRestructureInfo;

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
BACKEND_API bool NeedsRegeneration(const ibSchemaTable* old, const ibSchemaTable& cur);

} // namespace ibDerivedState

#endif // !__DERIVED_STATE_BUILDER_H__
