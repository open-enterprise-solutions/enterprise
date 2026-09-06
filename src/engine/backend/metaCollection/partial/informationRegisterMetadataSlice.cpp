////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : information register metadata - SLICE: the reading and its virtual table
////////////////////////////////////////////////////////////////////////////
//
// ONE PLACE FOR THE SLICE. The reading is a clean function ON THE METAOBJECT (its own table and
// dimensions are what a slice is made of); the companion queryable publishes it to L3; and the
// manager keeps nothing of it - it builds the companion, reads it through the door and dresses the
// rows for a script (informationRegisterManager_impl.cpp).
//
// ⚠ AND THERE IS NO VIEW HERE, on purpose. A slice is "the record nearest a MOMENT", so the answer
// depends on the moment asked for - there is nothing to accumulate and nothing a materialised
// surface could hold. The accumulation register materialises because a SUM does not depend on the
// question; a slice does. The register table IS the source, and the slice is a query over it.
//

#include "informationRegister.h"
#include "informationRegisterManager.h"

#include "backend/system/value/valueMap.h"
#include "backend/system/value/valueTable.h"
#include "backend/appData.h"
#include "backend/session/session.h"
#include "backend/query/dataQueryBuilder.h"   // L3 door — From(slice) + Select materialises the slice through L3
#include "backend/query/dbTableProvider.h"    // ibDbTableProvider::GetValueAttribute — the DB value-assembly
#include "backend/databaseLayer/databaseQueryBuilder.h"   // L2 — structured IR for the slice self-join (ComputeSlice)
#include "backend/metaCollection/partial/registerQueryLowering.h"   // ibRegFieldsOf / ibRegCompositeIR (shared lowering)

#include <set>   // the partition-key names a pushed-down filter is checked against


// ibValueMetaObjectInformationRegister::ComputeSlice — THE one place the period-slice
// is computed, and it belongs to the REGISTER (it is the register's own table /
// dimensions / SQL knowledge): build the self-join query, run it, materialise the
// slice table (one row per dimension key). The slice companion queryable calls it
// through m_reg, so BOTH the runtime (the manager's Slice* / Get*) and the L3 door
// (ComputeRows, when it materialises a query) reach this one compute — no parallel
// paths. The period bound is the only difference: last = MAX / "<=", first = MIN / ">=".
// The recorder's IDENTITY field — the reference id, asked by ROLE. Not "the first value field":
// a reference spreads to _TYPE, _RTRef, _RRRef, and the one that identifies the row is the last of
// those. Empty when the register has no recorder.
static wxString RecorderIdField(const ibValueMetaObjectInformationRegister* meta)
{
	const ibValueMetaObjectAttributeBase* recorder = meta->HasRecorder() ? meta->GetRegisterRecorder() : nullptr;
	if (recorder == nullptr)
		return wxString();

	for (const ibColumnSlot& slot : DescribeColumnLayout(recorder->GetQueryColumn()))
		if (slot.m_role == ibColumnRole::ReferenceId)
			return slot.m_name;
	return wxString();
}

// ⭐ WHERE THE SLICE STOPS — an instant, or an instant AND the document at it.
//
// A register SUBORDINATE TO A RECORDER can hold several records under one period for one key, so a
// date alone cannot say which of them the slice is taken before: three documents on one day are
// three different answers. The recorder separates them, compared after the period and in the same
// order the moment value compares by — one order, so the server and the runtime cannot disagree
// about which document came first.
//
// Without a recorder (an independent register) the period IS the key, and the plain comparison is
// the whole boundary.
static ibQueryExprPtr SliceBoundaryPredicate(const ibValueMetaObjectInformationRegister* meta,
                                             const ibMetaData* metaData,
                                             const ibValueMetaObjectAttributeBase* periodAttr,
                                             const ibRegBound& bound, bool last, ibQueryBinOp periodOp)
{
	// ⭐ NO MOMENT NAMED MEANS NO BOUNDARY — the whole register, sliced. `SliceLast` written without a
	// date is "the latest of each key, whenever that was", which is what the balance's own `Period`
	// argument already means when it is left out ("the reading runs to the last movement there is").
	// Compared against an empty date instead, every row failed the test and the slice came back
	// EMPTY — a register full of prices answering that it has none (measured 2026-09-05).
	if (bound.m_date.IsEmpty())
		return nullptr;

	const ibQueryExprPtr plain = ibRegCompositeIR(periodAttr->GetQueryColumn(), metaData, bound.m_date, periodOp);
	if (!bound.HasRecorder() || !meta->HasRecorder() || meta->GetRegisterRecorder() == nullptr)
		return plain;

	// Strictly past the instant, OR inside it and no later than the document named.
	const ibQueryBinOp strictly = last ? ibQueryBinOp::Lt : ibQueryBinOp::Gt;
	const ibQueryBinOp within   = periodOp;   // carries the Including / Excluding side already

	return ibBinOp(ibQueryBinOp::Or,
		ibRegCompositeIR(periodAttr->GetQueryColumn(), metaData, bound.m_date, strictly),
		ibBinOp(ibQueryBinOp::And,
			ibRegCompositeIR(periodAttr->GetQueryColumn(), metaData, bound.m_date, ibQueryBinOp::Eq),
			ibRegCompositeIR(meta->GetRegisterRecorder()->GetQueryColumn(), metaData, bound.m_recorder, within)));
}

ibQueryRamTable ibValueMetaObjectInformationRegister::ComputeSlice(
	const ibValue& cPeriod, const ibQueryPredicatePtr& cFilter, ibSliceEnd end) const
{
	const ibValueMetaObjectInformationRegister* meta = this;

	ibRequireOpenBase();

	// L3's own table (no runtime ibValueModelTable) — keyed by metaID; the script egress
	// (SelectionToTable) rebuilds the runtime table from the selection + metadata.
	ibQueryRamTable retTable;
	for (const auto object : meta->GetGenericAttributeArrayObject())
		retTable.AddColumn(object->GetMetaID(), object->GetName(), object->GetTypeDesc());

	if (meta->GetPeriodicity() != ibPeriodicity::eNonPeriodic ||
		meta->GetWriteRegisterMode() == ibWriteRegisterMode::eSubordinateRecorder) {
		// The condition arrives already converted (ibRegFilterPredicate) — one converter for every
		// register, so a script's Structure and a query's condition are the same thing by the time they
		// get here.
		std::vector<std::pair<const ibBackendQueryColumn*, ibValue>> selFilter;
		ibRegFlatLeaves(cFilter, selFilter);

		// Build the slice query through L2 (structured IR, dialect-free) instead of
		// hand-concatenated SQL: an aggregate subquery finds the boundary period per
		// dimension key (agg over the period, GROUP BY the keys), self-joined back to the
		// register table for the full record, then the dimension filter. Values ride as
		// bound Const — the renderer binds them; no per-DBMS SQL, no manual positional bind.
		const wxString table = meta->GetPhysicalTableName();
		const ibValueMetaObjectAttributeBase* periodAttr = meta->GetRegisterPeriod();

		// Field-list + composite-predicate lowering are shared across register managers
		// (registerQueryLowering.h): ibRegFieldsOf / ibRegCompositeIR.

		// ⭐ BOTH HALVES FROM ONE FACT. The LAST slice looks backwards (`period <= moment`) and takes
		// the greatest period in each group; the FIRST looks forwards and takes the least. Written as
		// two strings passed in separately, the pair could disagree — and the chain that parsed them
		// defaulted silently, so a typo produced a wrong slice instead of a complaint.
		const bool last = (end == ibSliceEnd::Last);
		const wxString     aggregateFn = last ? wxT("MAX") : wxT("MIN");

		// ⭐⭐ A SLICE STOPS WHERE A BALANCE STOPS, so it reads its boundary the same way: a bare date,
		// a moment naming a document, or a Boundary saying which side of that position is meant. One
		// function for every entrance (ibReadRegisterBound), so `SliceLast(date)` and
		// `SliceLast(Boundary(moment, Excluding))` differ only in what they mean, never in how they
		// are read.
		const ibRegBound bound = ibReadRegisterBound(cPeriod);

		// EXCLUDING opens the comparison rather than nudging the value — the same rule the register
		// cut follows, and for the same reason: moving the instant instead would guess at the grain
		// and still admit whatever was recorded within it.
		const ibQueryBinOp periodOp = last
			? (bound.m_excluding ? ibQueryBinOp::Lt : ibQueryBinOp::Le)
			: (bound.m_excluding ? ibQueryBinOp::Gt : ibQueryBinOp::Ge);

		ibQueryIR sliceIr;
		ibConnectionScope scope;

		// ⭐⭐ TWO SPELLINGS OF ONE SLICE, and which one is written depends on the engine.
		//
		// "The record nearest a moment" is a RANKING question, and an engine that can rank answers it
		// in ONE pass: number the rows of each key by period (and by the recorder that separates
		// documents sharing an instant), keep number one. The aggregate road below reaches the same
		// answer through MAX per key, a second level to break the tie, and a join back for the record
		// — three visits to the register's table where the ranked form makes one.
		//
		// The old road STAYS, because it is the only one on an engine without windows (the ANSI
		// baseline, i.e. ODBC and whatever derives from it). Same rule as a batched INSERT, which is
		// VALUES on one engine and UNION ALL on another: a second SPELLING, not a second mechanism —
		// and both must answer identically, which is what makes the pair testable.
		if (ibCanPushWindow(scope.get())) {
			// PARTITION BY exactly what the aggregate road GROUPS BY — the period's type tag, the
			// dimensions, the resources. Identical on purpose: partition the slice differently and the
			// two roads stop answering the same question.
			std::vector<ibQueryExprPtr> partitionBy;
			std::vector<wxString>       sliceFields;
			std::vector<ibQuerySortKey> orderBy;

			// The direction IS the reading: a last slice ranks the greatest period first, a first slice
			// the least. One fact, both halves — the same reason the aggregate road derives MAX / MIN
			// from `last` rather than taking two strings.
			const ibQuerySortDir dir = last ? ibQuerySortDir::Desc : ibQuerySortDir::Asc;

			const std::vector<wxString> rankPeriodFields = ibRegFieldsOf(periodAttr);
			for (size_t i = 0; i < rankPeriodFields.size(); ++i) {
				sliceFields.push_back(rankPeriodFields[i]);
				if (i == 0)   // _TYPE tag — a partition key, exactly as it is a GROUP BY key below
					partitionBy.push_back(ibCol(rankPeriodFields[i]));
				else          // the period VALUE — what the ranking orders by
					orderBy.push_back(ibQuerySortKey{ ibCol(rankPeriodFields[i]), dir });
			}

			// ⭐⭐ THE SLICE IS KEYED BY THE DIMENSIONS. A RESOURCE IS WHAT IT ANSWERS WITH.
			//
			// Partitioning by the resources too made every row its own partition the moment the value
			// changed — which is exactly when a slice matters. `SliceLast` then returned EVERY record
			// up to the date instead of the last one per key: three currencies, seven rows, all of
			// them rank 1 (measured 2026-09-05). The rows are still PROJECTED, of course — the slice
			// is read for its resources — they simply do not decide what a slice IS.
			auto addSliceKey = [&](const ibValueMetaObjectAttributeBase* a, bool partitions) {
				for (const wxString& f : ibRegFieldsOf(a)) {
					if (partitions) partitionBy.push_back(ibCol(f));
					sliceFields.push_back(f);
				}
			};
			for (const auto object : meta->GetDimensionArrayObject()) addSliceKey(object, /*partitions*/true);
			for (const auto object : meta->GetResourceArrayObject())  addSliceKey(object, /*partitions*/false);

			// ⭐ THE TIE IS ONE MORE SORT KEY. Where the aggregate road needs a whole extra level to
			// choose among the records sitting AT the boundary period, an ordered ranking already has
			// somewhere to put that question: after the period, in the same direction.
			const wxString rankRecorderId = RecorderIdField(meta);
			if (!rankRecorderId.IsEmpty()) {
				orderBy.push_back(ibQuerySortKey{ ibCol(rankRecorderId), dir });
				sliceFields.push_back(rankRecorderId);
			}

			// ⭐⭐ THE DIMENSION FILTER RIDES DOWN — but only the part that is SAFE to push.
			//
			// A condition on a PARTITION key narrows which partitions exist and changes nothing inside
			// any of them, so pushing it under the ranking is free: the slice of warehouse X is the
			// same rows whether the other warehouses were read and discarded or never read. That is
			// worth a great deal — the aggregate road ranks the WHOLE table and filters the finished
			// slice, so asking for one price reads every price.
			//
			// A condition on anything else is NOT free: filtering by recorder before the ranking would
			// answer "the last record BY THAT DOCUMENT" instead of "the last record, if that document
			// wrote it". Those stay outside, over the finished slice, where they mean what they say.
			std::set<wxString> partitionNames;
			for (const auto object : meta->GetDimensionArrayObject())
				if (object != nullptr) partitionNames.insert(object->GetName());
			for (const auto object : meta->GetResourceArrayObject())
				if (object != nullptr) partitionNames.insert(object->GetName());

			ibDatabaseQueryBuilder ranked;
			ranked.From(table);
			if (meta->HasRecorder())
				ranked.Where(ibRegCompositeIR(meta->GetRegisterActive()->GetQueryColumn(), GetMetaData(), ibValue(true), ibQueryBinOp::Eq));
			ranked.Where(SliceBoundaryPredicate(meta, GetMetaData(), periodAttr, bound, last, periodOp));

			std::vector<std::pair<const ibBackendQueryColumn*, ibValue>> keptOutside;
			for (auto filter : selFilter) {
				if (filter.first != nullptr && partitionNames.count(filter.first->GetName()) > 0)
					ranked.Where(ibRegCompositeIR(filter.first, GetMetaData(), filter.second, ibQueryBinOp::Eq));
				else
					keptOutside.push_back(filter);
			}

			std::vector<ibQueryProjItem> rankedProj;
			for (const wxString& f : sliceFields)
				rankedProj.push_back(ibQueryProjItem{ ibCol(f), wxString() });
			rankedProj.push_back(ibQueryProjItem{
				ibWindowed(ibFunc(wxT("ROW_NUMBER")), { partitionBy, orderBy, ibQueryFrame::NoFrame }),
				wxT("slice_rank") });
			ranked.Project(rankedProj);

			// Rank 1 is the slice. The test lives OUTSIDE the ranking because a window is computed
			// after WHERE — there is no such thing as a predicate on a rank in the same SELECT that
			// produces it, and a HAVING would not do it either.
			std::vector<ibQueryProjItem> sliceProj;
			for (const wxString& f : sliceFields)
				sliceProj.push_back(ibQueryProjItem{ ibCol(wxT("sliceTable"), f), f });

			ibDatabaseQueryBuilder slice;
			slice.From(ibSubquery(ranked.Build().m_root, wxT("sliceTable")))
			     .Where(ibBinOp(ibQueryBinOp::Eq,
			                    ibCol(wxT("sliceTable"), wxT("slice_rank")), ibConst(ibValue(1))))
			     .Project(sliceProj);

			for (auto filter : keptOutside)
				slice.Where(ibRegCompositeIR(filter.first, GetMetaData(), filter.second, ibQueryBinOp::Eq));

			sliceIr = slice.Build();
		}
		else {

		// Level 1 — boundary period per dimension key (aggregate subquery T1). The period
		// TYPE is grouped + projected bare; the period value is aggregated (MAX / MIN);
		// dimensions + resources are grouped + projected (the slice key).
		//
		// A register SUBORDINATE TO A RECORDER needs one more level after this one — see the tie
		// break below: the boundary period alone does not name a single row there.
		std::vector<ibQueryProjItem> l1proj;
		std::vector<ibQueryExprPtr>  groupKeys;
		const std::vector<wxString> periodFields = ibRegFieldsOf(periodAttr);
		for (size_t i = 0; i < periodFields.size(); ++i) {
			if (i == 0) {   // _TYPE tag: grouped + projected bare
				l1proj.push_back(ibQueryProjItem{ ibCol(periodFields[i]), wxString() });
				groupKeys.push_back(ibCol(periodFields[i]));
			}
			else {          // period value: aggregated (the boundary), NOT grouped
				l1proj.push_back(ibQueryProjItem{ ibFunc(aggregateFn, { ibCol(periodFields[i]) }), periodFields[i] });
			}
		}
		// The same key as the ranked road above, for the same reason and stated once more here: the
		// boundary is looked for PER DIMENSION KEY. A resource joined the GROUP BY made the boundary
		// per (key, value), which is every row that ever changed — the two roads were identically
		// wrong, which is the one thing a pair of roads is supposed to make visible.
		auto addKeyCols = [&](const ibValueMetaObjectAttributeBase* a) {
			for (const wxString& f : ibRegFieldsOf(a)) {
				l1proj.push_back(ibQueryProjItem{ ibCol(f), wxString() });
				groupKeys.push_back(ibCol(f));
			}
		};
		for (const auto object : meta->GetDimensionArrayObject()) addKeyCols(object);

		ibDatabaseQueryBuilder l1;
		l1.From(table);

		// ACTIVE EXISTS ONLY WHERE THE REGISTER IS SUBORDINATE TO A RECORDER — it is contributed by
		// FillArrayObjectByPredefinedAttribute under that condition, so an INDEPENDENT register (the
		// default) has no such column. Asking for it anyway produced SQL naming a field the table
		// does not have; the exception was swallowed by the catch below and the slice came back
		// EMPTY — which reads as "no data yet", not as "the query was wrong".
		if (meta->HasRecorder())
			l1.Where(ibRegCompositeIR(meta->GetRegisterActive()->GetQueryColumn(), GetMetaData(), ibValue(true), ibQueryBinOp::Eq));

		l1.Where(SliceBoundaryPredicate(meta, GetMetaData(), periodAttr, bound, last, periodOp))
		  .Project(l1proj);
		for (const ibQueryExprPtr& gk : groupKeys) l1.GroupBy(gk);

		// The fields the boundary is keyed by — the same set at every level below.
		// …and the join back to the table is on that same key — period + dimensions. Joining on the
		// resources as well would ask the table for the row that already matched itself.
		std::vector<const ibValueMetaObjectAttributeBase*> joinAttrs;
		joinAttrs.push_back(periodAttr);
		for (const auto object : meta->GetDimensionArrayObject()) joinAttrs.push_back(object);

		std::vector<wxString> keyFields;
		for (const auto a : joinAttrs)
			for (const wxString& f : ibRegFieldsOf(a))
				keyFields.push_back(f);

		// ⭐⭐ THE PERIOD DOES NOT PICK A ROW WHEN THE REGISTER IS SUBORDINATE TO A RECORDER.
		//
		// There the key is recorder + line, so several records can sit under one period for one
		// dimension set — three documents on one day are three different answers, and MAX(period)
		// has nothing to choose between them. It would return whichever the engine happened to join
		// first: never an error, never the same one twice, and wrong in a way nobody would question.
		//
		// So a second level breaks the tie among the rows AT the boundary period, by the RECORDER —
		// in the same direction the slice reads (the greatest for a last slice, the least for a
		// first) and by the same key a moment compares references with, the reference id.
		//
		// An independent register never reaches this: its key IS the period plus the dimensions, so
		// the boundary already names one row.
		ibQueryRelPtr boundary = ibSubquery(l1.Build().m_root, wxT("T1"));
		const wxString recorderId = RecorderIdField(meta);

		if (!recorderId.IsEmpty()) {
			ibQueryExprPtr tiePred;
			std::vector<ibQueryProjItem> tieProj;
			std::vector<ibQueryExprPtr>  tieKeys;
			for (const wxString& f : keyFields) {
				ibQueryExprPtr eq = ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("B"), f), ibCol(wxT("R"), f));
				tiePred = tiePred ? ibBinOp(ibQueryBinOp::And, tiePred, eq) : eq;
				tieProj.push_back(ibQueryProjItem{ ibCol(wxT("B"), f), f });
				tieKeys.push_back(ibCol(wxT("B"), f));
			}
			tieProj.push_back(ibQueryProjItem{
				ibFunc(aggregateFn, { ibCol(wxT("R"), recorderId) }), recorderId });

			ibDatabaseQueryBuilder tie;
			tie.From(ibSubquery(l1.Build().m_root, wxT("B")))
			   .Join(ibScan(table, wxT("R")), tiePred, ibQueryJoinType::Inner)
			   .Project(tieProj);
			for (const ibQueryExprPtr& gk : tieKeys) tie.GroupBy(gk);

			boundary = ibSubquery(tie.Build().m_root, wxT("T1"));
			keyFields.push_back(recorderId);   // the join below now equates the recorder too
		}

		// Level 2 — join the boundary back to the table for the full record. ON equates
		// every keyed field of T1 (boundary) and T2 (table); the projection is T2's same
		// fields (aliased to the bare name -> materialise by name).
		ibQueryExprPtr onPred;
		std::vector<ibQueryProjItem> l2proj;
		for (const wxString& f : keyFields) {
			ibQueryExprPtr eq = ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("T1"), f), ibCol(wxT("T2"), f));
			onPred = onPred ? ibBinOp(ibQueryBinOp::And, onPred, eq) : eq;
			l2proj.push_back(ibQueryProjItem{ ibCol(wxT("T2"), f), f });
		}

		ibDatabaseQueryBuilder l2;
		l2.From(boundary)
		  .Join(ibScan(table, wxT("T2")), onPred, ibQueryJoinType::Inner)
		  .Project(l2proj);

		// Level 3 — dimension filter over the joined slice: SELECT * FROM (l2) AS lastTable WHERE dims.
		ibDatabaseQueryBuilder l3;
		l3.From(ibSubquery(l2.Build().m_root, wxT("lastTable")));
		for (auto filter : selFilter)
			l3.Where(ibRegCompositeIR(filter.first, GetMetaData(), filter.second, ibQueryBinOp::Eq));

		sliceIr = l3.Build();

		}   // end of the aggregate road

		// Run through L2 (session holder) + materialise the slice table BY metaID — the
		// columns read back the same way the door / record form read them.
		//
		// 🛑 NOT WRAPPED IN `catch (...) {}` ANY MORE. It was, and the comment ninety lines up says
		// what that cost: a query naming a column the table does not have raised, the catch ate it,
		// and the slice came back EMPTY — which reads as "no data yet", not as "the query was wrong".
		// With two roads to this answer the silence would be worse still, since the one that broke
		// would be the one nobody looked at. A slice that cannot be computed is an error, and the
		// caller is entitled to hear which.
		ibQueryResult rs = ibDatabaseQueryBuilder().ExecuteIR(sliceIr);
		while (rs.Next()) {
			const long retRow = retTable.AppendRow();
			for (const auto object : meta->GetGenericAttributeArrayObject()) {
				ibValue retValue;
				if (ibDbTableProvider::GetValueAttribute(object, retValue, rs))
					retTable.SetCell(retRow, object->GetMetaID(), retValue);
			}
		}
	}

	return retTable;
}

//********************************************************************************************
//*    Slice companion queryable — a self-contained relation L3 reads / joins                *
//********************************************************************************************
// ComputeRows is the door's read hook for a computed (RAM) source: it builds the slice
// table from the filters baked into THIS instance's ctor (period + dimension filter)
// through the register's own compute (m_reg->ComputeSlice). Both the runtime (the
// manager's Slice* / Get*, which construct a configured slice and From() it) and a
// materialised query / JOIN reach that one place. All navigation forwards to the register.

ibQueryRamTable ibSliceQueryable::ComputeRows(const std::vector<ibQueryCondition>& /*extra*/) const
{
	// The slice's own scoping (period + dimension filter) rode in the ctor; build the
	// table from it. Extra door conditions (a further Where on the slice) compose on
	// top once the post-filter / JOIN-over-computed node lands — the manager path
	// passes none.
	return m_reg->ComputeSlice(m_period, m_filter, End());
}

// (The eight navigation methods forward to the register's own queryable — now shared in
// the ibComputedRegisterQueryable base; a slice returns the register's real records.)
