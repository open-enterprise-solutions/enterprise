////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : calculationRegister manager
////////////////////////////////////////////////////////////////////////////

#include "calculationRegister.h"
#include "calculationRegisterManager.h"
#include "chartOfCalculationTypes.h"   // the relations a base and a displacement are read by

#include "backend/system/value/valueMap.h"
#include "backend/system/value/valueTable.h"
#include "backend/appData.h"
#include "backend/session/session.h"
#include "backend/query/dataQueryBuilder.h"   // L3 door — From() + Where materialises the read through L3
#include "backend/query/dbTableProvider.h"    // ibDbTableProvider::GetValueAttribute — the DB value-assembly
#include "backend/databaseLayer/databaseQueryBuilder.h"   // L2 — structured IR
#include "backend/metaCollection/partial/registerQueryLowering.h"   // ibRegFilterPredicate (shared lowering)
#include "backend/query/columnLayout.h"                              // ibOwnerRefField — the Base row's owner, as a field
#include "backend/metaCollection/dimension/metaDimensionObject.h"     // ibValueMetaObjectDimension (GetBase matching)
#include "backend/metaCollection/resource/metaResourceObject.h"       // ibValueMetaObjectResource (GetBase value columns)

#include <map>
#include <set>

ibValue ibValueManagerDataObjectCalculationRegister::Get(const ibValue& cFilter)
{
	ibRequireOpenBase();

	ibValueModelTable* retTable = new ibValueModelTable();
	// 🛑 Held while its rows are made: a row holds its table, so without this the first
	// `wxDELETE(retLine)` deletes a table nobody else holds yet. See valueQueryable.cpp, M::ToTable.
	const ibValue keep(retTable);
	ibValueModelTable::ibValueModelColumnCollection* colCollection = retTable->GetColumnCollection();
	wxASSERT(colCollection);
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		ibValueModelTable::ibValueModelColumnCollection::ibValueModelColumnInfo* colInfo = colCollection->AddColumn(object->GetName(), object->GetTypeDesc(), object->GetSynonym());
		colInfo->SetColumnID(object->GetMetaID());
	}

	// The Structure a script passes becomes the condition here — the SAME converter the query door
	// uses, so a script's filter and a query's condition are one thing from this point on.
	const ibQueryPredicatePtr filter = ibRegFilterPredicate(m_metaObject, cFilter, ibRegFilterOver::Records);

	// Filtered read through the L3 door: each selected dimension is an Eq condition,
	// decomposed inside L3 across its physical fields. Rows come from the L3
	// selection (GetValue) — no statement, no raw result set here.
	//
	// 🛑 NO `catch (...) {}` AROUND THE READS OF THIS FILE ANY MORE. Every one of them turned a failed
	// read — a missing column, a refused statement — into an empty table, which a payroll calculation
	// then reads as "nothing was accrued". The block stays so the cursor closes before the rows are
	// worked on; the error goes to whoever asked.
	{
		ibDataQueryBuilder q;
		q.From(m_metaObject->GetQueryable());
		q.Where(filter);
		ibReadPageRequest page;
		page.m_count = 0;   // every matching record
		ibDataQueryResult selection = q.Execute(page);
		while (selection.Next()) {
			ibValueModelTable::ibValueModelTableReturnLine* retLine = retTable->GetRowAt(retTable->AppendRow());
			wxASSERT(retLine);
			for (const auto object : m_metaObject->GetGenericAttributeArrayObject())
				retLine->SetValueByMetaID(object->GetMetaID(), selection.GetValue(object->GetQueryColumn()));
			wxDELETE(retLine);
		}
	}

	return retTable;
}

ibValue ibValueManagerDataObjectCalculationRegister::Get(const ibValue& cPeriod, const ibValue& cFilter)
{
	ibRequireOpenBase();

	ibValueModelTable* retTable = new ibValueModelTable();
	const ibValue keep(retTable);   // held while its rows are made — see Get(filter) above
	ibValueModelTable::ibValueModelColumnCollection* colCollection = retTable->GetColumnCollection();
	wxASSERT(colCollection);
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		ibValueModelTable::ibValueModelColumnCollection::ibValueModelColumnInfo* colInfo =
			colCollection->AddColumn(
				object->GetName(),
				object->GetTypeDesc(),
				object->GetSynonym()
			);
		colInfo->SetColumnID(object->GetMetaID());
	}

	// A calculation register is always dated — by its REGISTRATION period, the one period it has.
	const ibQueryPredicatePtr filter = ibRegFilterPredicate(m_metaObject, cFilter, ibRegFilterOver::Records);

	// Period + dimension filtered read through the L3 door: the period is an Eq
	// condition like any selected dimension; L3 decomposes each across its physical
	// fields and binds them. Rows come from the L3 selection (GetValue) — no raw
	// statement, no per-DBMS SQL here.
	{
		ibDataQueryBuilder q;
		q.From(m_metaObject->GetQueryable());
		q.Where(m_metaObject->GetRegistrationPeriod()->GetQueryColumn(), ibQueryFilterOp::Equal, cPeriod);
		q.Where(filter);
		ibReadPageRequest page;
		page.m_count = 0;   // every matching record
		ibDataQueryResult selection = q.Execute(page);
		while (selection.Next()) {
			ibValueModelTable::ibValueModelTableReturnLine* retLine = retTable->GetRowAt(retTable->AppendRow());
			wxASSERT(retLine);
			for (const auto object : m_metaObject->GetGenericAttributeArrayObject())
				retLine->SetValueByMetaID(object->GetMetaID(), selection.GetValue(object->GetQueryColumn()));
			wxDELETE(retLine);
		}
	}

	return retTable;
}

// ---- the base, asked of the DATABASE ------------------------------------------------------------------
// (A column's field by role, and one value compared across two tables, are the register-shared lowering's:
// ibRegFieldOfRole / ibRegSameValueIR — the actual-periods write joins by the same rule.)

// ⭐⭐ THE BASE IS ASKED OF THE DATABASE, IN ONE STATEMENT. It used to be computed here: the whole base
// register read into memory — every piece of every record, no condition at all — and then every dependent
// record scored against every base record. Memory the size of the register and time its square, and the
// register only grows: forty thousand employees are forty thousand records a month, kept for years.
//
// Everything the answer is made of is already a table: the dependent records, the chart's Base section
// (which type feeds which — its rows are OWNED by the dependent type), and the base register's actual
// pieces (<register>_AP, kept by the write) or its records. So the database joins them and hands back
// only the pairs that meet: a dependent record, a piece of a base record it takes, the days they share,
// and the days that base record is in force in all. One row per such pair; what the base register holds
// beyond them is never read.
//
// The division stays here, in exact decimals, as it always was: a NUMERIC divided in SQL keeps the scale
// of its operands and cuts at the cent on every piece. `baseOf` gets one sum per base resource for each
// dependent record that takes any base, keyed by (recorder, line).
static void ibCalcReadBase(const ibValueMetaObjectCalculationRegister* self, const ibValueMetaObjectCalculationRegister* base,
	const ibValueMetaObjectChartOfCalculationTypes* chart, ibBaseDependence dependence, const ibQueryPredicatePtr& filter,
	const std::vector<ibValueMetaObjectResource*>& baseResources,
	std::map<std::pair<ibValue, ibValue>, std::vector<ibNumber>>& baseOf)
{
	const ibValueMetaObjectCalculationTypeRelationTable* feeds = chart->GetBaseTable();
	if (feeds == nullptr || !feeds->IsAllowed() || feeds->GetCalculationType() == nullptr)
		return;   // no Base section in this configuration: no type feeds any other

	// d — the dependent records; r — the Base rows OWNED by a dependent record's type.
	const wxString typeId = ibRegFieldOfRole(self->GetCalculationType()->GetQueryColumn(), ibColumnRole::ReferenceId);
	if (typeId.IsEmpty())
		return;
	const ibQueryExprPtr onFeeds = ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("r"), ibOwnerRefField()), ibCol(wxT("d"), typeId));

	// p — a base record of the type the row names, holding the dependent record's value in every dimension
	// the two registers share by name, in force (or registered) within the dependent record's base period.
	ibQueryExprPtr onBase;
	const auto also = [&onBase](const ibQueryExprPtr& term) {
		if (term)
			onBase = onBase ? ibBinOp(ibQueryBinOp::And, onBase, term) : term;
	};
	for (const ibColumnRole role : { ibColumnRole::ReferenceType, ibColumnRole::ReferenceId }) {
		const wxString named = ibRegFieldOfRole(feeds->GetCalculationType()->GetQueryColumn(), role);
		const wxString held = ibRegFieldOfRole(base->GetCalculationType()->GetQueryColumn(), role);
		if (!named.IsEmpty() && !held.IsEmpty())
			also(ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("p"), held), ibCol(wxT("r"), named)));
	}
	for (const ibValueMetaObjectDimension* mine : self->GetDimensionArrayObject())
		for (const ibValueMetaObjectDimension* theirs : base->GetDimensionArrayObject())
			if (theirs->GetName() == mine->GetName())
				also(ibRegSameValueIR(theirs->GetQueryColumn(), wxT("p"), mine->GetQueryColumn(), wxT("d")));

	const ibQueryExprPtr bs = ibCol(wxT("d"), ibRegFieldOfRole(self->GetBasePeriodStart()->GetQueryColumn(), ibColumnRole::Date));
	const ibQueryExprPtr be = ibCol(wxT("d"), ibRegFieldOfRole(self->GetBasePeriodEnd()->GetQueryColumn(), ibColumnRole::Date));

	ibDatabaseQueryBuilder q;
	q.From(ibScan(self->GetPhysicalTableName(), wxT("d")))
	 .Join(ibScan(feeds->GetPhysicalTableName(), wxT("r")), onFeeds, ibQueryJoinType::Inner);

	// ⚠ NO CONSTANT RIDES IN THE ARITHMETIC. A literal becomes a bound parameter, and Firebird cannot type
	// a parameter inside an expression of the select list — `DATEDIFF(...) + ?` is refused with "Data type
	// unknown" (MEASURED 2026-09-10). So the statement returns what needs no constant — the days BETWEEN
	// the ends, and the number of pieces — and the one day each piece has on top is added here.
	const bool byActionPeriod = (dependence == ibBaseDependence::eBaseByActionPeriod);
	std::vector<ibQueryProjItem> measures;
	std::vector<ibQueryExprPtr> pieceKey;   // a piece of a base record — what one row of the answer is, besides d
	if (byActionPeriod) {
		// The days a piece shares with the base period, less one — both ends are included, as every
		// period here is.
		const wxString startField = ibRegFieldOfRole(base->GetActionPeriodStart()->GetQueryColumn(), ibColumnRole::Date);
		const wxString endField = ibRegFieldOfRole(base->GetActionPeriodEnd()->GetQueryColumn(), ibColumnRole::Date);
		const ibQueryExprPtr s = ibCol(wxT("p"), startField), e = ibCol(wxT("p"), endField);
		also(ibBinOp(ibQueryBinOp::Le, s, be));
		also(ibBinOp(ibQueryBinOp::Ge, e, bs));
		const ibQueryExprPtr from = ibCase({ { ibBinOp(ibQueryBinOp::Gt, s, bs), s } }, bs);
		const ibQueryExprPtr to = ibCase({ { ibBinOp(ibQueryBinOp::Lt, e, be), e } }, be);
		q.Join(ibScan(base->GetActualActionPeriodTableName(), wxT("p")), onBase, ibQueryJoinType::Inner);

		// a — the days each base record is in force in ALL: the sum of its pieces, every one of them, not
		// only those the base period meets (a salary cut by a sick leave spreads its whole result over the
		// days it is left with). Returned as the days between the ends of each piece and the number of
		// pieces, for the reason above.
		//
		// ⭐⭐ JOINED BY THE RECORD'S OWN KEY, AND ONLY FOR THE RECORDS THE BASE TAKES. It was a derived table
		// summing EVERY piece of the register, joined afterwards — MEASURED 2026-09-10 at 1000 employees
		// (debug): GetBase took 8 s over six months of history and 4 s over one, the difference being that
		// aggregate over the whole history. Now each piece p pulls in its own record's pieces through the
		// pieces table's unique index (recorder, line, start) and the answer is grouped back to one row per
		// (dependent record, piece) — what the base does not touch is never summed.
		ibQueryExprPtr onRecord;
		for (const ibValueMetaObjectAttributeBase* key : { base->GetRegisterRecorder(), base->GetRegisterLineNumber() }) {
			for (const wxString& field : ibRegFieldsOf(key)) {
				const ibQueryExprPtr eq = ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("a"), field), ibCol(wxT("p"), field));
				onRecord = onRecord ? ibBinOp(ibQueryBinOp::And, onRecord, eq) : eq;
				pieceKey.push_back(ibCol(wxT("p"), field));
			}
		}
		pieceKey.push_back(s);
		q.Join(ibScan(base->GetActualActionPeriodTableName(), wxT("a")), onRecord, ibQueryJoinType::Inner);

		// One piece p per group, so its overlap is one value; MAX says so in the form an aggregate wants.
		measures.push_back(ibQueryProjItem{ ibFunc(wxT("MAX"), { ibDateDiff(from, to, ibTotalsPeriod::Day) }), wxT("calc_overlap") });
		measures.push_back(ibQueryProjItem{ ibFunc(wxT("SUM"), {
			ibDateDiff(ibCol(wxT("a"), startField), ibCol(wxT("a"), endField), ibTotalsPeriod::Day) }), wxT("calc_days") });
		measures.push_back(ibQueryProjItem{ ibFunc(wxT("COUNT"), { ibCol(wxT("a"), startField) }), wxT("calc_pieces") });
	}
	else {
		// By registration period: a record counts whole when it is registered inside the base period — one
		// day of one, nothing to measure.
		const ibQueryExprPtr registered = ibCol(wxT("p"), ibRegFieldOfRole(base->GetRegistrationPeriod()->GetQueryColumn(), ibColumnRole::Date));
		also(ibBinOp(ibQueryBinOp::Ge, registered, bs));
		also(ibBinOp(ibQueryBinOp::Le, registered, be));
		q.Join(ibScan(base->GetPhysicalTableName(), wxT("p")), onBase, ibQueryJoinType::Inner);
	}

	// The dependent records asked about — the caller's filter, on d.
	std::vector<std::pair<const ibBackendQueryColumn*, ibValue>> leaves;
	ibRegFlatLeaves(filter, leaves);
	for (const auto& leaf : leaves)
		if (leaf.first != nullptr)
			q.Where(ibRegCompositeIR(leaf.first, self->GetMetaData(), leaf.second, ibQueryBinOp::Eq, wxT("d")));

	// Projected under the fields' own names, so each value is read back the way every register reads it.
	std::vector<ibQueryProjItem> proj;
	for (const ibValueMetaObjectAttributeBase* key : { self->GetRegisterRecorder(), self->GetRegisterLineNumber() })
		for (const wxString& field : ibRegFieldsOf(key))
			proj.push_back(ibQueryProjItem{ ibCol(wxT("d"), field), field });
	for (const ibValueMetaObjectResource* resource : baseResources)
		for (const wxString& field : ibRegFieldsOf(resource))
			proj.push_back(ibQueryProjItem{ ibCol(wxT("p"), field), field });
	// With the pieces of each piece's record pulled in beside it (a, above), the rows are grouped back to
	// one per (dependent record, piece): every plain column the answer carries is a key of the group.
	if (!pieceKey.empty()) {
		for (const ibQueryProjItem& item : proj)
			q.GroupBy(item.m_expr);
		for (const ibQueryExprPtr& key : pieceKey)
			q.GroupBy(key);
	}
	for (const ibQueryProjItem& measure : measures)
		proj.push_back(measure);
	q.Project(proj);

	ibQueryResult rs = q.Execute();
	while (rs.Next()) {
		ibValue recorder, line;
		ibDbTableProvider::GetValueAttribute(self->GetRegisterRecorder(), recorder, rs);
		ibDbTableProvider::GetValueAttribute(self->GetRegisterLineNumber(), line, rs);
		const ibNumber overlap = byActionPeriod ? rs.GetResultNumber(wxT("calc_overlap")) + ibNumber(1) : ibNumber(1);
		const ibNumber total = byActionPeriod
			? rs.GetResultNumber(wxT("calc_days")) + rs.GetResultNumber(wxT("calc_pieces")) : ibNumber(1);
		if (total <= ibNumber(0) || overlap <= ibNumber(0))
			continue;
		std::vector<ibNumber>& sums = baseOf[{ recorder, line }];
		sums.resize(baseResources.size(), ibNumber(0));
		for (size_t c = 0; c < baseResources.size(); ++c) {
			ibValue value;
			ibDbTableProvider::GetValueAttribute(baseResources[c], value, rs);
			sums[c] += value.GetNumber() * overlap / total;
		}
	}
}

// GetBase(BaseRegister, Filter) — see the header. The dependent records are read through the door as
// every register's are; their base comes from the database in one statement (ibCalcReadBase). Matching
// is by shared-name dimension VALUES: a base record contributes to a dependent record only when every
// dimension they have in common holds an equal value.
ibValue ibValueManagerDataObjectCalculationRegister::GetBase(const ibValue& cBaseRegister, const ibValue& cFilter)
{
	ibRequireOpenBase();

	// The base is meaningful only when the dependent register has a base period. Missing it -> an empty
	// (columns-only) table, not a guess. Which period of a base record counts is the chart's (below).
	ibValueManagerDataObjectCalculationRegister* baseMgr =
		cBaseRegister.ConvertToType<ibValueManagerDataObjectCalculationRegister>();
	const ibValueMetaObjectCalculationRegister* baseMeta = baseMgr ? baseMgr->GetMetaObject() : nullptr;

	// Base resources become the value columns; a "Base<Resource>" name and a SYNTHETIC id keep them from
	// colliding with the dependent register's own attribute columns — the id the tree gives a column
	// nobody declared (queryColumn.h: negative, its kind composed on), asked in one place for both the
	// column and the cell. It was the base resource's metaID with the high bit set: a band in the
	// positive space, the scheme that header retired after one band came to hold two tenants.
	std::vector<ibValueMetaObjectResource*> baseResources =
		baseMeta ? baseMeta->GetResourceArrayObject() : std::vector<ibValueMetaObjectResource*>();
	const auto baseColumnId = [](const ibValueMetaObjectResource* res) {
		return ibBackendQueryColumn::SyntheticId(ibBackendQueryColumn::SyntheticKind::Derived, res->GetMetaID());
	};

	ibValueModelTable* retTable = new ibValueModelTable();
	const ibValue keep(retTable);   // held while its rows are made — see Get(filter) above
	ibValueModelTable::ibValueModelColumnCollection* colCollection = retTable->GetColumnCollection();
	wxASSERT(colCollection);
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		auto* colInfo = colCollection->AddColumn(object->GetName(), object->GetTypeDesc(), object->GetSynonym());
		colInfo->SetColumnID(object->GetMetaID());
	}
	for (const auto res : baseResources) {
		auto* colInfo = colCollection->AddColumn(wxT("Base") + res->GetName(), res->GetTypeDesc(), res->GetSynonym());
		colInfo->SetColumnID(baseColumnId(res));
	}

	if (baseMeta == nullptr || !m_metaObject->IsUseBasePeriod())
		return retTable;

	// 🛑 A BASE IS MADE OF THE TYPES THE CHART NAMES, and of nothing else. This reading used to take
	// every base record whose dimensions matched — MEASURED 2026-09-10: a bonus with no base declared came
	// back with the whole salary as its base, and so would a sick-leave payment or another bonus on the
	// same employee. A dependent type with no Base rows has no base; that is an answer, not a gap.
	// The relation is the DEPENDENT register's chart's: the bonus says what its base is, whichever register
	// the base is then read from (a base register on another chart names no type of this one and so feeds
	// nothing). It is joined in the database now (ibCalcReadBase), not read here.
	const ibValueMetaObjectChartOfCalculationTypes* ownChart = m_metaObject->GetChartOfCalculationTypes();

	// ---- WHICH PERIOD OF A BASE RECORD COUNTS: the dependent chart's answer (BaseDependence) ----------
	// It used to be decided by what the BASE register happened to have — its pieces if it kept action
	// periods, its registration period if not — so the same bonus took a different base depending on
	// which register it was pointed at, and nothing in the configuration said so. Asked of a chart that
	// takes no base, or by action period of a register that keeps none, it refuses: either answer
	// would be a number the configuration never asked for.
	const ibBaseDependence dependence = ownChart != nullptr ? ownChart->GetBaseDependence() : ibBaseDependence::eBaseNone;
	if (dependence == ibBaseDependence::eBaseNone) {
		ibBackendCoreException::Error(_("Register '%s': its chart of calculation types takes no base - set the chart's 'Base dependence'"),
			m_metaObject->GetSynonym());
	}
	if (dependence == ibBaseDependence::eBaseByActionPeriod && baseMeta->GetActualActionPeriodQueryable() == nullptr) {
		ibBackendCoreException::Error(_("Register '%s' takes its base by action period, but the base register '%s' keeps no action periods"),
			m_metaObject->GetSynonym(), baseMeta->GetSynonym());
	}

	// ---- the base of every dependent record asked about, from the database -----------------------------
	const ibQueryPredicatePtr filter = ibRegFilterPredicate(m_metaObject, cFilter, ibRegFilterOver::Records);
	std::map<std::pair<ibValue, ibValue>, std::vector<ibNumber>> baseOf;   // (recorder, line) -> per base resource
	ibCalcReadBase(m_metaObject, baseMeta, ownChart, dependence, filter, baseResources, baseOf);

	// ---- the dependent records, each with its base -----------------------------------------------------
	{
		ibDataQueryBuilder q;
		q.From(m_metaObject->GetQueryable());
		q.Where(filter);
		ibReadPageRequest page;
		page.m_count = 0;
		ibDataQueryResult sel = q.Execute(page);
		while (sel.Next()) {
			ibValueModelTable::ibValueModelTableReturnLine* retLine = retTable->GetRowAt(retTable->AppendRow());
			wxASSERT(retLine);
			for (const auto object : m_metaObject->GetGenericAttributeArrayObject())
				retLine->SetValueByMetaID(object->GetMetaID(), sel.GetValue(object->GetQueryColumn()));

			const auto found = baseOf.find({ sel.GetValue(m_metaObject->GetRegisterRecorder()->GetQueryColumn()),
				sel.GetValue(m_metaObject->GetRegisterLineNumber()->GetQueryColumn()) });
			for (size_t c = 0; c < baseResources.size(); ++c) {
				const ibNumber sum = found != baseOf.end() ? found->second[c] : ibNumber(0);
				retLine->SetValueByMetaID(baseColumnId(baseResources[c]), ibValue(sum));
			}
			wxDELETE(retLine);
		}
	}

	return retTable;
}
