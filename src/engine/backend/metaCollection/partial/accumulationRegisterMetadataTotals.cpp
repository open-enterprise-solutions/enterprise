////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : accumulation register metadata - TOTALS: the three readings and their virtual tables
////////////////////////////////////////////////////////////////////////////
//
// ONE PLACE FOR THE TOTALS. Balance, Turnovers and BalanceAndTurnovers are one virtual table read
// three ways: each is a clean function ON THE METAOBJECT (its own aggregate knowledge) plus a light
// companion queryable that publishes it to L3. The manager holds no reading of its own - it calls
// these and dresses the rows for a script (accumulationRegisterManager_impl.cpp).
//

#include "accumulationRegister.h"
#include "accumulationRegisterManager.h"

// ⚠ THE ONLY L2 THIS FILE STILL NEEDS, and it is the READ side of the materialised surface
// (GetSourceRelation hands the door a derived table). The L2-1 expression IR is gone with the
// hand-built aggregates: a reading that reads a view has nothing to build.
#include "backend/databaseLayer/databaseMaterializeBuilder.h"  // L2-2 — RenderMaterializedRead
#include "backend/query/dataQueryBuilder.h"                     // L3 door — From(balance/turnover queryable).Select()
#include "backend/metaCollection/partial/registerQueryLowering.h"   // ibRegFieldsOf / ibRegValueField — the physical names a materialised READ still spells
#include "backend/appData.h"
#include "backend/session/session.h"

// ============================================================================
// Balance / turnover COMPUTE — on the register metaobject (its own aggregate-query
// knowledge, built as L2 IR). Each returns a RAM table the companion queryable hands
// to L3 via ComputeRows. Mirrors ibValueMetaObjectInformationRegister::ComputeSlice.
// ============================================================================

// ⭐⭐ ONE SHAPE FOR BOTH ROADS.
//
// A virtual table has two entrances — a QUERY that names it, and the runtime call through the
// manager — and they must hand over and hand back the same thing. The table these computes return is
// therefore seeded from the VIEW's shape: its column ids, its names, its types. Invented ids and a
// second spelling survive only while nobody crosses between the roads; the day a query reaches this
// one, a table that plainly reports `Resource1Turnover` answers that it has no such column.
//
// `unitWord` empty = the interval is read WHOLE and the rows carry no date, so no period column at
// all; a unit = `Period`, rolled to it. That is `ibRegisterViewColumnFits` — the same rule the source
// explorer answers with, so what the catalogue offers and what the read returns cannot drift.
static void SeedRamTableFromView(ibQueryRamTable& table, const ibBackendQueryable* view,
                                 const ibValueMetaObjectAccumulationRegister* reg, const ibRegFold& fold)
{
	if (view == nullptr)
		return;

	const ibValueMetaObjectAttributeBase* period = reg != nullptr ? reg->GetRegisterPeriod() : nullptr;
	const wxString periodName = period != nullptr ? period->GetName() : wxString();

	// The movement's own identity travels with the same rule the field tree uses — a row that covers
	// a whole interval was written by no one document, so it carries neither.
	const bool subordinate = reg != nullptr && reg->HasRecorder();
	const wxString recorderName = subordinate && reg->GetRegisterRecorder()   != nullptr ? reg->GetRegisterRecorder()->GetName()   : wxString();
	const wxString lineName     = subordinate && reg->GetRegisterLineNumber() != nullptr ? reg->GetRegisterLineNumber()->GetName() : wxString();
	for (const ibBackendQueryColumn* col : view->GetColumns()) {
		if (col == nullptr)
			continue;
		if (!periodName.IsEmpty() && !ibRegisterViewColumnFits(col->GetName(), periodName, fold, recorderName, lineName))
			continue;
		table.AddColumn(col->GetColumnId(), col->GetName(), col->GetTypeDesc());
	}
}

// ⭐⭐ THE VIEW HAS TWO ARMS, SO EVERY READER MUST SAY WHICH ONE IT WANTS.
//
// The turnovers view carries the stored rows AND the movements that came after them (the sub-grain
// tail). That is what lets a balance stop at noon or at a document -- and it is also why a reader
// that says NOTHING would get every movement of the current grain twice: once rolled into the
// total, once as itself. There is no safe default here: silence is the wrong answer, not the
// neutral one, and it is wrong in the direction that looks plausible.
//
// These readings fold whole intervals at the stored grain, so they take the stored arm. Sub-grain
// precision belongs to the boundary reads (FillArmCut), which is where the floor lives.
static void RestrictToStoredArm(ibDataQueryBuilder& b,
                                const ibValueMetaObjectAccumulationRegister* reg,
                                const ibBackendQueryable* view)
{
	if (reg == nullptr || view == nullptr || !reg->HasRecorder() || reg->GetRegisterRecorder() == nullptr)
		return;

	const ibBackendQueryColumn* mark = view->ResolveColumnByName(reg->GetRegisterRecorder()->GetName());
	if (mark != nullptr)
		b.Where(ibQueryPredicate::Null(mark, /*negated*/ false));
}

// ⛔ THE NUMERIC PIN IS GONE TOO, and its absence is the point: it existed to stop a hand-built
// CAST from narrowing money (a bare NUMERIC is NUMERIC(9,0) on Firebird). Nothing here casts now -
// the view stores each figure in the resource's own declared type, and reading it keeps that type.

// ⭐ THE STORAGE NAME IS ASKED FOR, NEVER SPELLED. A view column carries both — `Resource1Turnover`
// for a query, `Resource1_Turnover` for the table — and the pair is built in ONE place
// (accumulationRegisterSchema). Typing the storage spelling out again here would be a second writing
// of one name, and this one drifts SILENTLY: a read spec naming a column the view does not have
// returns NULLs, not an error.
static wxString ibRegPhysicalOf(const ibBackendQueryable* view, const wxString& logical)
{
	const ibBackendQueryColumn* col = view != nullptr ? view->ResolveColumnByName(logical) : nullptr;
	return col != nullptr ? col->GetPhysicalName() : wxString();
}

// The id the seeding gave a column. The read stores BY ID, and the id belongs to the view — asking
// the table for it is what keeps this file from knowing how the view numbers its columns.
static ibMetaID RamColumnIdByName(const ibQueryRamTable& table, const wxString& name)
{
	for (const ibQueryRamColumn& col : table.Columns())
		if (col.m_name == name)
			return col.m_id;
	return 0;
}

// ⭐⭐ A BALANCE IS THE TURNOVERS, FOLDED UP TO A MOMENT — and the turnovers are already stored.
//
// This used to recompute them from the movements: a signed CASE per resource, a HAVING, GROUP BY over
// physical fields. None of that belonged here. GENERATING the totals is the materialisation's phase
// (L2-2 renders the bundle, L3-4 maintains it through the trigger); everything at this level READS.
ibQueryRamTable ibValueMetaObjectAccumulationRegister::ComputeBalance(const ibValue& cPeriod, const ibQueryPredicatePtr& cFilter) const
{
	ibQueryRamTable retTable;
	if (GetRegisterType() != ibRegisterType::eBalances)
		return retTable;   // a turnover-only register carries no balances at all

	// READ the turnovers surface, PUBLISH the balance shape: the two views differ, and that difference
	// is the whole of what a balance is.
	const ibBackendQueryable* source = GetViewQueryable(GetTurnoverViewName(), ibViewShape::Turnovers);
	SeedRamTableFromView(retTable, GetViewQueryable(GetBalanceViewName(), ibViewShape::Balance),
	                     this, ibRegFold());
	if (source == nullptr)
		return retTable;

	const wxString periodName = GetRegisterPeriod() != nullptr ? GetRegisterPeriod()->GetName() : wxString();
	const ibBackendQueryColumn* periodCol = source->ResolveColumnByName(periodName);

	ibDataQueryBuilder b;
	b.From(source);
	b.WithAccessPolicy(nullptr);   // a total filtered by the caller's rights is a wrong total
	RestrictToStoredArm(b, this, source);

	if (periodCol != nullptr && !cPeriod.IsEmpty())
		b.WhereCompare(periodCol, ibQueryFilterOp::LessEqual, cPeriod);

	std::vector<std::pair<const ibBackendQueryColumn*, ibValue>> leaves;
	ibRegFlatLeaves(cFilter, leaves);
	for (const auto& leaf : leaves)
		if (const ibBackendQueryColumn* onView = leaf.first != nullptr
				? source->ResolveColumnByName(leaf.first->GetName()) : nullptr)
			b.Where(onView, leaf.second);

	std::vector<const ibBackendQueryColumn*> keyCols;
	for (const auto dimension : GetDimensionArrayObject())
		if (const ibBackendQueryColumn* onView = dimension != nullptr
				? source->ResolveColumnByName(dimension->GetName()) : nullptr) {
			keyCols.push_back(onView);
			b.GroupBy(onView);
		}

	// `<Resource>Turnover` summed up to the moment IS `<Resource>Balance` — read under the turnover
	// name, published under the balance one.
	std::vector<std::pair<wxString, wxString>> figures;   // read-as, publish-as
	for (const auto res : GetResourceArrayObject())
		if (res != nullptr)
			figures.push_back({ res->GetName() + ibRegFigure::Turnover, res->GetName() + ibRegFigure::Balance });
	for (const auto& figure : figures)
		if (const ibBackendQueryColumn* col = source->ResolveColumnByName(figure.first))
			b.Aggregate(ibDataQueryBuilder::AggregateFn::Sum, col, figure.first);

	try {
		ibDataQueryResult sel = b.SelectAggregate();
		while (sel.Next()) {
			// No stock means NO ROW — the same answer the materialised read gives (m_dropZeroRows).
			std::vector<ibValue> values;
			bool anyNonZero = false;
			for (const auto& figure : figures) {
				ibValue v = sel.GetColumn(figure.first);
				if (!(v.GetNumber() == ibNumber())) anyNonZero = true;
				values.push_back(v);
			}
			if (!figures.empty() && !anyNonZero)
				continue;

			const long row = retTable.AppendRow();
			for (const ibBackendQueryColumn* key : keyCols)
				retTable.SetCell(row, key->GetColumnId(), sel.GetValue(key));
			for (size_t i = 0; i < figures.size(); i++)
				retTable.SetByName(row, figures[i].second, values[i]);
		}
	}
	catch (...) {}

	return retTable;
}

// ============================================================================
// Companion queryables — the LIVE fallback.
//
// ComputeRows runs only when there is no materialised surface (IsComputedInRam() is then true —
// on a driver that cannot maintain derived state, i.e. ODBC). With views present the source is a
// derived table instead and these are never called; see GetSourceRelation below.
//
// The fallback is not legacy. It is the only path on such a driver, and it is the ORACLE a parity
// test measures the materialised path against: two roads to the same numbers, and a disagreement
// is a bug in the new one.
// ============================================================================

ibQueryRamTable ibBalanceQueryable::ComputeRows(const std::vector<ibQueryCondition>& /*extra*/) const
{
	return m_reg->ComputeBalance(m_period, m_filter);
}

ibQueryRamTable ibTurnoverQueryable::ComputeRows(const std::vector<ibQueryCondition>& /*extra*/) const
{
	return m_reg->ComputeTurnover(m_begin, m_end, m_filter, m_fold);
}

ibQueryRamTable ibBalanceAndTurnoverQueryable::ComputeRows(const std::vector<ibQueryCondition>& /*extra*/) const
{
	return m_reg->ComputeBalanceAndTurnover(m_begin, m_end, m_fold, m_filter);
}
// ⭐⭐ THE READING IS A READING. Nothing is recomputed here.
//
// This used to build the whole aggregate by hand in L2 IR — signed sums over the movements, a CASE
// per resource, a HAVING, GROUP BY over physical field names. All of it recomputed what the
// materialisation had already computed and stored.
//
// The algorithm belongs to the TRIGGER, which maintains the surface and regenerates it. The view is
// that surface's shop window, and a view is an ordinary named relation — so this asks the door for
// it exactly as any query would, and the only thing left here is WHICH rows and HOW they fold.
ibQueryRamTable ibValueMetaObjectAccumulationRegister::ComputeTurnover(const ibValue& cBegin, const ibValue& cEnd, const ibQueryPredicatePtr& cFilter,
                                                                       const ibRegFold& cFold) const
{
	const ibBackendQueryable* view = GetViewQueryable(GetTurnoverViewName(), ibViewShape::Turnovers);
	if (view == nullptr)
		return ibQueryRamTable();   // no surface, no totals — the trigger is where they come from

	// The table this returns IS the view's shape (ids, names, types), filtered by which period
	// columns this reading actually carries.
	ibQueryRamTable retTable;
	SeedRamTableFromView(retTable, view, this, cFold);

	const wxString periodName = GetRegisterPeriod() != nullptr ? GetRegisterPeriod()->GetName() : wxString();
	const ibBackendQueryColumn* periodCol = view->ResolveColumnByName(periodName);

	ibDataQueryBuilder b;
	b.From(view);
	RestrictToStoredArm(b, this, view);
	// ⚠ NOT FILTERED BY THE CALLER'S RIGHTS. Totals read through a right-limited view would report
	// figures computed from the rows that caller happens to see, and a wrong total does not look
	// like an error.
	b.WithAccessPolicy(nullptr);

	if (periodCol != nullptr) {
		if (!cBegin.IsEmpty()) b.WhereCompare(periodCol, ibQueryFilterOp::GreaterEqual, cBegin);
		if (!cEnd.IsEmpty())   b.WhereCompare(periodCol, ibQueryFilterOp::LessEqual,    cEnd);
	}

	// The condition names DIMENSIONS; this read stands on the view, so each leaf is re-pointed at the
	// view's own column of that name. The filter is written once and applies to whichever surface a
	// reading happens to stand on.
	std::vector<std::pair<const ibBackendQueryColumn*, ibValue>> leaves;
	ibRegFlatLeaves(cFilter, leaves);
	for (const auto& leaf : leaves) {
		const ibBackendQueryColumn* onView = leaf.first != nullptr
			? view->ResolveColumnByName(leaf.first->GetName()) : nullptr;
		if (onView != nullptr)
			b.Where(onView, leaf.second);
	}

	// GROUP BY the keys — and by the truncated period when a granularity was asked for, which is the
	// whole of "the periodicity decides the fold".
	std::vector<const ibBackendQueryColumn*> keyCols;
	for (const auto dimension : GetDimensionArrayObject()) {
		const ibBackendQueryColumn* onView = dimension != nullptr
			? view->ResolveColumnByName(dimension->GetName()) : nullptr;
		if (onView != nullptr) { keyCols.push_back(onView); b.GroupBy(onView); }
	}
	// ⭐ A CALENDAR FOLD TRUNCATES; THE REGISTER'S OWN PERIOD DOES NOT. `Period` groups by the column
	// as it stands -- which is a different reading from "the interval whole", and used to be the same
	// one because a bool could not hold the difference.
	if (periodCol != nullptr) {
		if (cFold.IsCalendar())
			b.GroupByExpr(ibQueryColumnExpr::PeriodTrunc(ibQueryColumnExpr::Col(periodCol), cFold.m_unit), periodName);
		else if (cFold.m_kind == ibRegGranularity::Period)
			b.GroupBy(periodCol);
	}

	// SUM every figure the view reports. Which ones exist is the view's business — a turnover-only
	// register simply has no receipt or expense column, and nothing here needs to know that rule.
	const bool withSign = (GetRegisterType() == ibRegisterType::eBalances);
	std::vector<wxString> figures;
	for (const auto res : GetResourceArrayObject()) {
		if (res == nullptr)
			continue;
		figures.push_back(res->GetName() + ibRegFigure::Turnover);
		if (withSign) {
			figures.push_back(res->GetName() + ibRegFigure::Receipt);
			figures.push_back(res->GetName() + ibRegFigure::Expense);
		}
	}
	for (const wxString& figure : figures)
		if (const ibBackendQueryColumn* col = view->ResolveColumnByName(figure))
			b.Aggregate(ibDataQueryBuilder::AggregateFn::Sum, col, figure);

	try {
		ibDataQueryResult sel = b.SelectAggregate();
		while (sel.Next()) {
			// A key whose every figure folded to nothing is not a row. The old read said so with a
			// HAVING; the door's HAVING takes ordered comparisons only, so it is said here — same
			// rows out, one clause fewer in the SQL.
			std::vector<ibValue> values;
			bool anyNonZero = false;
			for (const wxString& figure : figures) {
				ibValue v = sel.GetColumn(figure);
				if (!(v.GetNumber() == ibNumber())) anyNonZero = true;
				values.push_back(v);
			}
			if (!figures.empty() && !anyNonZero)
				continue;

			const long row = retTable.AppendRow();
			for (const ibBackendQueryColumn* key : keyCols)
				retTable.SetCell(row, key->GetColumnId(), sel.GetValue(key));
			if (cFold.HasPeriod())
				retTable.SetByName(row, periodName, sel.GetColumn(periodName));
			for (size_t i = 0; i < figures.size(); i++)
				retTable.SetByName(row, figures[i], values[i]);
		}
	}
	catch (...) {}

	return retTable;
}
// ============================================================================
// Balance AND turnover, per period — TWO server-side aggregates plus a shared in-memory fold:
//
//   1. the balance each key carried INTO the interval (everything strictly before `begin`).
//      Skipping it would report correct turnovers on top of balances that all start at zero —
//      the failure that reads like a rounding bug and is not one;
//   2. turnovers inside the interval, GROUPed BY the period TRUNCATED to the requested unit. The
//      unit is a QUERY parameter (the read granularity), and the truncation goes through the
//      dialect map, so the grouping means the same thing on every engine;
//   3. FoldBalancesForward turns (1) + (2) into per-period opening / closing.
//
// Step 3 deliberately lives in query/queryRamTable rather than here: it is identical for an
// accounting register, which signs its movements by debit / credit side instead of by record type
// — by the time rows reach the fold, both arrive as a receipt / expense pair per period.
// ============================================================================
ibQueryRamTable ibValueMetaObjectAccumulationRegister::ComputeBalanceAndTurnover(
	const ibValue& cBegin, const ibValue& cEnd, const ibRegFold& cFold, const ibQueryPredicatePtr& cFilter) const
{
	const bool withSign = (GetRegisterType() == ibRegisterType::eBalances);

	// READ the turnovers surface twice, PUBLISH the balance-and-turnovers shape. Both reads are
	// ordinary reads of an ordinary relation; the only thing this function still computes itself is
	// the forward roll, which is sequential by nature and cannot be a query.
	const ibBackendQueryable* source = GetViewQueryable(GetTurnoverViewName(), ibViewShape::Turnovers);

	ibQueryRamTable retTable;
	SeedRamTableFromView(retTable,
	                     GetViewQueryable(GetBalanceAndTurnoverViewName(), ibViewShape::BalanceAndTurnovers),
	                     this, cFold);
	if (source == nullptr)
		return retTable;

	// The period column is named after the register's OWN period attribute — the view names it that
	// way, so every reader must ask rather than assume the word.
	const wxString periodName = GetRegisterPeriod() != nullptr ? GetRegisterPeriod()->GetName() : wxString();
	const ibMetaID periodId   = RamColumnIdByName(retTable, periodName);

	std::vector<ibBalanceFoldSlot> slots;
	std::vector<ibValueMetaObjectAttributeBase*> resources;
	for (const auto object : GetResourceArrayObject()) {
		ibBalanceFoldSlot s;
		s.m_opening  = RamColumnIdByName(retTable, object->GetName() + ibRegFigure::OpeningBalance);
		s.m_receipt  = RamColumnIdByName(retTable, object->GetName() + ibRegFigure::Receipt);
		s.m_expense  = RamColumnIdByName(retTable, object->GetName() + ibRegFigure::Expense);
		s.m_turnover = RamColumnIdByName(retTable, object->GetName() + ibRegFigure::Turnover);
		s.m_closing  = RamColumnIdByName(retTable, object->GetName() + ibRegFigure::ClosingBalance);
		slots.push_back(s);
		resources.push_back(object);
	}

	const ibBackendQueryColumn* periodCol = source->ResolveColumnByName(periodName);

	std::vector<const ibBackendQueryColumn*> keyOnView;
	for (const auto dimension : GetDimensionArrayObject())
		if (const ibBackendQueryColumn* onView = dimension != nullptr
				? source->ResolveColumnByName(dimension->GetName()) : nullptr)
			keyOnView.push_back(onView);

	std::vector<std::pair<const ibBackendQueryColumn*, ibValue>> leaves;
	ibRegFlatLeaves(cFilter, leaves);

	// One shape for both reads: the surface, the rights opt-out, the keys, the condition.
	auto openRead = [&](ibDataQueryBuilder& b) {
		b.From(source);
		b.WithAccessPolicy(nullptr);   // a total filtered by the caller's rights is a wrong total
		RestrictToStoredArm(b, this, source);
		for (const auto& leaf : leaves)
			if (const ibBackendQueryColumn* onView = leaf.first != nullptr
					? source->ResolveColumnByName(leaf.first->GetName()) : nullptr)
				b.Where(onView, leaf.second);
		for (const ibBackendQueryColumn* key : keyOnView)
			b.GroupBy(key);
	};

	// The identity a set of dimension values folds to. Built the SAME way for the opening rows and
	// for the output rows — if the two differed, every opening balance would silently miss its key
	// and the report would look like a fresh start.
	auto keyOfValues = [](const std::vector<ibValue>& values) {
		wxString k;
		for (const ibValue& v : values)
			k += v.GetHashKey() + wxT("\x1f");
		return k;
	};

	// --- 1. OPENING — the turnovers strictly before the interval, per key ----------------------
	std::map<wxString, std::map<ibMetaID, ibNumber>> opening;
	if (withSign) {
		ibDataQueryBuilder b;
		openRead(b);
		if (periodCol != nullptr && !cBegin.IsEmpty())
			b.WhereCompare(periodCol, ibQueryFilterOp::Less, cBegin);
		for (const auto res : resources)
			if (const ibBackendQueryColumn* col = source->ResolveColumnByName(res->GetName() + ibRegFigure::Turnover))
				b.Aggregate(ibDataQueryBuilder::AggregateFn::Sum, col, res->GetName() + ibRegFigure::Turnover);

		try {
			ibDataQueryResult sel = b.SelectAggregate();
			while (sel.Next()) {
				std::vector<ibValue> keyValues;
				for (const ibBackendQueryColumn* key : keyOnView)
					keyValues.push_back(sel.GetValue(key));
				std::map<ibMetaID, ibNumber>& row = opening[keyOfValues(keyValues)];
				for (size_t i = 0; i < resources.size(); i++)
					row[slots[i].m_opening] = sel.GetColumn(resources[i]->GetName() + ibRegFigure::Turnover).GetNumber();
			}
		}
		catch (...) {}
	}

	// --- 2. RECEIPT / EXPENSE inside the interval, per key and truncated period -----------------
	{
		ibDataQueryBuilder b;
		openRead(b);
		if (periodCol != nullptr) {
			if (!cBegin.IsEmpty()) b.WhereCompare(periodCol, ibQueryFilterOp::GreaterEqual, cBegin);
			if (!cEnd.IsEmpty())   b.WhereCompare(periodCol, ibQueryFilterOp::LessEqual,    cEnd);
			if (cFold.IsCalendar())
				b.GroupByExpr(ibQueryColumnExpr::PeriodTrunc(ibQueryColumnExpr::Col(periodCol), cFold.m_unit), periodName);
			else if (cFold.m_kind == ibRegGranularity::Period)
				b.GroupBy(periodCol);
		}
		for (const auto res : resources) {
			if (const ibBackendQueryColumn* col = source->ResolveColumnByName(res->GetName() + ibRegFigure::Receipt))
				b.Aggregate(ibDataQueryBuilder::AggregateFn::Sum, col, res->GetName() + ibRegFigure::Receipt);
			if (withSign)
				if (const ibBackendQueryColumn* col = source->ResolveColumnByName(res->GetName() + ibRegFigure::Expense))
					b.Aggregate(ibDataQueryBuilder::AggregateFn::Sum, col, res->GetName() + ibRegFigure::Expense);
		}

		try {
			ibDataQueryResult sel = b.SelectAggregate();
			while (sel.Next()) {
				const long row = retTable.AppendRow();
				size_t k = 0;
				for (const auto dimension : GetDimensionArrayObject()) {
					if (dimension == nullptr || k >= keyOnView.size())
						continue;
					retTable.SetCell(row, dimension->GetMetaID(), sel.GetValue(keyOnView[k++]));
				}
				if (periodCol != nullptr)
					retTable.SetCell(row, periodId, sel.GetColumn(periodName));
				for (size_t i = 0; i < resources.size(); i++) {
					retTable.SetCell(row, slots[i].m_receipt, sel.GetColumn(resources[i]->GetName() + ibRegFigure::Receipt));
					if (withSign)
						retTable.SetCell(row, slots[i].m_expense, sel.GetColumn(resources[i]->GetName() + ibRegFigure::Expense));
				}
			}
		}
		catch (...) {}
	}

	// --- 3. roll the openings forward through the periods --------------------------------------
	std::vector<ibMetaID> keyCols;
	for (const auto dimension : GetDimensionArrayObject())
		keyCols.push_back(dimension->GetMetaID());
	FoldBalancesForward(retTable, keyCols, periodId, slots, opening);

	return retTable;
}

// ============================================================================
// The MATERIALISED path — each virtual table as a DERIVED TABLE over its view.
//
// GetSourceRelation is what puts the reading on the server. The parameters (the as-of date, the
// interval, the dimension filter) are baked into the SUBQUERY, so the selection happens inside it,
// before the outer query engine sees a row. A join to a catalog is then an ordinary SQL join and
// only matching rows ever leave the database — which is the whole point, since reading balances is
// the most latency-critical operation the platform performs.
//
// When there is no materialised surface these are never called: IsComputedInRam() stays true and
// the live aggregation answers instead.
// ============================================================================

// ⛔ TWO HELPERS STOOD HERE AND NOTHING CALLED THEM ANY MORE. They belonged to the hand-built L2
// road: one lowered the condition into a subquery's WHERE, the other projected a view's physical
// columns through. Both died with the demolition - the readings now hand the door a condition and
// let it read the view, which is what a view is for.

// ONE answer for all three readings — the flag it asks is the virtual one, so each still decides its
// own road while the rule about which provider serves which road is written down once.
ibBackendQueryProvider& ibAccumulationTotalsQueryable::GetProvider() const
{
	// Materialised => the ordinary PHYSICAL provider, so the source behaves like any relation.
	return IsComputedInRam() ? ibComputedRegisterQueryable::GetProvider() : ibBackendQueryable::GetProvider();
}

// ⭐⭐ A COMPANION NAVIGATES THROUGH ITS OWN SHAPE, ON BOTH ROADS.
//
// These used to swing with IsComputedInRam(): the VIEW when the surface existed, the REGISTER when
// it did not. So the columns a query could name changed with the road it happened to take — on the
// live road it was told this table has `Quantity` and `Period` (the MOVEMENTS) and no
// `Resource1Turnover` at all, which is the one answer that is never true of a virtual table.
//
// The shape is metadata, not data: GetViewQueryable builds it from the register's own dimensions and
// resources, touching no database — the source explorer has always answered from it. So it is the
// right answer on every road, and the RAM tables the computes return are now seeded from the very
// same shape (SeedRamTableFromView), which is what makes the rows findable by the columns.
// THE SHAPE EACH READING PUBLISHES, answered from the one thing that distinguishes them. A balance
// FOLDS the turnovers surface, so its output is the dimensions plus one balance per resource — the
// BALANCE view's set, not the turnover view's. That is why the shape is declared by the reading and
// the read SOURCE stays each one's own business (GetSourceRelation).
const ibBackendQueryable* ibAccumulationTotalsQueryable::NavigationSource() const
{
	switch (m_shape) {
	case ibViewShape::Turnovers:
		return m_reg->GetViewQueryable(m_reg->GetTurnoverViewName(), m_shape);
	case ibViewShape::BalanceAndTurnovers:
		return m_reg->GetViewQueryable(m_reg->GetBalanceAndTurnoverViewName(), m_shape);
	default:
		return m_reg->GetViewQueryable(m_reg->GetBalanceViewName(), ibViewShape::Balance);
	}
}


// ============================================================================
// The three virtual tables as DERIVED TABLES over the materialised surface.
//
// Each one DESCRIBES the reading — which figures, over what range, filtered how — and L2-2 spells
// the SQL. Nothing here builds an expression: that is the point of the level existing.
//
// The parameters land INSIDE the subquery, so the selection happens on the server before the outer
// query sees a row; a join to a catalog is then ordinary SQL. When there is no materialised surface
// these are not called at all — IsComputedInRam() stays true and the live aggregation answers.
// ============================================================================

namespace {

// The dimension filter, resolved to (physical column, value) pairs L2-2 applies inside the read.
std::vector<std::pair<wxString, ibValue>> ReadFilters(
	const ibValueMetaObjectAccumulationRegister* /*reg*/, const ibQueryPredicatePtr& filter)
{
	std::vector<std::pair<wxString, ibValue>> out;
	std::vector<std::pair<const ibBackendQueryColumn*, ibValue>> leaves;
	ibRegFlatLeaves(filter, leaves);
	for (const auto& leaf : leaves)
		out.push_back({ leaf.first->GetPhysicalName(), leaf.second });
	return out;
}

// Every dimension's physical fields — the key of any read over the surface.
std::vector<wxString> ReadKeys(const ibValueMetaObjectAccumulationRegister* reg)
{
	std::vector<wxString> out;
	for (const auto dimension : reg->GetDimensionArrayObject())
		for (const wxString& f : ibRegFieldsOf(dimension))
			out.push_back(f);
	return out;
}

} // namespace

// ⭐⭐ WHERE THIS READ CUTS BETWEEN THE TOTALS AND THE MOVEMENTS.
//
// The turnovers view carries both: stored rows up to the grain it keeps (a day), and the movements
// themselves for everything after. Which half answers depends entirely on WHERE the question stops:
//
//   at the grain or above it   the totals are complete -- the movement arm is excluded outright, and
//                              a reading of a month costs exactly what it costs today;
//   inside the grain           the totals are complete up to the grain's START, and the rest of the
//                              answer is this grain's movements up to the boundary;
//   at a DOCUMENT              the same, plus the tail that separates documents sharing one instant.
//
// The floor is what keeps a movement from being counted twice -- once by the trigger that rolled it
// into today's total, once by itself. It is computed through `ibTruncateToPeriod`, the RAM twin of
// the truncation the trigger renders, because a floor that disagreed with the stored key by one
// second would drop or duplicate a whole grain.
// The recorder's fields carrying a boundary's document, decomposed by the WRITE codec: the same
// decomposition the rows were stored through, so the comparison rides exactly the fields the index
// is built on. The discriminator is dropped -- it says what KIND of value this is, which is the same
// for every recorder and would only make the tuple one constant longer.
static std::vector<std::pair<wxString, ibQueryExprPtr>> RecorderTuple(
	const ibValueMetaObjectAccumulationRegister* reg,
	const std::vector<ibColumnSlot>& slots, const ibValue& recorder)
{
	std::vector<std::pair<wxString, ibQueryExprPtr>> tuple;

	std::vector<wxString> fields;
	for (const ibColumnSlot& slot : slots)
		fields.push_back(slot.m_name);

	ibQueryStatement capture(ibQueryStatement::Kind::Delete, wxString(), fields);
	int pos = 1;
	ibColumnCodec::WriteValue(reg->GetRegisterRecorder(), reg->GetMetaData(), recorder, &capture, pos);
	const std::vector<ibQueryExprPtr>& consts = capture.CapturedValues();

	for (size_t i = 0; i < slots.size() && i < consts.size(); i++) {
		if (slots[i].m_role == ibColumnRole::Discriminator || !consts[i])
			continue;
		tuple.push_back({ slots[i].m_name, consts[i] });
	}
	return tuple;
}

static void FillArmCut(ibMaterializeReadSpec& read,
                       const ibValueMetaObjectAccumulationRegister* reg,
                       const ibRegBound& upper,
                       const ibRegBound& lower = ibRegBound())
{
	if (reg == nullptr || !reg->HasRecorder() || reg->GetRegisterRecorder() == nullptr)
		return;   // this view has one arm; there is nothing to cut

	const std::vector<ibColumnSlot> slots = DescribeColumnLayout(reg->GetRegisterRecorder());
	if (slots.empty())
		return;

	// A stored row has no recorder, so any of its fields is NULL there -- the mark is the row's own
	// answer to "which arm am I", with no flag column invented for it.
	read.m_markColumn = slots.front().m_name;

	const ibTotalsPeriod grain = reg->GetTotalsPeriodUnit();

	// ⭐ A BOUNDARY REACHES BELOW THE GRAIN when it falls inside one, or when it names a document.
	// Anything else -- a date on a grain edge, no date at all -- is answered by the stored rows
	// whole, and the movement arm is left excluded. The trigger keeps the CURRENT grain's row up to
	// date, so "no boundary" is not stale: it is complete at grain resolution.
	const auto reachesInside = [&](const ibRegBound& b, wxDateTime& moment) {
		if (b.m_date.GetType() != TYPE_DATE)
			return false;
		moment = b.m_date.GetDateTime();
		return b.HasRecorder() || ibTruncateToPeriod(moment, grain) != moment;
	};

	wxDateTime upperMoment, lowerMoment;
	const bool cutUpper = reachesInside(upper, upperMoment);
	const bool cutLower = reachesInside(lower, lowerMoment);
	if (!cutUpper && !cutLower)
		return;

	// The stored arm ends where the upper boundary's grain begins. With no upper boundary at all it
	// still ends somewhere -- at the LOWER boundary's grain -- because the only reason the arm is
	// cut at all is that one end of the interval is partial.
	read.m_toExcluding   = upper.m_excluding;
	read.m_fromExcluding = lower.m_excluding;
	read.m_floor = ibValue(ibTruncateToPeriod(cutUpper ? upperMoment : lowerMoment, grain));
	if (cutUpper && upper.HasRecorder())
		read.m_boundaryTail = RecorderTuple(reg, slots, upper.m_recorder);

	// The stored arm begins at the first WHOLE grain at or after the lower boundary: the grain the
	// boundary falls INTO holds movements before it as well, so it cannot be taken as a row.
	if (cutLower) {
		read.m_headSplit = ibValue(ibNextPeriodStart(lowerMoment, grain));
		if (lower.HasRecorder())
			read.m_boundaryHead = RecorderTuple(reg, slots, lower.m_recorder);
	}
}

ibQueryRelPtr ibBalanceQueryable::GetSourceRelation(const wxString& alias) const
{
	if (IsComputedInRam())
		return nullptr;

	// A balance AS OF A DATE is the turnovers folded up to that moment. The date cannot live in a
	// view, so it lives in this read — which is exactly why the surface is queried rather than
	// stored per date.
	// ⭐ THE BOUNDARY MAY NAME A DOCUMENT, not just a date. Three documents can carry one date, and
	// "the balance as of THIS one" is the question accounting actually asks; a date alone answers
	// about a moment nobody named.
	const ibRegBound bound = ibReadRegisterBound(m_period);

	ibMaterializeReadSpec r;
	r.m_view          = m_reg->GetTurnoverViewName();
	r.m_periodColumn  = ibRegValueField(m_reg->GetRegisterPeriod());
	r.m_to            = bound.m_date;
	r.m_keyColumns    = ReadKeys(m_reg);
	r.m_filters       = ReadFilters(m_reg, m_filter);
	r.m_dropZeroRows  = true;   // no stock means NO ROW, matching the live path
	FillArmCut(r, m_reg, bound);

	// Names asked for, not spelled: the balance view knows what it publishes, the turnovers view knows
	// what it stores.
	const ibBackendQueryable* outView = m_reg->GetViewQueryable(m_reg->GetBalanceViewName(),
		ibValueMetaObjectAccumulationRegister::ibViewShape::Balance);
	const ibBackendQueryable* srcView = m_reg->GetViewQueryable(m_reg->GetTurnoverViewName(),
		ibValueMetaObjectAccumulationRegister::ibViewShape::Turnovers);

	for (const auto res : m_reg->GetResourceArrayObject())
		if (res != nullptr)
			r.m_columns.push_back({ ibRegPhysicalOf(outView, res->GetName() + ibRegFigure::Balance),
			                        ibRegPhysicalOf(srcView, res->GetName() + ibRegFigure::Turnover),
			                        wxString(), ibMaterializeAgg::Value, ibMaterializeWhen::UpToTo, true });

	return RenderMaterializedRead(r, alias);
}

ibQueryRelPtr ibTurnoverQueryable::GetSourceRelation(const wxString& alias) const
{
	if (IsComputedInRam())
		return nullptr;

	// Turnovers need no fold — the surface already holds them per period. The interval and the
	// filter ARE the read.
	ibMaterializeReadSpec r;
	r.m_view         = m_reg->GetTurnoverViewName();
	r.m_periodColumn = ibRegValueField(m_reg->GetRegisterPeriod());
	r.m_from         = ibReadRegisterBound(m_begin).m_date;
	r.m_to           = ibReadRegisterBound(m_end).m_date;
	r.m_keyColumns   = ReadKeys(m_reg);
	r.m_filters      = ReadFilters(m_reg, m_filter);

	// ⭐ EITHER END MAY REACH BELOW THE GRAIN — "turnovers between this document and that one" is
	// the question, and both ends of it name a moment. Ask for whole days and the stored rows answer
	// alone; ask for noon-to-noon and only the two partial ends come from the movements.
	FillArmCut(r, m_reg, ibReadRegisterBound(m_end), ibReadRegisterBound(m_begin));

	// Straight through: what the view publishes IS what this reading reports, so both sides of every
	// pair are the same column — and neither side is typed out.
	const ibBackendQueryable* view = m_reg->GetViewQueryable(m_reg->GetTurnoverViewName(),
		ibValueMetaObjectAccumulationRegister::ibViewShape::Turnovers);

	const bool withSign = (m_reg->GetRegisterType() == ibRegisterType::eBalances);
	auto passThrough = [&](const wxString& logical) {
		const wxString field = ibRegPhysicalOf(view, logical);
		if (!field.IsEmpty())
			r.m_columns.push_back({ field, field, wxString(), ibMaterializeAgg::Value, ibMaterializeWhen::Always, false });
	};
	for (const auto res : m_reg->GetResourceArrayObject()) {
		if (res == nullptr)
			continue;
		const wxString base = res->GetName();
		passThrough(base + ibRegFigure::Receipt);
		if (withSign)
			passThrough(base + ibRegFigure::Expense);
		passThrough(base + ibRegFigure::Turnover);
	}

	return RenderMaterializedRead(r, alias);
}

ibQueryRelPtr ibBalanceAndTurnoverQueryable::GetSourceRelation(const wxString& alias) const
{
	if (IsComputedInRam())
		return nullptr;

	// The symbiosis, in ONE pass: what was carried in, what moved, what remains. Three different
	// conditions over the same scan — which is why no join and no window is needed, and why a
	// register with an opening balance but no movements in the interval still reports.
	ibMaterializeReadSpec r;
	r.m_view         = m_reg->GetTurnoverViewName();
	r.m_periodColumn = ibRegValueField(m_reg->GetRegisterPeriod());
	r.m_from         = ibReadRegisterBound(m_begin).m_date;
	r.m_to           = ibReadRegisterBound(m_end).m_date;
	r.m_keyColumns   = ReadKeys(m_reg);
	r.m_filters      = ReadFilters(m_reg, m_filter);

	// ⭐ EITHER END MAY REACH BELOW THE GRAIN — "turnovers between this document and that one" is
	// the question, and both ends of it name a moment. Ask for whole days and the stored rows answer
	// alone; ask for noon-to-noon and only the two partial ends come from the movements.
	FillArmCut(r, m_reg, ibReadRegisterBound(m_end), ibReadRegisterBound(m_begin));

	// ⭐ THE PHYSICAL NAMES ARE ASKED FOR, NOT SPELLED. Every one of these used to be written by hand
	// as `<Resource>` + `"_OpeningBalance"` — the storage spelling, typed out a fourth time, in a file
	// that already knows the figure suffixes and already holds a view that carries both names. Two
	// writings of one name is how they drift; here it would drift silently, because a read spec naming
	// a column the view does not have returns NULLs rather than an error.
	//
	// So each figure is looked up ON THE VIEW and hands over its own physical name. The logical side
	// is `ibRegFigure`; the physical side is the column's business, and nothing here spells either.
	const ibBackendQueryable* out = m_reg->GetViewQueryable(m_reg->GetBalanceAndTurnoverViewName(),
		ibValueMetaObjectAccumulationRegister::ibViewShape::BalanceAndTurnovers);
	const ibBackendQueryable* src = m_reg->GetViewQueryable(m_reg->GetTurnoverViewName(),
		ibValueMetaObjectAccumulationRegister::ibViewShape::Turnovers);
	if (out == nullptr || src == nullptr)
		return nullptr;

	const auto physical = [](const ibBackendQueryable* q, const wxString& logical) -> wxString {
		const ibBackendQueryColumn* col = q != nullptr ? q->ResolveColumnByName(logical) : nullptr;
		return col != nullptr ? col->GetPhysicalName() : wxString();
	};

	for (const auto res : m_reg->GetResourceArrayObject()) {
		if (res == nullptr)
			continue;
		const wxString base = res->GetName();
		const wxString turn = ibRegPhysicalOf(src, base + ibRegFigure::Turnover);
		const wxString rec  = ibRegPhysicalOf(src, base + ibRegFigure::Receipt);
		const wxString exp  = ibRegPhysicalOf(src, base + ibRegFigure::Expense);

		r.m_columns.push_back({ ibRegPhysicalOf(out, base + ibRegFigure::OpeningBalance), turn, wxString(), ibMaterializeAgg::Value, ibMaterializeWhen::BeforeFrom, true });
		r.m_columns.push_back({ ibRegPhysicalOf(out, base + ibRegFigure::Receipt),        rec,  wxString(), ibMaterializeAgg::Value, ibMaterializeWhen::InRange,    true });
		r.m_columns.push_back({ ibRegPhysicalOf(out, base + ibRegFigure::Expense),        exp,  wxString(), ibMaterializeAgg::Value, ibMaterializeWhen::InRange,    true });
		r.m_columns.push_back({ ibRegPhysicalOf(out, base + ibRegFigure::Turnover),       turn, wxString(), ibMaterializeAgg::Value, ibMaterializeWhen::InRange,    true });
		r.m_columns.push_back({ ibRegPhysicalOf(out, base + ibRegFigure::ClosingBalance), turn, wxString(), ibMaterializeAgg::Value, ibMaterializeWhen::UpToTo,     true });
	}

	return RenderMaterializedRead(r, alias);
}
