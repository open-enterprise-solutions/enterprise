////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : accumulation register — the derived TOTALS bundle declaration (L3-2)
////////////////////////////////////////////////////////////////////////////

#include "accumulationRegister.h"

#include "backend/query/schemaSnapshot.h"
#include "backend/query/queryColumn.h"
#include "backend/query/columnLayout.h"                          // DescribeColumnLayout -- physical fields WITH their canonical types
#include "backend/query/tempTableQueryable.h"                       // ibSchemaTableQueryable — the totals table as a source
#include "backend/metaCollection/attribute/metaAttributeObject.h"
#include "backend/metaCollection/partial/registerQueryLowering.h"   // ibRegValueField
#include "backend/databaseLayer/databaseMaterializeBuilder.h"      // ibCanMaterialize — ask L2-2, never a dialect
#include "backend/appData.h"                                        // db_query

// ============================================================================
// What this file DECLARES (it renders nothing):
//
//   mov      — the movements, contributed by the base. SOURCE data: dumped, restored, never derived.
//   totals   — one row per (period, dimensions), holding each resource as a RECEIVED / SPENT pair.
//              DERIVED: never moved, regenerated on demand, kept current by trigger.
//   3 views  — the read surface. Ordinary relations upstream; the materialisation is what they hide.
//
// Why the pair rather than one signed column: a net cannot be taken apart again. A period that
// received 100 and spent 100 is indistinguishable from a period with no movement at all, and the
// register already reports receipt and expense separately today. Two stored columns, and every
// reported figure derives from them — turnover is their difference, the balance is the running sum
// of that difference.
//
// Everything below speaks L2-2's vocabulary (Value / Difference / RunningSum). The words "balance"
// and "turnover" appear only in COLUMN NAMES, which is metadata's business; the layer that renders
// the SQL never learns them, so an accounting register composes the same primitives into its own
// tables without changing anything underneath.
// ============================================================================

void ibValueMetaObjectAccumulationRegister::ContributeTables(ibSchemaSnapshot& out) const
{
	// The movements table, its indexes and its columns.
	ibValueMetaObjectRegisterData::ContributeTables(out);

	// A turnover-only register carries no record type, so nothing signs its movements: there is no
	// expense side and no balance to run. It still gets totals — just the single accumulating
	// column and no balance views.
	const bool withSign = (GetRegisterType() == ibRegisterType::eBalances);

	// A register with NO RESOURCES has nothing to total: movements can be written, but no figure
	// will ever be accumulated from them and every report over it reads empty.
	//
	// WARN rather than refuse. It is a legitimate intermediate state — a register is built up over
	// several edits, and blocking the update would force the whole shape to be finished before it
	// could be applied once. But it is also a state nobody wants to KEEP, and silence about it is
	// how one ships: the movements record fine, and the emptiness only shows up in a report much
	// later. The warning is what makes the difference between "not finished yet" and "quietly
	// broken" visible at the moment it is created.
	if (GetResourceArrayObject().empty()) {
		RestructureWarning(wxString::Format(
			_("Register '%s' has no resources: no totals are maintained for it"), GetName()));
		return;
	}

	const wxString totalsName  = GetRegisterTableNameDB();
	const wxString periodField = ibRegValueField(GetRegisterPeriod());

	// ⭐⭐ THE INACTIVE KIND'S TABLE IS DECLARED TOO — EMPTY, AND WITHOUT MAINTENANCE.
	//
	// Both totals objects are PREDEFINED: whichever kind the register is, the other one is still in
	// the configuration and visible to both sides of every diff. Declaring only the active table made
	// the two disagree — object present, table absent — and the disagreement outlived a save:
	// switching the kind dropped one table, switching back produced "alter" rather than "create"
	// (the baseline still knew the object), so nothing re-created it and the maintenance was built
	// against a table that was gone. Exactly what the accounting register's credit side did.
	//
	// An empty relation costs storage and nothing else. Only the MAINTENANCE follows the setting,
	// which is what the setting means: the inactive kind accumulates nothing, so nothing writes here.
	{
		const ibRegisterType idleKind = (GetRegisterType() == ibRegisterType::eBalances)
			? ibRegisterType::eTurnovers : ibRegisterType::eBalances;
		if (const ibValueMetaObjectTotals* idle = GetTotalsObject(idleKind)) {
			ibSchemaTable& idleTable = out.Shared(idle->GetMetaID(), GetRegisterTableNameDB(idleKind));
			idleTable.Scaffold(ibRawDBColumn::Date(periodField));
			for (const auto dimension : GetDimensionArrayObject())
				idleTable.Add(dimension);
		}
	}

	// The identity comes from the totals METAOBJECT of the active kind — a real metaID, unique by
	// construction, stable across saves.
	const ibValueMetaObjectTotals* totals = GetTotalsObject();
	ibSchemaTable& t = out.Shared(totals->GetMetaID(), totalsName);

	// --- structure: the period, the dimensions, and a stored pair per resource -----------------
	const ibBackendQueryColumn* periodCol = t.Scaffold(ibRawDBColumn::Date(periodField));
	for (const auto dimension : GetDimensionArrayObject())
		t.Add(dimension);   // same physical fields as the movements, so a trigger reads NEW.<field> directly

	std::vector<const ibBackendQueryColumn*> keyCols;
	keyCols.push_back(periodCol);
	for (const auto dimension : GetDimensionArrayObject())
		keyCols.push_back(dimension);

	// SPLIT TOTALS: the shard column joins the KEY, which is what makes several physical rows
	// legal for one logical key. It exists only when the switch is on — turning the switch changes
	// the key shape, so it takes an Apply, and the regenerator rebuilds the table because a
	// re-keyed row cannot be migrated in place.
	//
	// The shard column carries an IDENTITY rather than being scaffold: turning the switch ADDS a
	// physical column to an existing table (and turning it back REMOVES one), and scaffold columns
	// are created with their table and never migrated. Without the id the differ would rebuild the
	// key around a column it never created.
	//
	// The totals object's own metaID names it. There is exactly one shard column per totals table and
	// it belongs to that table, so the table's identity IS the column's — a table id and a column id
	// are matched in different places and cannot be confused for one another.
	const bool split = IsTotalsSplitEnabled();
	const bool sharded = ibRegSplitIntoKey(t, totals, ShardColumnName(), split, keyCols);

	// The key is DECLARED here; how the engine is made to hold it unique is decided by
	// ibDeclareDerivedKey, because that answer depends on the engine rather than on the register — an
	// index has a segment ceiling, a reference dimension is three physical fields, and five reference
	// dimensions already pass Firebird's sixteen. Past the ceiling the identity moves into a hash
	// column and the match still runs over the key columns themselves.
	ibDeclareDerivedKey(t, totalsName, keyCols, totals->GetMetaID() | 0x40000000);

	ibSchemaMaterialize& m = t.Derived(GetQueryable());
	m.Split(sharded ? kTotalsShardCount : 1u);   // the COLUMN decides, not the setting -- see ibRegSplitIntoKey

	// STORED GRANULARITY = DAY, and the choice is load-bearing rather than a default.
	//
	// It is the FLOOR on what can ever be read back: a projection is derivable only into a unit no
	// finer than the stored one. Store by month and PeriodDay / PeriodWeek / PeriodTenDays — and
	// every sub-day unit — become unanswerable, because the information no longer exists. Six of the
	// ten units we expose would be dead.
	//
	// Store by second and the totals table degenerates into a copy of the movements: one row per
	// movement, no compression, no point.
	//
	// Day is where compression is still large (a key usually sees many movements a day) while the
	// coverage includes everything an accounting register is actually asked — day and coarser.
	// Sub-day readings are served by the MOVEMENTS, which is also where per-recorder and per-record
	// granularity comes from; that is the boundary the enum draws.
	//
	// It wants to become a per-register property: a register posted once a month per key gains
	// nothing from daily rows, and one that must answer hourly cannot use them. Introducing it later
	// is a property plus a regeneration — the mechanism already reads this value rather than assuming.
	m.Period(periodField, GetRegisterPeriod(), wxT("{row}.") + periodField, GetTotalsPeriodUnit());

	// ⭐⭐ AN INACTIVE MOVEMENT EXISTS AND COUNTS FOR NOTHING.
	//
	// Active is what separates a record that is THERE from a record that is IN FORCE: an entry
	// written but not in effect still occupies its row, still shows in the register, and must not
	// move a single figure. Flip the flag and the same row starts counting — nothing is rewritten,
	// the totals simply include it from then on.
	//
	// Declared as the delta's GUARD, which is the one place that meaning belongs: the trigger then
	// accumulates only what is in force, and the movement arm of the view reads under the same
	// condition. Filtering it in each READING instead would be the same rule written in as many
	// places as there are readings — and the day one of them forgot, an inactive entry would show up
	// in exactly one report.
	//
	// ⚠ THE GUARD IS AN EXPRESSION, AND IT HAS TO BE A BOOLEAN ONE. A "boolean" attribute is stored as
	// a SMALLINT, so `WHERE NEW.fld…_B` is a field where a condition is required — Firebird answers
	// "invalid usage of boolean expression" and the whole CREATE TRIGGER fails, taking the
	// restructuring that emitted it down with it. The comparison is written out for that reason, and
	// the cost of getting it wrong is asymmetric: nothing READS wrong, the APPLY does not finish.
	ibRegGuardInForce(m, GetRegisterActive());
	for (const auto dimension : GetDimensionArrayObject())
		m.Key(dimension);

	// --- the stored columns + what a movement contributes to each ------------------------------
	struct Pair { wxString m_in, m_out, m_name; };
	std::vector<Pair> pairs;

	for (const auto res : GetResourceArrayObject()) {
		const wxString resField = ibRegValueField(res);
		const wxString inName   = resField + wxT("_In");
		const wxString outName  = resField + wxT("_Out");

		// Accumulating columns carry an IDENTITY (not scaffold), so the differ can add and drop them
		// as the register gains resources or switches type. A scaffold field is created with its
		// table and never migrated — which would mean a register whose resources changed kept a
		// totals table shaped for the old ones. The two ids derive from the resource's own metaID,
		// so they are stable across saves and unique within the table — the second one through a
		// HIGH bit, which is the only range no metaID occupies (they are small sequential integers,
		// so a low-bit tweak lands on the neighbouring metaobject's id).
		// The figure is stored in the RESOURCE's own type (ibRegAccumulatorColumn) — declared flat it was
		// NUMERIC(18,0), so a resource carrying a fraction lost it on the way INTO the totals.
		const ibMetaID idIn  = res->GetMetaID();
		const ibMetaID idOut = res->GetMetaID() | 0x40000000;

		if (!withSign) {
			// No record type — nothing signs a movement, so there is no expense side to keep apart.
			const ibBackendQueryColumn* c = ibRegAccumulatorColumn(t, inName, idIn, res);
			m.Accumulate(c, wxT("{row}.") + resField, ibQueryColumnExpr::Col(res));
			pairs.push_back({ inName, wxString(), res->GetName() });
			continue;
		}

		const ibBackendQueryColumn* cIn  = ibRegAccumulatorColumn(t, inName,  idIn,  res);
		const ibBackendQueryColumn* cOut = ibRegAccumulatorColumn(t, outName, idOut, res);

		// The record type splits one movement into the two sides. The accumulate stays
		// UNCONDITIONAL — only the VALUE branches — so the trigger needs no procedural IF and the
		// delta template stays the same one every engine uses.
		//
		// ⚠ THE TAG IS THE ENUM'S ORDINAL, AND IT IS NOT ZERO FOR RECEIPT. `ibRecordType` declares
		// Expense first, so a movement stores 0 for an EXPENSE — while the column below is published
		// as `<Res>_Receipt` and the balance is computed as In − Out. Comparing against 0 therefore
		// filed every receipt as an expense and inverted the sign of every balance and turnover.
		//
		// Spelled through the enum rather than a literal, so the two can no longer disagree: what
		// goes into the receipt column is the movement whose record type IS Receipt.
		const wxString recField = ibRegValueField(GetRegisterRecordType());
		const int receiptTag = static_cast<int>(ibRecordType::eReceipt);
		const wxString receiptTagText = wxString::Format(wxT("%i"), receiptTag);

		m.Accumulate(cIn,  wxT("CASE WHEN {row}.") + recField + wxT(" = ") + receiptTagText + wxT(" THEN {row}.") + resField + wxT(" ELSE 0 END"),
			ibQueryColumnExpr::Case(
				{ { ibQueryPredicate::Leaf(ibQueryCondition{ GetRegisterRecordType(), ibQueryFilterOp::Equal, ibValue(receiptTag) }),
				    ibQueryColumnExpr::Col(res) } },
				ibQueryColumnExpr::Const(ibValue(0.0))));
		m.Accumulate(cOut, wxT("CASE WHEN {row}.") + recField + wxT(" = ") + receiptTagText + wxT(" THEN 0 ELSE {row}.") + resField + wxT(" END"),
			ibQueryColumnExpr::Case(
				{ { ibQueryPredicate::Leaf(ibQueryCondition{ GetRegisterRecordType(), ibQueryFilterOp::Equal, ibValue(receiptTag) }),
				    ibQueryColumnExpr::Const(ibValue(0.0)) } },
				ibQueryColumnExpr::Col(res)));

		pairs.push_back({ inName, outName, res->GetName() });
	}

	// --- the totals table AS A SOURCE -----------------------------------------------------------
	// This table is declared BY a metaobject but is not one, so nothing else vends a queryable for
	// it — and the door needs one to read or write anything. Both L3-4 operations gate on exactly
	// that (`m_queryable == nullptr` -> "nothing to do"), so without this binding regeneration and
	// the shard fold are unreachable code: they return success having touched nothing.
	//
	// Bound HERE, after every column exists, and built from the table's OWN declaration — the
	// scaffold period plus every logical column — so the source cannot drift from the schema it
	// describes. The physical TABLE, deliberately, not one of the views below: a view sums the
	// shards away, and the fold's whole business is the individual shard rows underneath.
	ibRegSelfSourceFromDeclaration(t, GetMetaData());

	// --- the read views, composed from L2-2 primitives ------------------------------------------
	// TURNOVERS — per period: what came in, what went out, and the net.
	{
		ibMaterializeView& v = m.View(GetTurnoverViewName(), /*withPeriod*/ true);

		// ⭐⭐ THE SECOND ARM: THIS VIEW ALSO CARRIES THE MOVEMENTS.
		//
		// A maintained total is complete only down to the grain it is stored at (a day). Everything
		// that happened INSIDE the current day is in the movements and nowhere else — so a balance
		// "at noon" is the stored total at midnight plus this morning's movements, and there is no
		// third place to get the second half from.
		//
		// Rather than a second relation and a fold in memory, the view offers both halves as ONE
		// relation: the stored rows UNION ALL the movements, each movement contributing exactly what
		// it contributed to the total (the same delta expressions the trigger accumulates through).
		// A reader takes stored rows BELOW the grain boundary and movement rows above it, so nothing
		// is counted twice; ask a question at the grain or coarser and the movement arm matches
		// nothing at all.
		//
		// The recorder and the line number ride along because they are what makes a row's own
		// identity readable — and what a boundary INSIDE one instant compares against, when three
		// documents share a date and have to be told apart.
		if (HasRecorder() && GetRegisterRecorder() != nullptr && GetRegisterLineNumber() != nullptr) {
			v.m_withMovements = true;
			// Name AND type — the stored arm stands a CAST null in their place (see the accounting
			// register's twin of this block, and ibMaterializeView::m_movementColumns).
			for (const ibColumnSlot& s : DescribeColumnLayout(GetRegisterRecorder()))
				v.m_movementColumns.push_back({ s.m_name, s.m_type });
			for (const ibColumnSlot& s : DescribeColumnLayout(GetRegisterLineNumber()))
				v.m_movementColumns.push_back({ s.m_name, s.m_type });
		}

		for (const Pair& p : pairs) {
			v.m_columns.push_back({ p.m_name + wxT("_Receipt"),  p.m_in,  wxString(), ibMaterializeAgg::Value });
			if (p.m_out.IsEmpty()) {
				v.m_columns.push_back({ p.m_name + wxT("_Turnover"), p.m_in, wxString(), ibMaterializeAgg::Value });
				continue;
			}
			v.m_columns.push_back({ p.m_name + wxT("_Expense"),  p.m_out, wxString(), ibMaterializeAgg::Value });
			v.m_columns.push_back({ p.m_name + wxT("_Turnover"), p.m_in,  p.m_out,    ibMaterializeAgg::Difference });
		}
	}

	if (!withSign)
		return;   // no sign, no balances

	// BALANCE — no period column at all: this answers "what is on hand", folded over every period.
	// dropZeroRows, because no stock means NO ROW — a row of zeros would list every item ever
	// traded instead of the ones actually held.
	{
		ibMaterializeView& v = m.View(GetBalanceViewName(), /*withPeriod*/ false, /*dropZeroRows*/ true);
		for (const Pair& p : pairs)
			v.m_columns.push_back({ p.m_name + wxT("_Balance"), p.m_in, p.m_out, ibMaterializeAgg::Difference });
	}

	// There is NO third view for balance-and-turnovers. It is not a third thing to store — it is
	// the opening balance, the turnovers of the interval, and the closing balance, which is how
	// anyone builds that report by hand: take the balance entering the period, the movements
	// within it, and the balance leaving it. All three come from the TURNOVERS view in a single
	// grouped pass with conditional sums (see ibBalanceAndTurnoverQueryable::GetSourceRelation).
	//
	// Dropping it removed a window function, and with it the one genuinely unfinished corner of
	// sharding: a running balance over shards is fiddly, an ordinary SUM over shards is not. The
	// gap closed by DELETING the construct that created it rather than by finishing it.
}

// ============================================================================
// The views AS SOURCES.
//
// A view is an ordinary named relation, so L3 needs nothing new to read one: ibDbTempTableQueryable
// already models exactly that — a table name plus generic columns, read by the standard physical
// provider. That is the payoff of the bridge. Everything a reader does to these — filter, join,
// page, restrict by role — is the engine's own machinery, and nothing upstream can tell that the
// numbers come from a trigger-maintained table split across shards.
// ============================================================================

bool ibValueMetaObjectAccumulationRegister::HasMaterializedViews() const
{
	// Presence of the driver's materialization dialect IS the capability: without it no view was
	// ever created, so a reader must fall back to live aggregation rather than query a relation
	// that does not exist.
	return db_query != nullptr && ibCanMaterialize(*db_query);
}

const ibBackendQueryable* ibValueMetaObjectAccumulationRegister::GetViewQueryable(
	const wxString& viewName, ibViewShape shape) const
{
	// One question, asked once: the turnovers view carries the period column, the balance
	// views do not. (There was a `withBalances` twin here, never read — it was just this
	// predicate negated, so the shape it described is the `else` of every `if (withPeriod)`.)
	// ⚠ BALANCE-AND-TURNOVERS CARRIES THE PERIOD TOO. It reports a row PER PERIOD — that is the whole
	// point of it, and the reference shape has `Period` and its coarser projections exactly as the
	// turnovers do. Only a plain BALANCE has none: it is one moment, not an interval cut into units,
	// so there is nothing for a period column to say.
	const bool withPeriod = (shape == ibViewShape::Turnovers
	                      || shape == ibViewShape::BalanceAndTurnovers);

	// ⚠⚠ THE CACHE IS KEYED BY WHAT IT WAS BUILT FROM, not merely by name — and both halves of that
	// rule now live in `ibRegSurfaceCache` (registerQueryLowering.h), because all three surface
	// builders needed them and only the KEY and the COLUMNS differ.
	//
	// The rule it protects, kept here because this is where it was learned: a view is built from the
	// register's dimensions and resources, which are read into the metaobject at LOAD time. Asked once
	// before they are there, a name-keyed cache answers with a table holding nothing but the period
	// columns — for the life of the session. It was not even a stable bug: WHO asks first decides, and
	// declaring the virtual tables' parameters moved the first ask earlier, so a shape that had been
	// right became empty with nothing said about columns changing.
	wxString shapeNow;
	ibRegSignAttribute(shapeNow, GetRegisterPeriod());
	for (const auto dimension : GetDimensionArrayObject()) ibRegSignAttribute(shapeNow, dimension);
	for (const auto resource  : GetResourceArrayObject())  ibRegSignAttribute(shapeNow, resource);

	// One name is one surface here, so the view's name IS the cache key.
	return m_surfaces.Obtain(viewName, shapeNow, viewName, GetMetaData(),
		[&](std::vector<ibTempColumn>& columns, ibMetaID& synthetic)
	{

	if (withPeriod) {
		// ⭐ TWO NAMES, AND THEY ARE NOT THE SAME NAME. The generated table stores `fld1124_D`; a query
		// says `Period`. Handing the storage name out as the column's name put the physical schema
		// straight into the constructor's field list — `fld1124_D_TenDays` beside `Resource1_Receipt`
		// — and made the table impossible to write against by hand.
		const wxString periodName  = GetRegisterPeriod()->GetName();       // what a query writes
		const wxString periodField = ibRegValueField(GetRegisterPeriod()); // what the table keeps
		columns.push_back(ibTempColumn(periodName, periodField,
		                               GetRegisterPeriod()->GetTypeDesc(), synthetic++,
		                               GetRegisterPeriod()->GetSynonym()));

		// The coarser projections the view exposes alongside the stored period. DERIVED from the
		// stored granularity, not listed by hand: the renderer emits exactly the units above it, and
		// a hand-written list would silently drift the moment that granularity changes — naming a
		// column the view does not have is how a reader gets NULLs instead of an error. The suffixes
		// must match the renderer's spelling, because that name IS the contract between the two.
		// The unit vocabulary is ibRegisterUnits() — one table, so the word this column is named after
		// and the word a query writes for the same granularity cannot part company.
		// The NAME loses the underscore (`PeriodWeek` is one word, the way every other field of this
		// language is); the PHYSICAL name keeps it, because that spelling is the contract with the
		// renderer and must not move.
		for (const auto& u : ibRegisterUnits())
			if (u.first > GetTotalsPeriodUnit())
				columns.push_back(ibTempColumn(periodName + u.second,
				                               periodField + wxT("_") + u.second,
				                               GetRegisterPeriod()->GetTypeDesc(), synthetic++));
	}

	// Dimensions keep their METAID as the column id, so a composed read reaches them by
	// Value(dimension) exactly as it would on the movements table — the view is interchangeable
	// with the register as a source, not a parallel vocabulary.
	for (const auto dimension : GetDimensionArrayObject())
		columns.push_back(ibRegAttributeColumn(dimension));

	// The movement arm's own identity — published EXACTLY as a dimension is (its own metaID, its own
	// type), because that is what it is on the source table: a real attribute of the register. Null
	// on every stored row, which is also how a reader tells the two arms apart. Only the turnovers
	// view carries them; a balance view has no arm to distinguish.
	if (withPeriod && HasRecorder() && GetRegisterRecorder() != nullptr && GetRegisterLineNumber() != nullptr) {
		columns.push_back(ibRegAttributeColumn(GetRegisterRecorder()));
		columns.push_back(ibRegAttributeColumn(GetRegisterLineNumber()));
	}

	// Per-resource columns, by shape. A turnover-only register (no record type) has no expense side
	// and no balance to report, so those columns simply do not exist for it.
	const bool withSign = (GetRegisterType() == ibRegisterType::eBalances);
	// `add` takes the SUFFIX and spells both names from it: `Resource1Turnover` for a query to write,
	// `Resource1_Turnover` for the table to keep. One place, so the pair cannot drift.
	// …and a THIRD name with them: the CAPTION. A published figure is read by a person as well as by
	// a query, and the caption used to be built somewhere else entirely (in the manager, per figure,
	// spelled by hand) — so the same number had a presentation through the script door and none at
	// all through the query one. The word comes from the one list that already holds the figures.
	auto add = [&](const ibValueMetaObjectAttributeBase* res, const wxString& suffix) {
		columns.push_back(ibTempColumn(res->GetName() + suffix,
		                               res->GetName() + wxT("_") + suffix,
		                               res->GetTypeDesc(), synthetic++,
		                               ibRegFigureColumnCaption(res->GetSynonym(), ibRegFigureCaption(suffix))));
	};

	for (const auto res : GetResourceArrayObject()) {
		switch (shape) {
			case ibViewShape::Balance:
				if (withSign) add(res, ibRegFigure::Balance);
				break;

			// ⚠ A TURNOVER-ONLY REGISTER HAS NEITHER SIDE. Receipt and expense are what a movement's
			// SIGN means, and a register that keeps no balance has no sign: its records only add up.
			// So it reports the turnover and nothing else — `Receipt` was being offered here always,
			// which put a column on such a register that could never hold anything but the turnover
			// again under a different name.
			case ibViewShape::Turnovers:
				if (withSign) {
					add(res, ibRegFigure::Receipt);
					add(res, ibRegFigure::Expense);
				}
				add(res, ibRegFigure::Turnover);
				break;

			// The symbiosis: the turnover part AND the balance on each side of it. One row per key
			// carrying what entered, what moved, and what remains — assembled from the turnovers
			// surface by conditional sums, not stored anywhere.
			case ibViewShape::BalanceAndTurnovers:
				if (withSign) add(res, ibRegFigure::OpeningBalance);
				add(res, ibRegFigure::Receipt);
				if (withSign) add(res, ibRegFigure::Expense);
				add(res, ibRegFigure::Turnover);
				if (withSign) add(res, ibRegFigure::ClosingBalance);
				break;
		}
	}

	});
}
