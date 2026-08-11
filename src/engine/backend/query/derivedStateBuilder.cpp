////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : L3-4 — regeneration of derived state (register totals)
////////////////////////////////////////////////////////////////////////////

#include "derivedStateBuilder.h"

#include "backend/query/schemaSnapshot.h"
#include "backend/query/dataQueryBuilder.h"     // the L3-1 door — aggregate read + write core
#include "backend/query/queryable.h"
#include "backend/query/schemaBuilder.h"
#include "backend/databaseLayer/databaseMaterializeBuilder.h"   // ibCanMaterialize — the capability question belongs to L2-2
#include "backend/restructureInfo.h"
#include "backend/databaseLayer/connectionScope.h"    // the holder's own transaction scope (no L1 named here)

#include <map>
#include <algorithm>

namespace {

// Enough declared to rebuild from? A derived table with no source, no accumulations, or no
// queryable of its own has nothing to compute or nowhere to put it.
bool IsRebuildable(const ibSchemaTable& t)
{
	if (!t.m_derived || t.m_queryable == nullptr)
		return false;
	const ibSchemaMaterialize& m = t.m_materialize;
	if (m.m_source == nullptr || m.m_deltas.empty())
		return false;
	// Every accumulation needs its REGENERATION form. The trigger form alone cannot be rebuilt from
	// (it is written against NEW / OLD, which exist only inside a trigger), and silently summing
	// nothing would produce a table full of zeros that looks maintained.
	for (const ibSchemaDelta& d : m.m_deltas)
		if (!d.m_regenExpr || d.m_column == nullptr)
			return false;
	return true;
}

// Enough declared to FOLD? Only a split table has shards to fold, and only a declared accumulation
// can be summed across them. An unsplit table is not a failure — there is simply nothing to do.
bool IsCollapsible(const ibSchemaTable& t)
{
	if (!t.m_derived || t.m_queryable == nullptr)
		return false;
	const ibSchemaMaterialize& m = t.m_materialize;
	if (m.m_shards <= 1 || m.m_deltas.empty())
		return false;
	for (const ibSchemaDelta& d : m.m_deltas)
		if (d.m_column == nullptr)
			return false;
	return true;
}

// ⚠ EVERY read and write in this file is SYSTEM work, and the access policy must not touch any of
// it. This recomputes STORED totals: filtered by the caller's rights it would rebuild them from the
// rows that caller happens to see, and everyone afterwards would read numbers that are simply
// wrong — silently, because a wrong total does not look like an error. The write half is the same
// argument from the other side: clearing and re-writing the derived table is not the user's edit.
//
// SAID OUT LOUD, not inferred. The door pulls the policy from the CURRENT SESSION, and a background
// job runs under the identity of whoever queued it — so it has a session and a policy, and any rule
// of the form "no session = system" would be false exactly here.
//
// One helper rather than nine marks: nine copies of a decision drift, and the ninth is the one
// somebody adds without the comment.
ibDataQueryBuilder SystemQuery(ibDatabaseConnectionHolder* holder)
{
	ibDataQueryBuilder query(holder);
	query.WithAccessPolicy(nullptr);
	return query;
}

} // namespace

namespace ibDerivedState {

bool NeedsRegeneration(const ibSchemaTable* old, const ibSchemaTable& cur)
{
	if (!cur.m_derived)
		return false;
	if (old == nullptr)
		return true;   // new table over a possibly-populated source

	// A column that VANISHED (dropped) — the grouping or the accumulation it fed is gone.
	for (const ibSchemaColumn& o : old->m_columns) {
		bool stillThere = false;
		for (const ibSchemaColumn& c : cur.m_columns)
			if (c.m_id == o.m_id) { stillThere = true; break; }
		if (!stillThere)
			return true;
	}

	// The KEY SHAPE itself — a dimension added or removed, or a different stored grain. Existing
	// rows are keyed the old way; no in-place fix exists.
	const ibSchemaMaterialize& a = old->m_materialize;
	const ibSchemaMaterialize& b = cur.m_materialize;
	if (a.m_keys.size() != b.m_keys.size())     return true;
	if (a.m_periodUnit != b.m_periodUnit)       return true;
	if (a.m_periodColumn != b.m_periodColumn)   return true;
	// Splitting joins or leaves the KEY, so switching it re-keys every existing row. There is no
	// in-place fix — an unsplit row has no shard to belong to, and a split one cannot merge back
	// without summing. Rebuild.
	if (a.m_shards != b.m_shards)               return true;

	// The set of ACCUMULATIONS changed — which happens when a register switches between turnover
	// and balance kinds. This is NOT the harmless "a column was added" case: gaining an expense
	// side changes what the receipt side MEANS (it stops holding every movement and starts holding
	// only one direction), so every stored figure is now wrong even though the column that held it
	// still exists. Rebuild.
	if (a.m_deltas.size() != b.m_deltas.size()) return true;

	// Only additions left — provably no effect on what is already accumulated.
	return false;
}

bool Regenerate(const ibSchemaTable& derived, ibDatabaseConnectionHolder* holder)
{
	if (!IsRebuildable(derived))
		return true;   // nothing declared to rebuild — not a failure

	{
		// Ask L2-2 whether this driver materialises at all — never inspect a dialect from up here.
		ibSchemaBuilder schema(holder);
		if (!ibCanMaterialize(schema.Connection()))
			return true;   // nothing was ever materialised, so there is nothing to rebuild
	}

	const ibSchemaMaterialize& spec = derived.m_materialize;

	// 1. READ the source, aggregated by the declared key.
	//
	// The period key is grouped by the TRUNCATED expression, through the same ibTotalsPeriod and the
	// same dialect map the trigger uses. That identity is the point: two separate notions of "start
	// of the month" would make a rebuilt row land on a different key than the trigger would have
	// produced, silently splitting rows the trigger had merged.
	ibDataQueryBuilder read = SystemQuery(holder);
	read.From(spec.m_source);

	// The guard the trigger accumulates under — applied here too, or a rebuild would produce totals
	// the trigger would never have produced. Same condition, one declaration, two forms.
	if (spec.m_guardExpr)
		read.Where(spec.m_guardExpr);

	if (!spec.m_periodColumn.IsEmpty() && spec.m_periodSource != nullptr)
		read.GroupByExpr(ibQueryColumnExpr::PeriodTrunc(ibQueryColumnExpr::Col(spec.m_periodSource), spec.m_periodUnit),
		                 spec.m_periodColumn);
	for (const ibBackendQueryColumn* k : spec.m_keys)
		read.GroupBy(k);

	for (const ibSchemaDelta& d : spec.m_deltas)
		read.Aggregate(ibDataQueryBuilder::AggregateFn::Sum, d.m_regenExpr, d.m_column->GetPhysicalName());

	ibDataQueryResult rows = read.SelectAggregate();

	// 2. CLEAR the derived table. It is a cache: everything in it is about to be recomputed. Clearing
	//    AFTER the read, not before, keeps the window where totals are missing as short as possible —
	//    and both steps sit inside the caller's restructuring transaction, so a failure rolls the old
	//    rows back rather than leaving the table empty.
	{
		ibDataQueryBuilder clear = SystemQuery(holder);
		clear.From(derived.m_queryable);
		if (!clear.Delete())
			return false;
	}

	// 3. WRITE the aggregate back. Upsert, not insert: a trigger firing concurrently during the
	//    rebuild would otherwise collide on the key, and upsert also makes a retried rebuild
	//    idempotent.
	while (rows.Next()) {
		ibDataQueryBuilder write = SystemQuery(holder);
		write.From(derived.m_queryable);

		if (!spec.m_periodColumn.IsEmpty())
			write.SetValue(ibRawDBColumn::Date(spec.m_periodColumn), rows.GetColumn(spec.m_periodColumn));

		// A SPLIT table still gets ONE row per key from a rebuild — the shard exists to spread
		// concurrent writers, and a rebuild is a single writer that already holds the consolidated
		// figure. Shard 0 is where it lands; the trigger spreads everything that follows. Leaving the
		// column unset would work too (the view sums every shard, so the total is invariant), but a
		// NULL in the unique key is a fact nobody declared.
		if (spec.m_shards > 1)
			write.SetValue(ibRawDBColumn::Number(ShardColumnName()), ibValue(0.0));
		for (const ibBackendQueryColumn* k : spec.m_keys)
			write.SetValue(k, rows.GetValue(k));
		for (const ibSchemaDelta& d : spec.m_deltas)
			write.SetValue(d.m_column, rows.GetColumn(d.m_column->GetPhysicalName()));

		if (!write.Upsert())
			return false;
	}

	return true;
}

bool Collapse(const ibSchemaTable& derived, ibDatabaseConnectionHolder* holder)
{
	if (holder == nullptr)
		return false;   // no context = no connection to be sure of; see the header — never guess one
	if (!IsCollapsible(derived))
		return true;    // nothing split to fold — not a failure

	{
		// The same capability question the rebuild asks: a driver that never materialised has no
		// shards to fold, because it has no derived table at all.
		ibSchemaBuilder schema(holder);
		if (!ibCanMaterialize(schema.Connection()))
			return true;
	}

	const ibSchemaMaterialize& spec = derived.m_materialize;
	const bool hasPeriod = !spec.m_periodColumn.IsEmpty();

	// THE BOUNDARY IS DERIVED, NEVER PASSED IN. Everything strictly before the CURRENT stored period
	// is settled: nothing writes there in the normal course, so folding it is stable work rather than
	// a race against live postings. And the two facts it takes are both already here — the table's
	// own stored unit (declared beside it) and the clock — so no caller has to know, track, or
	// advance anything. There is no "how far have we folded" state to keep, and none to get wrong.
	//
	// A table with no period dimension has no such frontier: its whole content is fair game, since
	// a key without a period is written to at any time or not at all.
	const ibValue periodBefore = hasPeriod
		? ibValue(ibTruncateToPeriod(wxDateTime::Now(), spec.m_periodUnit)) : ibValue();
	const bool bounded = hasPeriod;

	// Resolve both through the SOURCE rather than building raw columns here. The door checks column
	// ownership by POINTER identity (OwnsColumn), so a locally-made twin of the same field is a
	// different column as far as it is concerned — it would read as belonging to nobody. These are
	// the table's own declared columns, which is also why they need no lifetime care.
	const ibBackendQueryColumn* periodCol = hasPeriod
		? derived.m_queryable->ResolveColumnByName(spec.m_periodColumn) : nullptr;
	const ibBackendQueryColumn* shardCol = derived.m_queryable->ResolveColumnByName(ShardColumnName());
	if (shardCol == nullptr || (hasPeriod && periodCol == nullptr))
		return true;   // the source does not expose what the declaration promised — nothing safe to do

	// 1. READ one row per (key, shard) straight off the TOTALS table — never the movements. This
	//    re-packs figures that are already correct instead of recomputing them, which is the whole
	//    reason it is affordable next to a rebuild.
	ibDataQueryBuilder read = SystemQuery(holder);
	read.From(derived.m_queryable);

	if (hasPeriod)
		read.GroupBy(periodCol);
	for (const ibBackendQueryColumn* k : spec.m_keys)
		read.GroupBy(k);
	read.GroupBy(shardCol);
	for (const ibSchemaDelta& d : spec.m_deltas)
		read.Aggregate(ibDataQueryBuilder::AggregateFn::Sum, d.m_column, d.m_column->GetPhysicalName());

	if (bounded)
		read.WhereCompare(periodCol, ibQueryFilterOp::Less, periodBefore);

	ibDataQueryResult rows = read.SelectAggregate();

	// Drain and bucket by LOGICAL key (period + dimensions). Keys that occupy one row are already
	// folded and drop out below, so what survives is only what actually spread. Reading the whole
	// range to discover that is the cost of not tracking state anywhere — and once a period settles
	// it is one row per key, so the pass gets cheaper every time it runs.
	struct ibShardRow { ibValue m_period; std::vector<ibValue> m_keys; ibValue m_shard; std::vector<ibValue> m_sums; };
	std::map<wxString, std::vector<ibShardRow>> spread;

	while (rows.Next()) {
		ibShardRow row;
		wxString id;
		if (hasPeriod) {
			row.m_period = rows.GetValue(periodCol);
			id = row.m_period.GetString();
		}
		// (the shard is read below, after the keys, so `id` stays the LOGICAL key only)
		for (const ibBackendQueryColumn* k : spec.m_keys) {
			row.m_keys.push_back(rows.GetValue(k));
			id += wxT("\x1F") + row.m_keys.back().GetString();   // unit separator — never inside a value
		}
		row.m_shard = rows.GetValue(shardCol);
		for (const ibSchemaDelta& d : spec.m_deltas)
			row.m_sums.push_back(rows.GetColumn(d.m_column->GetPhysicalName()));
		spread[id].push_back(std::move(row));
	}

	// 2. FOLD each spread key: move every other shard's figure into one absorbing row, then drop
	//    the rows that end up empty.
	for (auto& entry : spread) {
		std::vector<ibShardRow>& shards = entry.second;
		if (shards.size() < 2)
			continue;   // one row already — nothing to fold, whichever shard it sits in

		// ONE TRANSACTION PER KEY — the finest granularity that is still correct, and the choice
		// that decides whether this can run while people work. Atomicity is needed only across one
		// key's add / subtract pair; anything wider just holds locks longer. Per TABLE (what this
		// was) accumulates a row lock for every key it has touched and holds them all until the
		// table is done — so a posting that happens to need one of those rows waits on a sweep that
		// has nothing to do with it. Per key, the lock is three statements long and the next
		// transaction starts clean.
		//
		// Nested scopes collapse onto one real transaction, so a caller that already opened one
		// keeps its own boundary and this becomes a no-op inside it.
		ibConnectionScope scope(holder);
		scope.SafeBeginTransaction();

		// The ABSORBER is the lowest-numbered shard PRESENT, not shard 0. The trigger picks shards
		// by hashing the connection, so a key may well have no shard-0 row at all — and creating one
		// first would mean an INSERT racing the very writers this fold is meant to tolerate.
		std::sort(shards.begin(), shards.end(),
			[](const ibShardRow& a, const ibShardRow& b) { return a.m_shard.GetNumber() < b.m_shard.GetNumber(); });
		const ibValue absorber = shards.front().m_shard;

		for (size_t i = 1; i < shards.size(); i++) {
			const ibShardRow& src = shards[i];

			// The WHERE that names one physical row of this key — shared by all three statements.
			auto Aim = [&](ibDataQueryBuilder& q, const ibValue& shard) {
				q.From(derived.m_queryable);
				if (hasPeriod)
					q.Where(periodCol, src.m_period);
				for (size_t n = 0; n < spec.m_keys.size(); n++)
					q.Where(spec.m_keys[n], src.m_keys[n]);
				q.Where(shardCol, shard);
			};
			// (WHERE, not a key match — which is why the source reports no primary key: an UPDATE
			//  here must hit ONE physical row of the key, the one this shard occupies.)

			// ADD then SUBTRACT, both as in-statement arithmetic (AddValue). That is the whole point
			// of the rewrite: a movement landing mid-fold COMPOSES with the adjustment instead of
			// being overwritten by it, so the fold no longer needs a quiet range. The pair must be
			// atomic or a crash between them doubles / loses the figure — hence the caller's
			// transaction, and hence folding one key at a time so that transaction stays short.
			{
				ibDataQueryBuilder add = SystemQuery(holder);
				Aim(add, absorber);
				for (size_t n = 0; n < spec.m_deltas.size(); n++)
					add.AddValue(spec.m_deltas[n].m_column, src.m_sums[n]);
				if (!add.Update())
					return false;
			}
			{
				ibDataQueryBuilder sub = SystemQuery(holder);
				Aim(sub, src.m_shard);
				for (size_t n = 0; n < spec.m_deltas.size(); n++)
					sub.AddValue(spec.m_deltas[n].m_column, ibValue(-src.m_sums[n].GetNumber()));
				if (!sub.Update())
					return false;
			}

			// DROP the drained row — ONLY if it really came out empty. A delta that arrived
			// mid-fold left it non-zero, and then the row is not ours to remove: its contribution
			// is still owed to the total, and the next pass folds it. Deleting on the shard number
			// alone (what the first version did) is exactly the write that would swallow it.
			{
				ibDataQueryBuilder drop = SystemQuery(holder);
				Aim(drop, src.m_shard);
				for (const ibSchemaDelta& d : spec.m_deltas)
					drop.Where(d.m_column, ibValue(0.0));
				if (!drop.Delete())
					return false;   // ~scope rolls the key back — nothing half-moved survives
			}
		}
		scope.SafeCommitTransaction();
	}

	return true;
}

int VerifyLastPeriod(const ibSchemaTable& derived, ibDatabaseConnectionHolder* holder)
{
	// IsRebuildable is exactly the right gate: it asks for a source and a REGENERATION expression per
	// accumulation, which is precisely what re-aggregating the movements needs. A table that cannot be
	// rebuilt cannot be verified either — there is nothing to compare against.
	if (holder == nullptr || !IsRebuildable(derived))
		return -1;

	{
		ibSchemaBuilder schema(holder);
		if (!ibCanMaterialize(schema.Connection()))
			return 0;   // nothing materialised: the live aggregation IS the only path, so it agrees with itself
	}

	const ibSchemaMaterialize& spec = derived.m_materialize;
	if (spec.m_periodColumn.IsEmpty() || spec.m_periodSource == nullptr)
		return -1;   // no period dimension — "the last elapsed period" means nothing here

	const ibBackendQueryColumn* periodCol = derived.m_queryable->ResolveColumnByName(spec.m_periodColumn);
	if (periodCol == nullptr)
		return -1;

	// The window is the whole of the PREVIOUS stored period. Stepping back is done by truncating a
	// moment just before the current period begins — no per-unit calendar arithmetic, and it stays
	// right for the irregular units too (a week, a ten-day span whose last one runs 8-11 days).
	const wxDateTime curStart  = ibTruncateToPeriod(wxDateTime::Now(), spec.m_periodUnit);
	const wxDateTime prevStart = ibTruncateToPeriod(curStart - wxTimeSpan::Seconds(1), spec.m_periodUnit);

	// Both sides are drained the same way — one entry per key, the accumulations in declaration
	// order — so the comparison below comes down to two maps of the same shape.
	auto Drain = [&](ibDataQueryResult& rows) {
		std::map<wxString, std::vector<ibValue>> out;
		while (rows.Next()) {
			wxString id;
			for (const ibBackendQueryColumn* k : spec.m_keys)
				id += wxT("\x1F") + rows.GetValue(k).GetString();
			std::vector<ibValue> sums;
			for (const ibSchemaDelta& d : spec.m_deltas)
				sums.push_back(rows.GetColumn(d.m_column->GetPhysicalName()));
			out[id] = std::move(sums);
		}
		return out;
	};

	// SIDE A — re-aggregate the MOVEMENTS. The same read Regenerate performs, narrowed to one period.
	// Filtering on the RAW period column is correct because truncation is monotone: exactly the
	// movements that truncate into prevStart lie in [prevStart, curStart).
	std::map<wxString, std::vector<ibValue>> fromSource;
	{
		ibDataQueryBuilder read = SystemQuery(holder);
		read.From(spec.m_source);
		read.WhereCompare(spec.m_periodSource, ibQueryFilterOp::GreaterEqual, ibValue(prevStart));
		read.WhereCompare(spec.m_periodSource, ibQueryFilterOp::Less,         ibValue(curStart));
		for (const ibBackendQueryColumn* k : spec.m_keys)
			read.GroupBy(k);
		for (const ibSchemaDelta& d : spec.m_deltas)
			read.Aggregate(ibDataQueryBuilder::AggregateFn::Sum, d.m_regenExpr, d.m_column->GetPhysicalName());

		ibDataQueryResult rows = read.SelectAggregate();
		fromSource = Drain(rows);
	}

	// SIDE B — what the TOTALS hold for that period. Grouping WITHOUT the shard column sums the
	// shards, which is the same thing the read view does, so a split table is compared as one figure.
	std::map<wxString, std::vector<ibValue>> fromTotals;
	{
		ibDataQueryBuilder read = SystemQuery(holder);
		read.From(derived.m_queryable);
		read.Where(periodCol, ibValue(prevStart));
		for (const ibBackendQueryColumn* k : spec.m_keys)
			read.GroupBy(k);
		for (const ibSchemaDelta& d : spec.m_deltas)
			read.Aggregate(ibDataQueryBuilder::AggregateFn::Sum, d.m_column, d.m_column->GetPhysicalName());

		ibDataQueryResult rows = read.SelectAggregate();
		fromTotals = Drain(rows);
	}

	// COMPARE both ways. A key the movements know and the totals do not is a missed accumulation; a
	// key only the totals carry is a figure nothing accounts for — a leftover from a grouping that
	// changed, or a movement deleted without its trigger. Both are disagreements, and counting only
	// the first direction would call the second one clean.
	int mismatches = 0;
	for (const auto& entry : fromSource) {
		const auto found = fromTotals.find(entry.first);
		if (found == fromTotals.end()) { mismatches++; continue; }
		for (size_t i = 0; i < entry.second.size() && i < found->second.size(); i++)
			if (entry.second[i].GetNumber() != found->second[i].GetNumber()) { mismatches++; break; }
	}
	for (const auto& entry : fromTotals)
		if (fromSource.find(entry.first) == fromSource.end())
			mismatches++;

	return mismatches;
}

int CollapseAll(const ibSchemaSnapshot& target, ibDatabaseConnectionHolder* holder, ibRestructureInfo* report)
{
	// ONE TRANSACTION PER TABLE, and that granularity is the design rather than a detail. The fold's
	// add / subtract pair must be atomic — a crash between them doubles or drops that one figure —
	// while one transaction around the whole sweep would hold a write transaction open for as long as
	// the sweep runs, against a database people are working in. Per table is short enough to be
	// polite and long enough to be correct.
	//
	// So an interrupted run leaves whole tables folded and the rest untouched. That is a valid state,
	// not a partial one: an unfolded table reads exactly right, it just reads a few rows wider.
	// NO transaction here. Each fold owns its own, one per KEY (see Collapse) — which is what keeps
	// the row locks it takes measured in statements rather than in tables. Opening one around the
	// whole sweep would undo exactly that.
	if (holder == nullptr)
		return -1;   // the job must be handed its holder — there is no ambient one worth borrowing

	int done = 0;
	for (const ibSchemaTable& t : target.Tables()) {
		if (!IsCollapsible(t))
			continue;   // unsplit — nothing to fold, not a failure

		if (!Collapse(t, holder))
			return -1;

		if (report != nullptr)
			report->AppendInfo(_("Fold totals shards for ") + t.m_name);
		done++;
	}
	return done;
}

ibTotalsMaintenance MaintainTotals(const ibSchemaSnapshot& target, ibDatabaseConnectionHolder* holder)
{
	ibTotalsMaintenance out;
	if (holder == nullptr) {
		out.m_failed = true;
		return out;   // the job is handed its holder; there is no ambient one worth borrowing
	}

	for (const ibSchemaTable& t : target.Tables()) {
		if (!t.m_derived)
			continue;
		out.m_checked++;

		// VERIFY FIRST. Not for safety — the fold moves figures without changing their sum, so the
		// order cannot corrupt anything — but because a disagreement discovered afterwards would
		// have to say whether it predates the fold. Checking first leaves no such question.
		//
		// -1 means the check could not run (no period dimension, nothing declared to rebuild from),
		// which is not a disagreement and must not be counted as one.
		const int mismatched = VerifyLastPeriod(t, holder);
		if (mismatched > 0)
			out.m_mismatched += mismatched;

		// FOLD regardless of the verdict. A disagreement is a question for the Designer's recompute
		// command, not a reason to leave the table spread — the shards are wrong either way, and
		// folding them changes nothing about that while still shrinking the read.
		if (IsCollapsible(t)) {
			if (!Collapse(t, holder)) {
				out.m_failed = true;
				return out;
			}
			out.m_folded++;
		}
	}
	return out;
}

int RegenerateAll(const ibSchemaSnapshot& target, ibDatabaseConnectionHolder* holder, ibRestructureInfo* report)
{
	int done = 0;
	for (const ibSchemaTable& t : target.Tables()) {
		if (!t.m_derived)
			continue;
		if (!Regenerate(t, holder))
			return -1;
		if (report != nullptr)
			report->AppendInfo(_("Rebuild totals for ") + t.m_name);
		done++;
	}
	return done;
}

} // namespace ibDerivedState
