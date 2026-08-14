////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : L2-2 — materialization renderer (derived-state triggers + view)
////////////////////////////////////////////////////////////////////////////

#include "databaseMaterializeBuilder.h"

#include "backend/backend_exception.h"
#include "backend/query/queryException.h"   // ibBackendQueryException — a bundle that will not install is OURS
#include "databaseErrorCodes.h"   // DATABASE_LAYER_QUERY_RESULT_ERROR — a failed CREATE is real
#include "databaseResultSet.h"    // the existence probe reads a row

// The RAM twin of the dialect truncation expressions. Every branch mirrors what the SQL does, and
// the mirroring is the requirement: if these two ever disagree, the same query returns different
// numbers depending on whether the read pushed down or folded in memory — a discrepancy that looks
// like a rounding bug and is not one.
wxDateTime ibNextPeriodStart(const wxDateTime& moment, ibTotalsPeriod unit)
{
	if (!moment.IsValid())
		return moment;

	const wxDateTime start = ibTruncateToPeriod(moment, unit);
	switch (unit) {
		case ibTotalsPeriod::Second:   return start + wxTimeSpan::Seconds(1);
		case ibTotalsPeriod::Minute:   return start + wxTimeSpan::Minutes(1);
		case ibTotalsPeriod::Hour:     return start + wxTimeSpan::Hours(1);
		case ibTotalsPeriod::Day:      return start + wxDateSpan::Days(1);
		case ibTotalsPeriod::Week:     return start + wxDateSpan::Weeks(1);
		case ibTotalsPeriod::TenDays: {
			// The third bucket runs to the END of the month, however long that is — so what follows
			// it is the 1st of the next month, not "ten days later". Same cap as the truncation,
			// read from the other side.
			if (start.GetDay() >= 21) {
				wxDateTime first = start;
				first.SetDay(1);
				return first + wxDateSpan::Months(1);
			}
			return start + wxDateSpan::Days(10);
		}
		case ibTotalsPeriod::Month:    return start + wxDateSpan::Months(1);
		case ibTotalsPeriod::Quarter:  return start + wxDateSpan::Months(3);
		case ibTotalsPeriod::HalfYear: return start + wxDateSpan::Months(6);
		case ibTotalsPeriod::Year:     return start + wxDateSpan::Years(1);
	}
	return start;
}

wxDateTime ibTruncateToPeriod(const wxDateTime& moment, ibTotalsPeriod unit)
{
	if (!moment.IsValid())
		return moment;

	wxDateTime d = moment;
	switch (unit) {
		case ibTotalsPeriod::Second:
			d.SetMillisecond(0);
			return d;
		case ibTotalsPeriod::Minute:
			d.SetMillisecond(0); d.SetSecond(0);
			return d;
		case ibTotalsPeriod::Hour:
			d.SetMillisecond(0); d.SetSecond(0); d.SetMinute(0);
			return d;
		case ibTotalsPeriod::Day:
			return d.GetDateOnly();
		case ibTotalsPeriod::Week: {
			// ISO: back up to Monday. wxDateTime's Sun==0, so Sunday is 6 days past Monday.
			const int wd = static_cast<int>(d.GetWeekDay());
			const int back = (wd == wxDateTime::Sun) ? 6 : (wd - wxDateTime::Mon);
			return d.GetDateOnly() - wxDateSpan::Days(back);
		}
		case ibTotalsPeriod::TenDays: {
			// 1st / 11th / 21st. The offset is CAPPED at 2 for the same reason the SQL caps it: day
			// 31 must fold into the third ten-day period, not open a fourth one-day bucket.
			const int day = d.GetDay();
			const int idx = wxMin((day - 1) / 10, 2);
			wxDateTime first = d.GetDateOnly();
			first.SetDay(1);
			return first + wxDateSpan::Days(idx * 10);
		}
		case ibTotalsPeriod::Month: {
			wxDateTime first = d.GetDateOnly();
			first.SetDay(1);
			return first;
		}
		case ibTotalsPeriod::Quarter: {
			wxDateTime first = d.GetDateOnly();
			first.SetDay(1);
			first.SetMonth(static_cast<wxDateTime::Month>((d.GetMonth() / 3) * 3));
			return first;
		}
		case ibTotalsPeriod::HalfYear: {
			wxDateTime first = d.GetDateOnly();
			first.SetDay(1);
			first.SetMonth(d.GetMonth() < wxDateTime::Jul ? wxDateTime::Jan : wxDateTime::Jul);
			return first;
		}
		case ibTotalsPeriod::Year: {
			wxDateTime first = d.GetDateOnly();
			first.SetDay(1);
			first.SetMonth(wxDateTime::Jan);
			return first;
		}
	}
	return d;
}

namespace {

// ---------------------------------------------------------------------------
// Template filling. The dictionaries use NAMED placeholders rather than printf
// order — a name survives being reordered by a dialect, which is the whole
// reason Firebird can put {table} before {timing} without the renderer knowing.
// ---------------------------------------------------------------------------

wxString Fill(wxString tpl, const wxString& key, const wxString& value)
{
	tpl.Replace(wxT("{") + key + wxT("}"), value);
	return tpl;
}

wxString Join(const std::vector<wxString>& items, const wxString& sep)
{
	wxString out;
	for (size_t i = 0; i < items.size(); i++) {
		if (i != 0) out += sep;
		out += items[i];
	}
	return out;
}

// The suffix a period PROJECTION column carries. Fixed spellings: they become column names on a
// view that scripts and reports address by name, so they are API.
const wxChar* UnitSuffix(ibTotalsPeriod unit)
{
	switch (unit) {
		case ibTotalsPeriod::Second:   return wxT("Second");
		case ibTotalsPeriod::Minute:   return wxT("Minute");
		case ibTotalsPeriod::Hour:     return wxT("Hour");
		case ibTotalsPeriod::Day:      return wxT("Day");
		case ibTotalsPeriod::Week:     return wxT("Week");
		case ibTotalsPeriod::TenDays:  return wxT("TenDays");
		case ibTotalsPeriod::Month:    return wxT("Month");
		case ibTotalsPeriod::Quarter:  return wxT("Quarter");
		case ibTotalsPeriod::HalfYear: return wxT("HalfYear");
		case ibTotalsPeriod::Year:     return wxT("Year");
	}
	return wxT("");
}

// Every unit in COARSENING order — the enum's own contract, restated once so the projection walk
// and the derivability check read the same list.
const ibTotalsPeriod g_units[] = {
	ibTotalsPeriod::Second,  ibTotalsPeriod::Minute,  ibTotalsPeriod::Hour,
	ibTotalsPeriod::Day,     ibTotalsPeriod::Week,    ibTotalsPeriod::TenDays,
	ibTotalsPeriod::Month,   ibTotalsPeriod::Quarter, ibTotalsPeriod::HalfYear,
	ibTotalsPeriod::Year,
};

// Truncate through the QUERY dictionary's map — the same map L2-1 spells an
// ibQueryExprKind::PeriodTrunc node through. One definition of "start of the month" per engine,
// shared by the trigger that keys rows and the view that projects columns, so the two cannot
// drift apart by construction rather than by two authors agreeing.
//
// Throws when the engine has no form for the unit: falling back to a neighbouring unit, or
// leaving the value untruncated, yields a wrong grouping key that still runs, still commits, and
// reconciles to nothing months later.
wxString Truncate(const ibDialectDictionary& dialect, ibTotalsPeriod unit, const wxString& expr)
{
	const auto it = dialect.m_periodTrunc.find(unit);
	if (it == dialect.m_periodTrunc.end()) {
		ibBackendCoreException::Error(_("The database engine cannot truncate a period to '%s'"), UnitSuffix(unit));
		return wxEmptyString;   // not reached — Error throws
	}
	return Fill(it->second, wxT("expr"), expr);
}

// ⭐⭐ WHAT IDENTIFIES A DERIVED ROW — the column and the value it is filled from, as ONE list.
//
// Two currencies for one list. `rowAlias` non-empty reads the values off the MOVEMENT row: that is the
// trigger, where the period is a truncation of the movement's own instant and the shard is an
// expression over the connection. Empty reads them off the derived TABLE, where both are already
// stored — which is what the digest fill after a rebuild needs.
//
// They walk one function because the digest below is an identity only while both walk the same parts
// in the same order. Two copies of this list would compile, run, and hash the same row two ways.
std::vector<std::pair<wxString, wxString>> KeyColumnValues(const ibMaterializeSpec& spec,
                                                           const ibMaterializationDialect& mat,
                                                           const ibDialectDictionary& dialect,
                                                           const wxString& rowAlias,
                                                           unsigned int shards)
{
	const bool overRow = !rowAlias.IsEmpty();
	std::vector<std::pair<wxString, wxString>> out;

	if (!spec.m_periodColumn.IsEmpty()) {
		out.push_back({ spec.m_periodColumn, overRow
			? Truncate(dialect, spec.m_periodUnit, Fill(spec.m_periodSourceExpr, wxT("row"), rowAlias))
			: spec.m_periodColumn });
	}
	for (const wxString& k : spec.m_keyColumns)
		out.push_back({ k, overRow ? (rowAlias + wxT(".") + k) : k });

	if (shards > 1) {
		wxString shardExpr = Fill(mat.m_shardExprTemplate, wxT("conn"), mat.m_connectionIdExpr);
		shardExpr = Fill(shardExpr, wxT("n"), wxString::Format(wxT("%u"), shards));
		out.push_back({ ShardColumnName(), overRow ? shardExpr : wxString(ShardColumnName()) });
	}
	return out;
}

// The digest of that list — empty unless this table carries its identity in one field
// (ibKeyNeedsHash). Each part is hashed BEFORE the parts are joined: a key field may be a binary
// reference key or an unbounded string, and casting either into text to join it is a character-set
// error or a silent truncation, while a hashed part is fixed-width ASCII whatever it holds.
wxString RenderKeyHash(const ibMaterializeSpec& spec, const ibMaterializationDialect& mat,
                       const ibDialectDictionary& dialect, const wxString& rowAlias, unsigned int shards)
{
	if (spec.m_keyHashColumn.IsEmpty() || mat.m_keyHashDigest.IsEmpty())
		return wxEmptyString;

	std::vector<wxString> parts;
	for (const auto& keyColumn : KeyColumnValues(spec, mat, dialect, rowAlias, shards))
		parts.push_back(Fill(mat.m_keyHashItem, wxT("value"), keyColumn.second));
	return Fill(mat.m_keyHashDigest, wxT("expr"), Join(parts, mat.m_keyHashJoin));
}

// ---------------------------------------------------------------------------
// One delta statement: "accumulate this movement into the row for its key".
//
// `rowAlias` is NEW or OLD; `negate` flips the contribution's sign. That pair is what lets ONE
// declaration serve all three events: insert adds the value over NEW, delete adds the NEGATED
// value over OLD, update does both in sequence. The accumulate itself is ALWAYS '+' — the sign
// rides in the VALUE, never in the operator — so no engine needs a second template and a delete
// cannot accidentally be emitted as a replace.
// ---------------------------------------------------------------------------
wxString RenderDelta(const ibMaterializeSpec& spec,
                     const ibMaterializationDialect& mat,
                     const ibDialectDictionary& dialect,
                     const wxString& rowAlias,
                     bool negate,
                     unsigned int shards)
{
	auto overRow = [&](wxString e) { return Fill(std::move(e), wxT("row"), rowAlias); };

	std::vector<wxString> columns, values, keyNames, keyMatch, update, selectItems;

	// --- key columns ---
	for (const auto& keyColumn : KeyColumnValues(spec, mat, dialect, rowAlias, shards)) {
		columns.push_back(keyColumn.first);
		values.push_back(keyColumn.second);
		keyNames.push_back(keyColumn.first);
	}

	// ⭐⭐ THE KEY'S IDENTITY IN ONE FIELD — declared only where an index cannot hold the key itself
	// (ibKeyNeedsHash), and digested from the very expressions the key columns are filled with, so the
	// stored digest and the stored key cannot describe different rows.
	//
	// It joins {columns} / {values} and NOT {keyNames}: the match goes on comparing the key columns
	// (exact), and the digest only carries the UNIQUE index. It is also absent from {update} — a
	// matched row keeps the key it was inserted with, so its digest is already right, and re-deriving
	// one on every movement would be work with no answer to give.
	const wxString keyHashValue = RenderKeyHash(spec, mat, dialect, rowAlias, shards);
	if (!keyHashValue.IsEmpty()) {
		columns.push_back(spec.m_keyHashColumn);
		values.push_back(keyHashValue);
	}

	// --- accumulating columns ---
	const wxString target = Fill(mat.m_deltaTargetAlias, wxT("table"), spec.m_table);
	const wxString source = mat.m_deltaSourceAlias;

	for (const ibMaterializeDelta& d : spec.m_deltas) {
		wxString value = overRow(d.m_valueExpr);
		if (negate)
			value = wxT("-(") + value + wxT(")");
		columns.push_back(d.m_column);
		values.push_back(value);

		wxString item = Fill(mat.m_deltaUpdateItem, wxT("col"), d.m_column);
		item = Fill(item, wxT("target"), target);
		item = Fill(item, wxT("source"), source);
		update.push_back(item);
	}

	for (const wxString& k : keyNames) {
		wxString item = Fill(mat.m_deltaKeyMatchItem, wxT("col"), k);
		item = Fill(item, wxT("target"), target);
		item = Fill(item, wxT("source"), source);
		keyMatch.push_back(item);
	}

	for (size_t i = 0; i < columns.size(); i++)
		selectItems.push_back(values[i] + wxT(" AS ") + columns[i]);

	// A guarded delta is a SELECT with a WHERE, and a source-less SELECT cannot carry one on every
	// engine — hence the dummy relation, whose spelling already lives in the query dictionary
	// (RDB$DATABASE / DUAL) and is therefore not restated in the materialization one.
	const wxString guard = overRow(spec.m_guard);
	wxString from, where;
	if (!dialect.m_selectFromDual.IsEmpty())
		from = wxT(" FROM ") + dialect.m_selectFromDual;
	if (!guard.IsEmpty())
		where = wxT(" WHERE ") + guard;

	wxString sourceRel;
	if (!mat.m_deltaSourceTemplate.IsEmpty()) {
		sourceRel = Fill(mat.m_deltaSourceTemplate, wxT("selectItems"), Join(selectItems, wxT(", ")));
		sourceRel = Fill(sourceRel, wxT("from"),  from);
		sourceRel = Fill(sourceRel, wxT("where"), where);
	}

	wxString sql = mat.m_deltaUpsertTemplate;
	sql = Fill(sql, wxT("table"),     spec.m_table);
	sql = Fill(sql, wxT("columns"),   Join(columns, wxT(", ")));
	sql = Fill(sql, wxT("values"),    Join(values, wxT(", ")));
	// {keys} is the CONFLICT TARGET, and a conflict target has to name an existing unique index. Where
	// the key's uniqueness moved into the digest column, that is the index there is — so the engines
	// that spell ON CONFLICT (<columns>) name it, and the ones that match by their own ON clause
	// (Firebird's MERGE) never look at this placeholder at all.
	sql = Fill(sql, wxT("keys"),      keyHashValue.IsEmpty() ? Join(keyNames, wxT(", ")) : spec.m_keyHashColumn);
	sql = Fill(sql, wxT("update"),    Join(update, wxT(", ")));
	sql = Fill(sql, wxT("keyMatch"),  Join(keyMatch, wxT(" AND ")));
	sql = Fill(sql, wxT("sourceRel"), sourceRel);
	sql = Fill(sql, wxT("target"),    target);
	sql = Fill(sql, wxT("source"),    source);
	sql = Fill(sql, wxT("from"),      from);
	sql = Fill(sql, wxT("where"),     where);
	return sql;
}

// One trigger: shell (+ a separate function object where the engine needs one) around a body of
// N delta statements. N, not one: a single movement can feed SEVERAL derived tables — an
// accounting line carries a debit side and a credit side, each accumulating into its own totals —
// and all of them land inside the one transaction that wrote the movement.
void RenderTrigger(ibMaterializeSql& out,
                   const ibMaterializationDialect& mat,
                   const wxString& name,
                   const wxString& sourceTable,
                   const wxString& timing,
                   const std::vector<wxString>& statements)
{
	wxString body;
	for (const wxString& s : statements)
		body += s + mat.m_bodyStatementTerminator;

	wxString functionName;
	if (!mat.m_functionShellTemplate.IsEmpty()) {
		functionName = wxT("fn_") + name;
		wxString fn = Fill(mat.m_functionShellTemplate, wxT("name"), functionName);
		fn = Fill(fn, wxT("body"), body);
		out.AddCreate(fn);
	}

	wxString sql = Fill(mat.m_triggerShellTemplate, wxT("name"), name);
	sql = Fill(sql, wxT("table"),    sourceTable);
	sql = Fill(sql, wxT("timing"),   timing);
	sql = Fill(sql, wxT("body"),     body);
	sql = Fill(sql, wxT("function"), functionName);
	out.AddCreate(sql);
}

// The READ view — a THIN projection of the derived table, never a re-aggregation of the source.
// Thinness is load-bearing: a thin view co-locates and stays an index lookup, while a view that
// re-aggregated the movements would JOIN just as well and recompute on every read, leaving the
// scale ceiling exactly where it was. JOINability comes from the table shape; speed comes from
// the trigger-maintained table underneath. Two separable properties, and the view needs both.
//
// Its extra columns are the period PROJECTIONS — every unit coarser than the stored one — so a
// consumer groups by month or quarter inside one query without a second source. A split table
// additionally folds its shards here (SUM + GROUP BY), which is what keeps the split invisible:
// nothing upstream can tell a split table from a plain one.
// A resource column as the view reads it. SPLIT totals spread one logical key over N rows, so every
// read of a split table is a SUM over its shards — and that is the only place the split is visible.
// Above the view nothing can tell a split table from a plain one, which is what makes the split a
// storage decision rather than a schema one.
wxString Fold(const wxString& column, bool collapsing)
{
	return collapsing ? (wxT("SUM(") + column + wxT(")")) : column;
}

// The period column plus every COARSER unit as its own projection, so one query can group by month
// or quarter without a second source. Finer than stored is absent by construction — that
// information does not exist, and a column silently repeating the month under the name "day" would
// be worse than its absence.
void AppendPeriodColumns(const ibMaterializeSpec& spec, const ibDialectDictionary& dialect,
                         std::vector<wxString>& select, std::vector<wxString>& groupBy,
                         const wxString& periodExpr = wxEmptyString)
{
	if (spec.m_periodColumn.IsEmpty())
		return;

	// The period reads differently per ARM: a stored row carries it in its own column, a movement
	// carries the raw instant. Everything else -- the projections, their names, the grouping -- is
	// the same sentence over whichever expression that is, which is why the arm passes it in rather
	// than each arm growing its own copy of this walk.
	const wxString base = periodExpr.IsEmpty() ? spec.m_periodColumn : periodExpr;

	select.push_back(base + (periodExpr.IsEmpty() ? wxString() : (wxT(" AS ") + spec.m_periodColumn)));
	groupBy.push_back(base);
	for (const ibTotalsPeriod unit : g_units) {
		if (unit <= spec.m_periodUnit)
			continue;
		const wxString expr = Truncate(dialect, unit, base);
		select.push_back(expr + wxT(" AS ") + spec.m_periodColumn + wxT("_") + UnitSuffix(unit));
		groupBy.push_back(expr);
	}
}

// ---------------------------------------------------------------------------
// The movement arm of a union view: every movement, as the contribution it makes.
//
// It is rendered from `spec.m_deltas` -- the very expressions the trigger accumulates through -- so
// a movement counted here and the same movement counted into the totals are the same arithmetic by
// construction, not by two files agreeing. Nothing in this function knows what the figures mean.
//
// The period is the RAW instant rather than a truncation: that is the whole reason this arm exists.
// The stored arm answers down to the grain; below it, only a movement knows when it happened.
// ---------------------------------------------------------------------------
wxString RenderMovementArm(const ibMaterializeSpec& spec,
                           const ibMaterializeView& view,
                           const ibDialectDictionary& dialect)
{
	auto overRow = [&](wxString e) { return Fill(std::move(e), wxT("row"), spec.m_source); };

	// What a movement contributes to a stored column -- looked up, never re-derived. A column with
	// no delta contributes nothing, which is the honest answer for a scaffold column.
	auto contribution = [&](const wxString& column) -> wxString {
		for (const ibMaterializeDelta& d : spec.m_deltas)
			if (d.m_column == column)
				return wxT("(") + overRow(d.m_valueExpr) + wxT(")");
		return wxT("0");
	};

	std::vector<wxString> select, unusedGroupBy;
	if (view.m_withPeriod)
		AppendPeriodColumns(spec, dialect, select, unusedGroupBy, overRow(spec.m_periodSourceExpr));
	for (const wxString& k : spec.m_keyColumns)
		select.push_back(spec.m_source + wxT(".") + k);

	for (const ibMaterializeViewColumn& c : view.m_columns) {
		const wxString a = contribution(c.m_columnA);
		const wxString b = c.m_columnB.IsEmpty() ? wxString() : contribution(c.m_columnB);
		const wxString diff = b.IsEmpty() ? a : (wxT("(") + a + wxT(" - ") + b + wxT(")"));

		// ⚠ A RUNNING FORM HAS NO MEANING ON THIS ARM. A running sum is defined over the ordered
		// stored rows; a movement is one row of the tail. A view that needs both is two views.
		select.push_back(diff + wxT(" AS ") + c.m_alias);
	}

	for (const auto& c : view.m_movementColumns)
		select.push_back(spec.m_source + wxT(".") + c.first);

	// The SAME guard the trigger accumulates under. The stored arm holds only what was in force
	// (the trigger saw to that); the movement arm has to apply the condition itself, or the tail
	// would add rows the totals below it deliberately never counted.
	wxString body = wxT("SELECT ") + Join(select, wxT(", ")) + wxT(" FROM ") + spec.m_source;
	if (!spec.m_guard.IsEmpty())
		body += wxT(" WHERE ") + overRow(spec.m_guard);

	return body;
}

// ---------------------------------------------------------------------------
// ONE view renderer, driven entirely by the caller's description.
//
// It composes PRIMITIVES — a shard fold, a difference, a running sum — and never learns what they
// add up to. "Balance", "turnover", "opening" are metadata's words; an accounting register will
// compose the same primitives into debit / credit turnovers and per-dimension breakdowns without
// this file gaining a line. That independence is the requirement: level 2 must not know which
// tables exist above it.
// ---------------------------------------------------------------------------
wxString RenderView(const ibMaterializeSpec& spec,
                    const ibMaterializeView& view,
                    const ibMaterializationDialect& mat,
                    const ibDialectDictionary& dialect,
                    unsigned int shards)
{
	// Does this view MERGE stored rows? Two independent reasons, and either one means every stored
	// column must be summed rather than read:
	//   * split totals — one logical key lives across N shard rows;
	//   * a view without the period — dropping a key column genuinely merges every period into one.
	// Miss this and a balance view reports whatever single period the engine happened to return.
	const bool collapses = (shards > 1) || !view.m_withPeriod;

	std::vector<wxString> select, groupBy;
	if (view.m_withPeriod)
		AppendPeriodColumns(spec, dialect, select, groupBy);
	for (const wxString& k : spec.m_keyColumns) { select.push_back(k); groupBy.push_back(k); }

	// The window every running form is evaluated over: partition by the key, ordered by the period.
	// The DATABASE walks the periods — the alternative is carrying every row out to a client and
	// accumulating there, which turns a report into a transfer.
	const wxString over = wxT(" OVER (PARTITION BY ") + Join(spec.m_keyColumns, wxT(", "))
	                    + (spec.m_periodColumn.IsEmpty() ? wxString() : (wxT(" ORDER BY ") + spec.m_periodColumn))
	                    + wxT(")");

	wxString nonZero;
	for (const ibMaterializeViewColumn& c : view.m_columns) {
		const wxString a = Fold(c.m_columnA, collapses);
		const wxString b = c.m_columnB.IsEmpty() ? wxString() : Fold(c.m_columnB, collapses);
		const wxString diff = b.IsEmpty() ? a : (wxT("(") + a + wxT(" - ") + b + wxT(")"));

		wxString expr;
		switch (c.m_agg) {
			case ibMaterializeAgg::Value:      expr = a;    break;
			case ibMaterializeAgg::Difference: expr = diff; break;
			case ibMaterializeAgg::RunningSum:
				expr = wxT("SUM(") + diff + wxT(")") + over;
				break;
			case ibMaterializeAgg::RunningSumExcludingCurrent:
				// The running sum through this row, minus this row's own contribution. One window
				// instead of two, and no self-join: the value entering a period is what left the
				// previous one, by construction.
				expr = wxT("(SUM(") + diff + wxT(")") + over + wxT(" - ") + diff + wxT(")");
				break;
		}
		select.push_back(expr + wxT(" AS ") + c.m_alias);

		if (view.m_dropZeroRows) {
			// Survives if ANY column is non-zero — an item with quantity 0 but amount 5 still
			// reports. All-zero means NO ROW, not a row of zeros: the latter lists everything ever
			// touched instead of what is actually there. Windowed forms are excluded because a
			// window cannot appear in HAVING.
			if (c.m_agg == ibMaterializeAgg::Value || c.m_agg == ibMaterializeAgg::Difference) {
				const wxString nz = expr + wxT(" <> 0");
				nonZero = nonZero.IsEmpty() ? nz : (nonZero + wxT(" OR ") + nz);
			}
		}
	}

	// A movement-only column is absent from the stored arm, and NULL is how a union says so — but a
	// TYPED null, never a bare one. A bare NULL carries no type for the engine to reconcile the arms
	// with, and Firebird answers "expression evaluation not supported" when it compiles the view,
	// which it does at COMMIT rather than at CREATE (see m_movementColumns).
	for (const auto& c : view.m_movementColumns)
		select.push_back(wxT("CAST(NULL AS ") + ibMapColumnType(dialect, c.second) + wxT(") AS ") + c.first);

	// GROUP BY only when something actually merges (see `collapses` above). An unsplit,
	// period-carrying view already holds one row per key, and grouping it would buy nothing while
	// costing the engine a sort or a hash on every single read.
	wxString body = wxT("SELECT ") + Join(select, wxT(", ")) + wxT(" FROM ") + spec.m_table;
	if (collapses && !groupBy.empty())
		body += wxT(" GROUP BY ") + Join(groupBy, wxT(", "));
	if (collapses && !nonZero.IsEmpty())
		body += wxT(" HAVING ") + nonZero;
	else if (!nonZero.IsEmpty())
		body += wxT(" WHERE ") + nonZero;   // nothing was grouped, so the same test belongs in WHERE

	if (view.m_withMovements)
		body += wxT(" UNION ALL ") + RenderMovementArm(spec, view, dialect);

	return Fill(Fill(mat.m_createViewTemplate, wxT("name"), view.m_name), wxT("body"), body);
}

// Every object the bundle for this spec owns, as DROP statements.
//
// Rendered from the SPEC, which means the names are the ones that spec would have created — so
// removing a bundle that is being replaced and removing one that is going away for good are the
// same call over a different spec. The view goes first: it depends on the table, and on the
// trigger's column set through it.
void RenderDrops(ibMaterializeSql& out, const ibMaterializeSpec& spec, const ibMaterializationDialect& mat)
{
	for (const ibMaterializeView& v : spec.m_views)
		if (!v.m_name.IsEmpty())
			out.AddDrop(Fill(mat.m_dropViewTemplate, wxT("name"), v.m_name),
			            Fill(mat.m_viewExistsQuery, wxT("name"), v.m_name));

	for (const wxString& n : { spec.m_table + wxT("_AI"), spec.m_table + wxT("_AU"), spec.m_table + wxT("_AD") }) {
		out.AddDrop(Fill(Fill(mat.m_dropTriggerTemplate, wxT("name"), n), wxT("table"), spec.m_source),
		            Fill(mat.m_triggerExistsQuery, wxT("name"), n));
		if (!mat.m_dropFunctionTemplate.IsEmpty())
			out.AddDrop(Fill(mat.m_dropFunctionTemplate, wxT("name"), wxT("fn_") + n));
	}
}

} // namespace

const wxChar* ShardColumnName()   { return wxT("shard_"); }
const wxChar* KeyHashColumnName() { return wxT("keyhash_"); }

unsigned int ibIndexFieldCapacity(const ibDatabaseLayer& conn)
{
	return conn.GetDialect().m_maxIndexSegments;
}

bool ibKeyNeedsHash(const ibDatabaseLayer& conn, size_t keyFieldCount)
{
	const unsigned int ceiling = ibIndexFieldCapacity(conn);
	if (ceiling == 0 || keyFieldCount <= ceiling)
		return false;   // no ceiling declared, or the key is under it — the plain unique index stands

	// The ceiling is passed and the engine has no digest to offer. Say so by saying NO: the caller
	// then declares the index it meant, and the engine refuses it at apply time in its own words. A
	// hash column nothing fills would be worse — a UNIQUE index over a column of NULLs enforces
	// nothing on any engine, and it LOOKS like it enforces the key.
	const ibMaterializationDialect* mat = conn.GetMaterializationDialect();
	return mat != nullptr && !mat->m_keyHashDigest.IsEmpty();
}

bool ibFillKeyHashes(ibDatabaseLayer& conn, const ibMaterializeSpec& spec,
                     const ibMaterializationDialect* dialectOrNull)
{
	const ibMaterializationDialect* mat = dialectOrNull != nullptr ? dialectOrNull : conn.GetMaterializationDialect();
	if (mat == nullptr || spec.m_table.IsEmpty() || spec.m_keyHashColumn.IsEmpty())
		return true;   // nothing carries an identity here — not a failure

	const wxString digest = RenderKeyHash(spec, *mat, conn.GetDialect(), wxEmptyString,
	                                      EffectiveShardCount(spec, mat));
	if (digest.IsEmpty())
		return true;

	// Only the rows that have none. A row the TRIGGER wrote already carries its digest, and rewriting
	// it would be an update per movement-bearing key for nothing; a row a REBUILD wrote has an empty
	// one, because a rebuild writes through the ordinary door and knows nothing about an identity the
	// database computes.
	//
	// It is an UPDATE and not a constraint because the value is a function of the row: read off the
	// table's own key columns, digested by the same expression the trigger uses over the movement row
	// (KeyColumnValues walks one list for both), so a filled row and a triggered row are the same row.
	//
	// ⚠ MINTED, NOT HANDED OVER. The text is assembled here and then becomes an ibRenderedStatement —
	// the type only this renderer can construct — and runs through the same Apply every other
	// statement of the bundle runs through. Calling the connection with a string built by hand would
	// be exactly the raw-L1 door this level exists to close: the rule that no SQL string reaches a
	// driver except through here is held by the type, and a bypass costs nothing until the day
	// something else copies it.
	ibMaterializeSql fill;
	fill.AddCreate(wxT("UPDATE ") + spec.m_table + wxT(" SET ") + spec.m_keyHashColumn +
	               wxT(" = ") + digest +
	               wxT(" WHERE ") + spec.m_keyHashColumn + wxT(" IS NULL"));
	return fill.Apply(conn);
}

unsigned int EffectiveShardCount(const ibMaterializeSpec& spec, const ibMaterializationDialect* mat)
{
	if (mat == nullptr || mat->m_connectionIdExpr.IsEmpty())
		return 1;   // nothing to hash => one shard; not a failure, the split is simply pointless here
	return spec.m_shards > 0 ? spec.m_shards : 1;
}

ibMaterializeSql RenderMaterialization(const ibMaterializeSpec& spec,
                                       const ibMaterializationDialect* mat,
                                       const ibDialectDictionary& dialect)
{
	ibMaterializeSql out;
	if (mat == nullptr)
		return out;   // driver cannot maintain derived state (ODBC) — live aggregation stands in
	if (spec.m_table.IsEmpty() || spec.m_source.IsEmpty())
		return out;   // nothing to maintain, or nothing to hang the triggers on

	// --- DROP first, and UNCONDITIONALLY. The bundle is always replaced WHOLE rather than patched:
	// a trigger body carries the column list it was generated with, so any structural change
	// silently invalidates it. Recreating is cheap — no data moves, the accumulated rows are
	// untouched — and it removes the entire class of "trigger still writing the old column set" bugs.
	//
	// Before the early exit below, not after: a register that LOST its last resource has nothing to
	// create, and that is exactly when a surviving trigger is most harmful — it would keep writing
	// columns the same apply just dropped.
	RenderDrops(out, spec, *mat);

	// NOTHING TO ACCUMULATE — a register with no resources yet (freshly created in the designer,
	// or one that only ever had dimensions). There is no delta to apply, so there is no
	// maintenance to install: an empty accumulate list would render `UPDATE SET` with no
	// assignments, which is not a statement any engine accepts. Dropping and creating nothing is
	// the honest answer — the totals table simply has nothing to keep current.
	if (spec.m_deltas.empty())
		return out;

	const unsigned int shards = EffectiveShardCount(spec, mat);

	const wxString nameAI = spec.m_table + wxT("_AI");
	const wxString nameAU = spec.m_table + wxT("_AU");
	const wxString nameAD = spec.m_table + wxT("_AD");

	// --- CREATE. The three events differ ONLY in which row the delta reads and which sign it
	// carries, which is why one declaration covers all of them.
	const wxString plusNew  = RenderDelta(spec, *mat, dialect, mat->m_newRowAlias, false, shards);
	const wxString minusOld = RenderDelta(spec, *mat, dialect, mat->m_oldRowAlias, true,  shards);

	RenderTrigger(out, *mat, nameAI, spec.m_source, mat->m_timingInsert, { plusNew });
	RenderTrigger(out, *mat, nameAU, spec.m_source, mat->m_timingUpdate, { minusOld, plusNew });
	RenderTrigger(out, *mat, nameAD, spec.m_source, mat->m_timingDelete, { minusOld });

	// The READ views — the BRIDGE. Above each one: an ordinary relation, joined and filtered by the
	// engine's own machinery with no special case anywhere upstream. Below: the materialisation it
	// hides — that the numbers come from a trigger-maintained table rather than a scan, and that the
	// table may be split across shards. Whatever the caller composed them to mean, this loop does
	// not know; it renders primitives.
	for (const ibMaterializeView& v : spec.m_views)
		if (!v.m_name.IsEmpty())
			out.AddCreate(RenderView(spec, v, *mat, dialect, shards));
	return out;
}

// ============================================================================
// READING the materialised surface — L2-2's other half.
//
// The metaobject says WHICH figures it wants and over WHAT range; every expression that follows is
// engine business and lives here. Previously this was assembled as L2-1 IR up in metadata, which
// put SQL shaping in the layer that should only be naming columns.
// ============================================================================

namespace {

// The predicate a conditional column is summed under. Unbounded sides simply drop out — an absent
// interval start makes "before the start" empty, which is the honest reading of "no start given".
ibQueryExprPtr WhenPredicate(const ibMaterializeReadSpec& spec, ibMaterializeWhen when)
{
	const bool hasFrom = (spec.m_from.GetType() == TYPE_DATE);
	const bool hasTo   = (spec.m_to.GetType()   == TYPE_DATE);
	const ibQueryExprPtr period = ibCol(spec.m_periodColumn);

	switch (when) {
		case ibMaterializeWhen::Always:
			return nullptr;
		case ibMaterializeWhen::BeforeFrom:
			return hasFrom ? ibBinOp(ibQueryBinOp::Lt, period, ibConst(spec.m_from)) : nullptr;
		case ibMaterializeWhen::UpToTo:
			return hasTo ? ibBinOp(ibQueryBinOp::Le, period, ibConst(spec.m_to)) : nullptr;
		case ibMaterializeWhen::InRange: {
			ibQueryExprPtr p;
			if (hasFrom) p = ibBinOp(ibQueryBinOp::Ge, period, ibConst(spec.m_from));
			if (hasTo) {
				ibQueryExprPtr hi = ibBinOp(ibQueryBinOp::Le, period, ibConst(spec.m_to));
				p = p ? ibBinOp(ibQueryBinOp::And, p, hi) : hi;
			}
			return p;
		}
	}
	return nullptr;
}

} // namespace

// (a, b, c) <= (x, y, z) spelled out: a < x OR (a = x AND (b < y OR (b = y AND c <= z))). Every
// engine takes it, and it is the SAME order the boundary value compares by — one order, so a
// balance computed on the server and one computed in memory cannot disagree about which of two
// documents at the same instant came first.
static ibQueryExprPtr TupleCompare(const std::vector<std::pair<wxString, ibQueryExprPtr>>& tail,
                                   size_t i, bool atMost, bool excluding = false)
{
	const ibQueryExprPtr col = tail[i].second ? ibCol(tail[i].first) : nullptr;
	const ibQueryExprPtr val = tail[i].second;
	if (col == nullptr)
		return nullptr;

	if (i + 1 == tail.size()) {
		const ibQueryBinOp last = excluding ? (atMost ? ibQueryBinOp::Lt : ibQueryBinOp::Gt)
		                                    : (atMost ? ibQueryBinOp::Le : ibQueryBinOp::Ge);
		return ibBinOp(last, col, val);
	}

	const ibQueryExprPtr rest = TupleCompare(tail, i + 1, atMost, excluding);
	return ibBinOp(ibQueryBinOp::Or,
		ibBinOp(atMost ? ibQueryBinOp::Lt : ibQueryBinOp::Gt, col, val),
		ibBinOp(ibQueryBinOp::And, ibBinOp(ibQueryBinOp::Eq, col, val), rest));
}

// "at this instant, no later than this document" -- the period compared first, the tuple only when
// the instants are equal. That is the whole of what a moment adds to a date.
static ibQueryExprPtr BoundedByMoment(const ibQueryExprPtr& period, const ibValue& at,
                                      const std::vector<std::pair<wxString, ibQueryExprPtr>>& tail,
                                      bool atMost, bool excluding)
{
	// EXCLUDING moves the operator, not the value. Nudging the instant by a second instead would be
	// a guess about the storage grain, and it would still include a document recorded in that
	// second — the very row the caller said to leave out.
	const ibQueryBinOp closed = atMost ? ibQueryBinOp::Le : ibQueryBinOp::Ge;
	const ibQueryBinOp open   = atMost ? ibQueryBinOp::Lt : ibQueryBinOp::Gt;

	const ibQueryExprPtr edge = ibConst(at);
	const ibQueryExprPtr plain = ibBinOp(excluding ? open : closed, period, edge);
	if (tail.empty())
		return plain;

	// Inside the same instant the tail decides — and EXCLUDING there means the named document
	// itself is out, so its own comparison opens while everything before it stays in.
	const ibQueryExprPtr inside = TupleCompare(tail, 0, atMost, excluding);
	if (!inside)
		return plain;

	return ibBinOp(ibQueryBinOp::Or,
		ibBinOp(open, period, edge),
		ibBinOp(ibQueryBinOp::And, ibBinOp(ibQueryBinOp::Eq, period, edge), inside));
}

ibQueryRelPtr RenderMaterializedRead(const ibMaterializeReadSpec& spec, const wxString& alias)
{
	const ibQueryExprPtr zero = ibConst(ibValue(0.0));

	std::vector<ibQueryProjItem> proj;
	std::vector<ibQueryExprPtr>  keys;
	for (const wxString& k : spec.m_keyColumns) {
		proj.push_back(ibQueryProjItem{ ibCol(k), wxString() });
		keys.push_back(ibCol(k));
	}

	bool anyAggregate = false;
	ibQueryExprPtr nonZero;

	for (const ibMaterializeReadColumn& c : spec.m_columns) {
		// The value before any conditioning: a stored column, or the difference of two.
		ibQueryExprPtr value = ibCol(c.m_columnA);
		if (c.m_agg == ibMaterializeAgg::Difference && !c.m_columnB.IsEmpty())
			value = ibBinOp(ibQueryBinOp::Sub, ibCol(c.m_columnA), ibCol(c.m_columnB));

		// Conditioning turns "sum this column" into "sum it only for the rows that count" — which
		// is how one pass yields an opening balance, an interval turnover and a closing balance
		// side by side instead of three scans or a full outer join.
		if (ibQueryExprPtr when = WhenPredicate(spec, c.m_when))
			value = ibCase({ { when, value } }, zero);

		if (c.m_aggregate) {
			// Pin the width: a bare SUM narrows to (9,0)/(10,0) on Firebird and MySQL and silently
			// truncates the fraction.
			value = ibCast(ibFunc(wxT("SUM"), { value }), ibTypeNumber(18, 6));
			anyAggregate = true;
			if (spec.m_dropZeroRows) {
				ibQueryExprPtr nz = ibBinOp(ibQueryBinOp::Ne, value, zero);
				nonZero = nonZero ? ibBinOp(ibQueryBinOp::Or, nonZero, nz) : nz;
			}
		}
		proj.push_back(ibQueryProjItem{ value, c.m_alias });
	}

	// The period column rides through unaggregated reads: the caller groups or reports by it.
	if (!anyAggregate && !spec.m_periodColumn.IsEmpty())
		proj.insert(proj.begin(), ibQueryProjItem{ ibCol(spec.m_periodColumn), wxString() });

	ibDatabaseQueryBuilder q;
	q.From(spec.m_view).Project(proj);

	// The OUTER bound of the scan. Rows past the interval belong to no reported figure, so they are
	// excluded before grouping rather than conditioned away column by column.
	if (spec.m_to.GetType() == TYPE_DATE && !spec.m_periodColumn.IsEmpty())
		q.Where(ibBinOp(ibQueryBinOp::Le, ibCol(spec.m_periodColumn), ibConst(spec.m_to)));
	if (!anyAggregate && spec.m_from.GetType() == TYPE_DATE && !spec.m_periodColumn.IsEmpty())
		q.Where(ibBinOp(ibQueryBinOp::Ge, ibCol(spec.m_periodColumn), ibConst(spec.m_from)));

	// ⭐⭐ THE CUT BETWEEN THE ARMS — the one place this whole arrangement can lie.
	//
	// Stored rows are complete up to the grain boundary; movements carry everything after it. Take
	// stored rows BELOW the floor and movement rows from the floor onward and each movement is
	// counted exactly once — by the trigger below the line, by itself above it. Drop the floor and
	// the current grain arrives twice, which reads as a balance that is quietly too large.
	if (!spec.m_markColumn.IsEmpty() && !spec.m_periodColumn.IsEmpty()) {
		const ibQueryExprPtr period    = ibCol(spec.m_periodColumn);
		const ibQueryExprPtr fromTotal = ibIsNull(ibCol(spec.m_markColumn));

		if (spec.m_floor.GetType() != TYPE_DATE) {
			// The question stops at the grain or above it. The movements have nothing to add that
			// the totals do not already hold, so the arm is excluded — one predicate, and the
			// planner never touches that side of the union.
			q.Where(fromTotal);
		}
		else {
			// THE STORED ARM TAKES WHOLE GRAINS ONLY -- those that begin at or after the head split
			// and end before the floor. A partial grain at either end is the movements' business,
			// because a stored row cannot be asked for half of itself.
			ibQueryExprPtr stored = ibBinOp(ibQueryBinOp::And, fromTotal,
				ibBinOp(ibQueryBinOp::Lt, period, ibConst(spec.m_floor)));
			if (spec.m_headSplit.GetType() == TYPE_DATE)
				stored = ibBinOp(ibQueryBinOp::And, stored,
					ibBinOp(ibQueryBinOp::Ge, period, ibConst(spec.m_headSplit)));

			// THE MOVEMENT ARM TAKES THE PARTIAL ENDS -- and only them. Written as one range minus
			// the middle rather than two ranges, because two ranges OVERLAP when the whole interval
			// fits inside a single grain, and an overlap here is a movement counted twice.
			ibQueryExprPtr moved = ibIsNull(ibCol(spec.m_markColumn), /*negated*/ true);

			ibQueryExprPtr ends = ibBinOp(ibQueryBinOp::Ge, period, ibConst(spec.m_floor));
			if (spec.m_headSplit.GetType() == TYPE_DATE)
				ends = ibBinOp(ibQueryBinOp::Or, ends,
					ibBinOp(ibQueryBinOp::Lt, period, ibConst(spec.m_headSplit)));
			moved = ibBinOp(ibQueryBinOp::And, moved, ends);

			// The boundaries themselves: an instant, or an instant AND the document at it.
			if (spec.m_to.GetType() == TYPE_DATE)
				moved = ibBinOp(ibQueryBinOp::And, moved,
					BoundedByMoment(period, spec.m_to, spec.m_boundaryTail, /*atMost*/ true, spec.m_toExcluding));
			if (spec.m_from.GetType() == TYPE_DATE)
				moved = ibBinOp(ibQueryBinOp::And, moved,
					BoundedByMoment(period, spec.m_from, spec.m_boundaryHead, /*atMost*/ false, spec.m_fromExcluding));

			q.Where(ibBinOp(ibQueryBinOp::Or, stored, moved));
		}
	}

	for (const auto& f : spec.m_filters)
		q.Where(ibBinOp(ibQueryBinOp::Eq, ibCol(f.first), ibConst(f.second)));

	if (anyAggregate) {
		for (const ibQueryExprPtr& k : keys) q.GroupBy(k);
		if (nonZero) q.Having(nonZero);
	}

	return ibSubquery(q.Build().m_root, alias);
}

// Does the object a guarded drop targets actually exist? A read, never DDL — so it cannot fail the
// way a speculative DROP does, and the transaction stays intact whatever the answer.
static bool ExistsByProbe(ibDatabaseLayer& conn, const wxString& probe)
{
	// "NOT THERE" AND "COULD NOT ASK" ARE OPPOSITE ANSWERS, and this used to return both as `false`.
	// The probe reads a system table, so it does not fail on an absent object — it returns no rows.
	// It fails when the TRANSACTION is already broken, and then every later probe fails too: each one
	// reports "absent", each guarded DROP is skipped, and the views and triggers stay standing over
	// the table the apply is about to drop. The database then refuses with "there are 1 dependencies"
	// and the first fault arrives disguised as a second, unrelated one.
	//
	// So a probe that cannot run RAISES. That is the honest reading, and it is also the one that
	// stops the restructuring where the fault is instead of letting it walk on half-applied.
	ibDatabaseResultSet* rs = nullptr;
	try {
		rs = conn.RunQueryWithResults(probe);
	}
	catch (const ibBackendException&) {
		conn.ResetErrorCodes();
		throw;
	}
	const bool found = (rs != nullptr && rs->Next());
	if (rs != nullptr)
		conn.CloseResultSet(rs);
	conn.ResetErrorCodes();
	return found;
}

// Execution stays at the level that rendered the text — the same arrangement as
// ibDatabaseQueryBuilder running what it built. No SQL string crosses out of L2-2.
bool ibMaterializeSql::Apply(ibDatabaseLayer& conn) const
{
	// A drop must not be ATTEMPTED unless the object is there. Swallowing the failure is not
	// enough: on Firebird a failed DDL rolls the transaction back, so dropping something that was
	// never created takes the entire restructuring down — the first apply would destroy itself,
	// and every later statement would run in a dead transaction.
	//
	// So a guarded drop asks first. Engines with DROP … IF EXISTS carry no guard and just run.
	for (const ibRenderedStatement& s : m_drop) {
		if (!s.m_guard.IsEmpty() && !ExistsByProbe(conn, s.m_guard)) {
			// ⚠ A SKIPPED DROP IS THE ONE WAY AN OBJECT SURVIVES A TABLE IT DEPENDS ON. The probe says
			// "not there", the drop is passed over, and if the probe was wrong the view stays standing
			// over a table the differ is about to remove — which surfaces much later, and elsewhere, as
			// "cannot delete TABLE …: there are 1 dependencies". The skip is silent by design (a first
			// apply has nothing to drop); this note is here so the silence is not mistaken for proof.
			continue;
		}
		// "Never fatal" has to hold however the DRIVER reports the failure — and it is the
		// EXCEPTION that reports it. See the note on the create loop below.
		try {
			conn.RunStatement(s.m_sql);
		}
		catch (const ibBackendException&) {
			conn.ResetErrorCodes();   // the object was not there — that is the normal first apply
		}
	}

	// A failed CREATE is real: the maintenance would silently not exist, and the totals would drift
	// from the movements with nothing to signal it.
	//
	// FAILURE IS AN EXCEPTION, NOT A RETURN CODE — this loop used to test
	// `== DATABASE_LAYER_QUERY_RESULT_ERROR` and got BOTH halves wrong:
	//   * that constant is 0, while RunStatement returns the AFFECTED-ROW COUNT — which for DDL
	//     (CREATE TRIGGER / CREATE VIEW) is legitimately 0. So every successful create read as a
	//     failure and Apply returned false having installed the bundle perfectly.
	//   * a real failure never reaches the comparison anyway: the driver throws
	//     (ibDatabaseErrorReporter::ThrowDatabaseException), which makes the `return …_ERROR`
	//     line inside SQLite's DoRunQuery dead code.
	// Both surfaced together on the first apply against a clean in-memory database — first the
	// unguarded DROP threw, then, with that caught, every CREATE "failed" at zero rows.
	// Caught 2026-08-02 by tests/test_totalsNumericParity.cpp.
	//
	// ⭐⭐ AND IT IS NOT SWALLOWED HERE. It used to be, turned into `false`, and `false` became a status
	// nobody read — so a CREATE TRIGGER that could not compile ended like one that succeeded and the
	// apply walked on, saving the configuration over a schema it had failed to build.
	//
	// It travels as a QUERY-layer failure, not as a database one, and the distinction is the whole
	// point of the variety: the engine merely REPORTED the fault, it did not commit it. The trigger
	// body is text WE generated, and "Column unknown NEW.FLDnnnn_RTRef" says our declaration named a
	// column our own schema does not carry — a translation failure by any reading. Typed as a DB
	// fault it would inherit DB retry semantics (docs/exceptions.md §3), and retrying a mistranslation
	// is an infinite loop.
	//
	// The driver's message rides along verbatim, because it names the exact column and no summary of
	// ours could. A refusal is an exception (docs/exceptions.md §5a).
	// AND THE STATEMENT TRAVELS WITH IT. The driver names the fault ("expression evaluation not
	// supported") but not what it was evaluating, and the maintenance is the ONE place where the
	// failing text is OURS — a trigger body this level rendered a moment ago. Reported without it,
	// the message arrives with nothing to look at and nothing to grep: the maintenance is also the
	// only DDL that does not pass through ibExecuteDdl, so no log holds a copy either.
	for (const ibRenderedStatement& s : m_create) {
		try {
			conn.RunStatement(s.m_sql);
		}
		catch (const ibBackendException& err) {
			ibBackendQueryException::Throw(ibBackendQueryException::Kind::TranslationFailure,
			                               wxString::Format(wxT("%s\n\nstatement:\n%s"),
			                                                err.GetErrorDescription(), s.m_sql));
		}
	}
	return true;
}

// Do these two declarations produce the SAME bundle on this engine? Answered by rendering both and
// comparing the text — the same test ibApplyMaterialization uses to decide it has nothing to do, so
// there is one definition of "unchanged" rather than a second one written in terms of struct fields.
// A caller above uses it to decide what to TELL the user; it never decides what to run.
bool ibMaterializationEquivalent(ibDatabaseLayer& conn, const ibMaterializeSpec& a,
                                 const ibMaterializeSpec& b)
{
	const ibMaterializationDialect* mat = conn.GetMaterializationDialect();
	if (mat == nullptr)
		return true;   // nothing is maintained here, so nothing can differ
	return RenderMaterialization(a, mat, conn.GetDialect()).CreateText()
	    == RenderMaterialization(b, mat, conn.GetDialect()).CreateText();
}

bool ibCanMaterialize(const ibDatabaseLayer& conn)
{
	return conn.GetMaterializationDialect() != nullptr;
}

bool ibMaterializeSql::IsInstalled(ibDatabaseLayer& conn) const
{
	if (m_create.empty())
		return true;   // nothing is supposed to exist, so nothing can be missing
	for (const ibRenderedStatement& s : m_drop)
		if (!s.m_guard.IsEmpty() && !ExistsByProbe(conn, s.m_guard))
			return false;
	return true;
}

ibMaterializeApply ibApplyMaterialization(ibDatabaseLayer& conn, const ibMaterializeSpec& spec,
                                          const ibMaterializeSpec* previous)
{
	// Both dictionaries come off the connection HERE. A caller that had to fetch them first would
	// be a caller that knows what a dialect is — and the point of this level is that nothing above
	// it does.
	const ibMaterializationDialect* mat = conn.GetMaterializationDialect();
	const ibMaterializeSql sql = RenderMaterialization(spec, mat, conn.GetDialect());
	if (sql.IsEmpty())
		return ibMaterializeApply::Unchanged;   // nothing to maintain on this driver

	// UNCHANGED and INTACT — leave it alone. Dropping and recreating an identical bundle is not
	// merely wasted DDL: on an engine that invalidates dependent plans it costs every register on
	// every apply, and it makes the change log claim work that did not happen.
	if (previous != nullptr) {
		const ibMaterializeSql was = RenderMaterialization(*previous, mat, conn.GetDialect());
		if (was.CreateText() == sql.CreateText() && sql.IsInstalled(conn))
			return ibMaterializeApply::Unchanged;
	}

	// Apply RAISES when it cannot install the bundle — so reaching this line means it is installed,
	// and there is no failing value to answer with (docs/exceptions.md §5a).
	sql.Apply(conn);
	return ibMaterializeApply::Rebuilt;
}

bool ibDropMaterialization(ibDatabaseLayer& conn, const ibMaterializeSpec& spec)
{
	const ibMaterializationDialect* mat = conn.GetMaterializationDialect();
	if (mat == nullptr || spec.m_table.IsEmpty() || spec.m_source.IsEmpty()) {
		// ⚠ THREE DIFFERENT FACTS LEAVE BY THE SAME DOOR, AND THE ANSWER IS "nothing to drop" FOR ALL
		// THREE. A driver with no materialization has genuinely nothing here; a spec with no table or
		// no source is a spec that was never FILLED IN, and that is not the same statement — it reads
		// as "nothing was installed" while an object may well be standing over a table the differ is
		// about to remove ("cannot delete TABLE …: there are 1 dependencies"). Kept because a caller
		// that hands over an empty spec has nothing this function could act on either way; noted
		// because the emptiness is an assumption about the CALLER, not knowledge about the database.
		return true;   // nothing was ever installed here
	}

	ibMaterializeSql sql;
	RenderDrops(sql, spec, *mat);
	return sql.Apply(conn);   // drops only — Apply's guards keep a missing object from failing the transaction
}
