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
	ibDataQueryBuilder read(holder);
	read.From(spec.m_source);

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
		ibDataQueryBuilder clear(holder);
		clear.From(derived.m_queryable);
		if (!clear.Delete())
			return false;
	}

	// 3. WRITE the aggregate back. Upsert, not insert: a trigger firing concurrently during the
	//    rebuild would otherwise collide on the key, and upsert also makes a retried rebuild
	//    idempotent.
	while (rows.Next()) {
		ibDataQueryBuilder write(holder);
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
