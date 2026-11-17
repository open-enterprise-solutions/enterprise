////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : L3-4 — regeneration of derived state (register totals)
////////////////////////////////////////////////////////////////////////////

#include "derivedStateBuilder.h"

#include "backend/query/provider/database/structure/schemaSnapshot.h"
#include "backend/query/dataQueryBuilder.h"     // the L3-1 door — aggregate read + write core
#include "backend/query/queryable.h"
#include "backend/query/provider/database/structure/schemaBuilder.h"
#include "backend/databaseLayer/databaseMaterializeBuilder.h"   // ibCanMaterialize — the capability question belongs to L2-2
#include "backend/restructureInfo.h"
#include "backend/backend_exception.h"                // ibBackendCoreException — a rebuild refused aloud
#include "backend/databaseLayer/connectionScope.h"    // the holder's own transaction scope (no L1 named here)
#include "backend/databaseLayer/databaseLayerException.h"   // a fold slice a writer holds is a conflict, not a failure
#include "backend/databaseLayer/databaseQueryBuilder.h" // L2 — the rebuild's INSERT … SELECT
#include "backend/query/columnLayout.h"                 // ColumnFieldNames / ibSqlAliasOf — the read's output names

#include <unordered_map>   // the verification's key figures — keyed by a sequence of values (ibValueSeqHash)

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
	// The stored units, the whole list and the column that tells them apart: a unit gained has no rows yet,
	// a unit lost leaves rows nothing writes, and a table that gains the column holds rows of no unit at all.
	if (a.m_periodUnits != b.m_periodUnits)     return true;
	if (a.m_periodColumn != b.m_periodColumn)   return true;
	if (a.m_periodUnitColumn != b.m_periodUnitColumn)
		return true;
	// Splitting joins or leaves the KEY, so switching it re-keys every existing row. There is no
	// in-place fix — an unsplit row has no shard to belong to, and a split one cannot merge back
	// without summing. Rebuild.
	if (a.m_shards != b.m_shards)               return true;

	// ⭐⭐ A KEY COLUMN THAT CHANGED SHAPE re-keys every stored row just as surely as a key that was
	// added — the count is the same, but a dimension retyped from a string to a reference no longer
	// spreads into the same physical fields (`…_S` becomes `…_RTRef` + `…_RRRef`), and even a plain
	// widening changes the declared type of a field the table's PRIMARY KEY stands on.
	//
	// ⚠ AND NOTHING BELOW CAN FIX IT IN PLACE: the ordinary column diff would emit an ALTER, and no
	// engine alters a column an index is built over — Firebird refuses the whole apply ("column
	// FLD1391_S … is referenced in index …_PK", measured 2026-09-16 on a copy while a register's
	// Currency dimension moved from String to a catalog reference). The right answer is the one this
	// function already gives for every other re-keying: drop the derived table and regenerate it from
	// the movements, where the values are kept in their own right.
	for (size_t i = 0; i < a.m_keys.size(); ++i) {
		if (a.m_keys[i] == nullptr || b.m_keys[i] == nullptr)
			return true;   // a key nobody can describe is not a key anybody can prove unchanged
		const std::vector<ibColumnSlot> was = DescribeColumnLayout(*a.m_keys[i]);
		const std::vector<ibColumnSlot> now = DescribeColumnLayout(*b.m_keys[i]);
		if (was.size() != now.size())
			return true;
		for (size_t f = 0; f < was.size(); ++f)
			if (was[f].m_name != now[f].m_name || !ibSameFieldType(was[f].m_type, now[f].m_type))
				return true;
	}

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

	// 1. The READ of the source, aggregated by the declared key — built here, run as the SELECT of step 3. One
	//    per stored unit (m_periodUnits): each unit is rows of its own, the period truncated to THAT unit.
	//
	// The period key is grouped by the TRUNCATED expression, through the same ibTotalsPeriod and the
	// same dialect map the trigger uses. That identity is the point: two separate notions of "start
	// of the month" would make a rebuilt row land on a different key than the trigger would have
	// produced, silently splitting rows the trigger had merged.
	const auto ReadAt = [&](ibTotalsPeriod unit) {
		ibDataQueryBuilder read = SystemQuery(holder);
		read.From(spec.m_source);

		// The guard the trigger accumulates under — applied here too, or a rebuild would produce totals
		// the trigger would never have produced. Same condition, one declaration, two forms.
		if (spec.m_guardExpr)
			read.Where(spec.m_guardExpr);

		if (!spec.m_periodColumn.IsEmpty() && spec.m_periodSource != nullptr)
			read.GroupByExpr(ibQueryColumnExpr::PeriodTrunc(ibQueryColumnExpr::Col(spec.m_periodSource), unit),
			                 spec.m_periodColumn);
		for (const std::shared_ptr<const ibBackendQueryColumn>& k : spec.m_keys)
			read.GroupBy(k);

		for (const ibSchemaDelta& d : spec.m_deltas)
			read.Aggregate(ibDataQueryBuilder::AggregateFn::Sum, d.m_regenExpr, d.m_column->GetPhysicalName());
		return read.BuildRelation();
	};

	// 2. CLEAR the derived table. It is a cache: everything in it is about to be recomputed. Both steps
	//    sit inside the caller's restructuring transaction, so a failure rolls the old rows back rather
	//    than leaving the table empty.
	{
		ibDataQueryBuilder clear = SystemQuery(holder);
		clear.From(derived.m_queryable);
		if (!clear.Delete())
			return false;
	}

	// 3. WRITE the aggregate back — ONE statement per unit: the read above, as the SELECT of an INSERT.
	//
	// ⭐⭐ IT WAS ONE UPSERT PER ROW, a statement built and prepared for every one of them: MEASURED 2026-09-13
	// on a 40 000-employee copy (Debug), ~5 ms a row, and a calculation register's totals (it kept them then)
	// ran for tens of minutes of an apply. The relation is the read's own lowering (BuildRelation — the provider's aggregate
	// query stopped before it runs), so nothing here spells a second GROUP BY; its outputs are the key columns'
	// fields under their own names and every figure under the statement's spelling (ibSqlAliasOf).
	//
	// INSERT, not upsert: the table was cleared just above, inside the same restructuring, which holds the base
	// exclusively — no trigger writes beside it, and a retried rebuild clears first again.
	//
	// A SPLIT table still gets ONE row per key from a rebuild — the shard exists to spread concurrent writers,
	// and a rebuild is a single writer that already holds the consolidated figure. Shard 0 is where it lands;
	// the trigger spreads everything that follows. A NULL in the unique key would be a fact nobody declared.
	for (const ibTotalsPeriod unit : spec.m_periodUnits) {
		const ibQueryRelPtr folded = ReadAt(unit);
		if (!folded)
			ibBackendCoreException::Error(_("The derived table '%s' cannot be rebuilt: its source is not read from the database"),
			                              derived.m_name);
		std::vector<ibString> columns;
		std::vector<ibQueryProjItem> values;
		const auto take = [&](const ibString& column, const ibString& output) {
			columns.push_back(column);
			values.push_back(ibQueryProjItem{ ibCol(ibT("r_"), output), column });
		};
		if (!spec.m_periodColumn.IsEmpty() && spec.m_periodSource != nullptr)   // the read's own condition, above
			take(spec.m_periodColumn, ibSqlAliasOf(spec.m_periodColumn));
		// The unit these rows are kept at, when the table keeps several — one value for the whole statement, as
		// the shard's 0 is below.
		if (!spec.m_periodUnitColumn.IsEmpty()) {
			columns.push_back(spec.m_periodUnitColumn);
			values.push_back(ibQueryProjItem{ ibCast(ibConst(ibValue(static_cast<int>(unit))), ibTypeInteger()), spec.m_periodUnitColumn });
		}
		// The period, its unit and the shard are named here each once, from their own names — `m_keys` holds none
		// of them, so nothing can put the same physical column into the statement twice.
		for (const std::shared_ptr<const ibBackendQueryColumn>& k : spec.m_keys)
			for (const ibString& field : ColumnFieldNames(*k))
				take(field, field);
		for (const ibSchemaDelta& d : spec.m_deltas)
			take(d.m_column->GetPhysicalName(), ibSqlAliasOf(d.m_column->GetPhysicalName()));
		if (spec.m_shards > 1) {
			columns.push_back(ShardColumnName());
			values.push_back(ibQueryProjItem{ ibCast(ibConst(ibValue(0)), ibTypeInteger()), ShardColumnName() });
		}
		// …and through the barrier, as the hash fill below: Firebird refuses DML against a table created in the
		// same transaction, and refuses it at commit.
		{
			ibSchemaBuilder schema(holder);
			const ibDmlStatement insert = ibInsertSelect(derived.m_name, columns, ibProject(ibSubquery(folded, ibT("r_")), values));
			if (!schema.RunOrDefer(derived.m_name, [insert, holder]() {
					ibDatabaseQueryBuilder write(holder);
					return write.Execute(insert) >= 0;
				}))
				return false;
		}
	}

	// 4. GIVE THOSE ROWS THE IDENTITY THE TRIGGER WOULD HAVE GIVEN THEM.
	//
	// Where a key is too wide for an index, the derived table holds its uniqueness in a hashed field
	// that the TRIGGER computes as it inserts (databaseMaterializeBuilder.h). A rebuild writes through
	// the ordinary door, so its rows arrive without one — and an empty identity is not a neutral state:
	// a UNIQUE index over NULLs enforces nothing, and on the engines that conflict on that column the
	// next movement for a rebuilt key would insert a second row beside the first. Same reasoning as the
	// shard column above, one field along.
	// ⭐⭐ AND IT GOES THROUGH THE BARRIER, like every other write into a table this save created.
	//
	// It used to run straight at the connection, and that is the whole reason a chart-of-accounts
	// register could not be re-keyed: Firebird refuses DML against a table created in the SAME
	// transaction, and refuses it AT COMMIT — so this UPDATE reported success, the twenty-six
	// statements before it reported success, and the apply died at the end on "expression evaluation
	// not supported", naming nothing.
	//
	// ⚠ WHY ONLY THIS REGISTER SHOWED IT. The hash column exists only where the key is too wide for
	// the engine's index (ibKeyNeedsHash) — an accounting register's key is the period, the account,
	// a (kind, value) pair per analytic and the dimensions, well past Firebird's sixteen segments. An
	// accumulation register's key fits, carries no hash column, and therefore never issued this
	// statement: the same switch on the same day worked there and failed here.
	//
	// The spec is COPIED into the closure — a deferred fill runs after this function, and after the
	// builder that started it, has gone. The holder travels for the same reason (see schemaSnapshot).
	{
		ibSchemaBuilder schema(holder);
		const ibMaterializeSpec fillSpec = spec.ToRenderSpec(derived.m_name);
		if (!schema.RunOrDefer(derived.m_name, [fillSpec, holder]() {
				ibSchemaBuilder deferred(holder);
				return ibFillKeyHashes(deferred.Connection(), fillSpec);
			}))
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
	const ibMaterializeSpec rendered = spec.ToRenderSpec(derived.m_name);
	const bool hasPeriod = !spec.m_periodColumn.IsEmpty();
	const bool hasUnits  = !spec.m_periodUnitColumn.IsEmpty();

	// Each stored unit keeps rows of its own — a day and the month it opens share a period, and only the unit
	// column tells them apart. A table that keeps one unit has one.
	const std::vector<ibTotalsPeriod> units = hasUnits ? spec.m_periodUnits
	                                                   : std::vector<ibTotalsPeriod>{ spec.m_periodUnits.front() };

	for (const ibTotalsPeriod unit : units) {
		// ⭐⭐ THE PERIOD THAT CLOSED LAST — per unit, and nothing older: yesterday, last week, last month, last
		// quarter, last year. A period's rows split while it is current, because that is when people post into it;
		// the first pass after it closes folds it, and every later pass finds it folded at the cost of one grouping
		// read. So a pass costs one period per unit, never the history behind it — a trillion rows of past cost it
		// nothing. The two facts it takes are the unit and the clock: there is no "how far have we folded" state to
		// keep, and none to get wrong.
		//
		// What it leaves: a posting dated back into an older period may split a key there again. That key reads
		// exactly right, one row wider; the recompute of the totals packs it.
		//
		// The period's VALUE is read off the table, between the two boundaries, rather than computed here: it is
		// the trigger's truncation that stored it (where a week begins is the engine's answer, not this clock's).
		//
		// A table with no period dimension has no frontier: it is one slice, folded whole.
		std::vector<ibDateTime> periods;
		if (hasPeriod) {
			const ibDateTime current = ibDateTime::Now().BeginOfPeriod(unit);
			ibQueryExprPtr closed = ibBinOp(ibQueryBinOp::And,
				ibBinOp(ibQueryBinOp::Ge, ibCol(spec.m_periodColumn), ibConst(ibValue(current.AddPeriods(unit, -1)))),
				ibBinOp(ibQueryBinOp::Lt, ibCol(spec.m_periodColumn), ibConst(ibValue(current))));
			if (hasUnits)
				closed = ibBinOp(ibQueryBinOp::And, ibBinOp(ibQueryBinOp::Eq, ibCol(spec.m_periodUnitColumn),
				                                            ibConst(ibValue(static_cast<int>(unit)))), closed);
			ibDatabaseQueryBuilder read(holder);
			read.From(derived.m_name);
			read.Project({ ibQueryProjItem{ ibCol(spec.m_periodColumn), spec.m_periodColumn } });
			read.GroupBy(ibCol(spec.m_periodColumn));
			read.Where(closed);
			ibQueryResult rows = read.Execute();
			while (rows.Next())
				periods.push_back(rows.GetResultDate(spec.m_periodColumn));
		}
		else
			periods.push_back(ibDateTime());

		// ONE SNAPSHOT TRANSACTION PER SLICE, and one that never waits. The fold writes the sum it read
		// (ibFoldShards); a row a writer holds, or changed meanwhile, is refused at once, the slice rolls
		// back and the posting goes through — the slice folds on the next pass. So the cleanup always yields: a
		// posting waits on a fold at most one slice long, and a fold never waits on a posting.
		//
		// Nested scopes collapse onto one real transaction, so a caller that already opened one keeps its own
		// boundary.
		for (const ibDateTime& period : periods) {
			ibDbTxOptions oneState;
			oneState.snapshot = true;
			oneState.noWait   = true;
			ibConnectionScope scope(holder);
			scope.SafeBeginTransaction(oneState);
			try {
				ibFoldShards(*scope.get(), rendered, unit, period);
			}
			catch (const ibBackendDatabaseException& err) {
				if (err.GetKind() != ibBackendDatabaseException::Kind::Deadlock &&
				    err.GetKind() != ibBackendDatabaseException::Kind::Timeout)
					throw;   // not a writer in the way: ~scope rolls the slice back, and the job hears it
				scope.SafeRollBackTransaction();
				continue;
			}
			scope.SafeCommitTransaction();
		}
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

	const std::shared_ptr<const ibBackendQueryColumn> periodCol = derived.m_queryable->ResolveColumnByName(spec.m_periodColumn);
	if (periodCol == nullptr)
		return -1;

	// The window is the whole of the PREVIOUS stored period. Stepping back is done by truncating a
	// moment just before the current period begins — no per-unit calendar arithmetic, and it stays
	// right for the irregular units too (a week, a ten-day span whose last one runs 8-11 days).
	const ibDateTime curStart  = ibDateTime::Now().BeginOfPeriod(spec.m_periodUnits.front());
	const ibDateTime prevStart = curStart.AddMilliseconds(-1000).BeginOfPeriod(spec.m_periodUnits.front());

	// The stored grain's rows only, when the table keeps several units: the day that opens a month shares its
	// period with the month's row, and summed together the totals would hold that day twice.
	const std::shared_ptr<const ibBackendQueryColumn> unitCol = spec.m_periodUnitColumn.IsEmpty()
		? nullptr : derived.m_queryable->ResolveColumnByName(spec.m_periodUnitColumn);
	if (!spec.m_periodUnitColumn.IsEmpty() && unitCol == nullptr)
		return -1;

	// Both sides are drained the same way — one entry per key, the accumulations in declaration
	// order — so the comparison below comes down to two maps of the same shape.
	//
	// ⭐ THE KEY AS THE READING SEES IT: a sequence of values (ibValueSeqHash), not their text joined.
	// 🛑 It was the text, and the entry was ASSIGNED: two groups the table keeps apart - an empty
	// currency stored untagged and one stored as an empty reference - read as the same text, and the
	// second replaced the first, so the check compared half a key's figure with the whole of it
	// (2026-09-26). Groups that are one key to a reader are one entry here, and their figures ADD.
	using ibKeyFigures = std::unordered_map<std::vector<ibValue>, std::vector<ibNumber>, ibValueSeqHash, ibValueSeqEqual>;
	auto Drain = [&](ibDataQueryResult& rows) {
		ibKeyFigures out;
		while (rows.Next()) {
			std::vector<ibValue> key;
			for (const std::shared_ptr<const ibBackendQueryColumn>& k : spec.m_keys)
				key.push_back(rows.GetValue(k));
			std::vector<ibNumber>& sums = out[key];
			sums.resize(spec.m_deltas.size());
			for (size_t n = 0; n < spec.m_deltas.size(); n++)
				sums[n] += rows.GetColumn(spec.m_deltas[n].m_column->GetPhysicalName()).GetNumber();
		}
		return out;
	};

	// SIDE A — re-aggregate the MOVEMENTS. The same read Regenerate performs, narrowed to one period.
	// Filtering on the RAW period column is correct because truncation is monotone: exactly the
	// movements that truncate into prevStart lie in [prevStart, curStart).
	ibKeyFigures fromSource;
	{
		ibDataQueryBuilder read = SystemQuery(holder);
		read.From(spec.m_source);
		read.WhereCompare(spec.m_periodSource, ibQueryFilterOp::GreaterEqual, ibValue(prevStart));
		read.WhereCompare(spec.m_periodSource, ibQueryFilterOp::Less,         ibValue(curStart));
		for (const std::shared_ptr<const ibBackendQueryColumn>& k : spec.m_keys)
			read.GroupBy(k);
		for (const ibSchemaDelta& d : spec.m_deltas)
			read.Aggregate(ibDataQueryBuilder::AggregateFn::Sum, d.m_regenExpr, d.m_column->GetPhysicalName());

		ibDataQueryResult rows = read.SelectAggregate();
		fromSource = Drain(rows);
	}

	// SIDE B — what the TOTALS hold in the rows of `unit` whose period lies in [from, to). Grouping WITHOUT the shard
	// column sums the shards, which is the same thing the read view does, so a split table is compared as one figure.
	const auto stored = [&](ibTotalsPeriod unit, const ibDateTime& from, const ibDateTime& to) {
		ibDataQueryBuilder read = SystemQuery(holder);
		read.From(derived.m_queryable);
		read.WhereCompare(periodCol, ibQueryFilterOp::GreaterEqual, ibValue(from));
		read.WhereCompare(periodCol, ibQueryFilterOp::Less,         ibValue(to));
		if (unitCol != nullptr)
			read.Where(unitCol, ibValue(static_cast<int>(unit)));
		for (const std::shared_ptr<const ibBackendQueryColumn>& k : spec.m_keys)
			read.GroupBy(k);
		for (const ibSchemaDelta& d : spec.m_deltas)
			read.Aggregate(ibDataQueryBuilder::AggregateFn::Sum, d.m_column, d.m_column->GetPhysicalName());

		ibDataQueryResult rows = read.SelectAggregate();
		return Drain(rows);
	};

	// COMPARE both ways. A key the movements know and the totals do not is a missed accumulation; a
	// key only the totals carry is a figure nothing accounts for — a leftover from a grouping that
	// changed, or a movement deleted without its trigger. Both are disagreements, and counting only
	// the first direction would call the second one clean.
	const auto disagreeing = [](const ibKeyFigures& expected, const ibKeyFigures& held) {
		int mismatches = 0;
		for (const auto& entry : expected) {
			const auto found = held.find(entry.first);
			if (found == held.end()) { mismatches++; continue; }
			for (size_t i = 0; i < entry.second.size() && i < found->second.size(); i++)
				if (entry.second[i] != found->second[i]) { mismatches++; break; }
		}
		for (const auto& entry : held)
			if (!expected.contains(entry.first))
				mismatches++;
		return mismatches;
	};

	const ibTotalsPeriod day = spec.m_periodUnits.front();
	int mismatches = disagreeing(fromSource, stored(day, prevStart, curStart));

	// ⭐ AND EVERY COARSER UNIT the table keeps — its last elapsed period against the Day rows of the same span, which
	// it is the sum of. A balance is read off the years and the quarters now, so a coarse row that parted from its days
	// is a wrong balance; the days are held to the movements above, and a coarse row to the days costs a year of Day
	// rows rather than a year of movements.
	for (size_t u = 1; u < spec.m_periodUnits.size() && unitCol != nullptr; u++) {
		const ibTotalsPeriod unit = spec.m_periodUnits[u];
		const ibDateTime unitStart = ibDateTime::Now().BeginOfPeriod(unit);
		const ibDateTime unitPrev  = unitStart.AddMilliseconds(-1000).BeginOfPeriod(unit);
		mismatches += disagreeing(stored(day, unitPrev, unitStart), stored(unit, unitPrev, unitStart));
	}

	return mismatches;
}

int CollapseAll(const ibSchemaSnapshot& target, ibDatabaseConnectionHolder* holder, ibRestructureInfo* report)
{
	// NO transaction here. Each fold owns its own, one per SLICE (see Collapse) — which is what keeps the
	// row locks it takes measured in one unit's period rather than in tables. Opening one around the whole
	// sweep would hold a write transaction open for as long as the sweep runs, against a database people are
	// working in.
	//
	// So an interrupted run leaves whole slices folded and the rest untouched. That is a valid state, not a
	// partial one: an unfolded slice reads exactly right, it just reads a few rows wider.
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
