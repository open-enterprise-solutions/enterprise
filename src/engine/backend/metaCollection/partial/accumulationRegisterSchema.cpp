////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : accumulation register — the derived TOTALS bundle declaration (L3-2)
////////////////////////////////////////////////////////////////////////////

#include "accumulationRegister.h"

#include "backend/query/schemaSnapshot.h"
#include "backend/query/queryColumn.h"
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

	// The identity comes from the totals METAOBJECT of the active kind — a real metaID, unique by
	// construction, stable across saves. Declaring one kind is what makes the other's table absent,
	// so switching the register kind is a DROP plus a CREATE and never an ALTER of a table that was
	// never there. (See ibValueMetaObjectTotals in accumulationRegister.h for what this replaced.)
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
	if (split) {
		const ibBackendQueryColumn* shard = t.OwnRaw(ibRawDBColumn::Number(ShardColumnName(), totals->GetMetaID()));
		t.Add(shard);
		keyCols.push_back(shard);
	}

	// The key must be UNIQUE: it is what the delta upserts against, and a duplicate would let two
	// rows accumulate half the movements each — totals that are individually plausible and jointly
	// wrong.
	t.Index(totalsName + wxT("_PK"), keyCols, true);

	ibSchemaMaterialize& m = t.Derived(GetQueryable());
	m.Split(split ? kTotalsShardCount : 1u);

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
		const ibMetaID idIn  = res->GetMetaID();
		const ibMetaID idOut = res->GetMetaID() | 0x40000000;

		if (!withSign) {
			// No record type — nothing signs a movement, so there is no expense side to keep apart.
			const ibBackendQueryColumn* c = t.OwnRaw(ibRawDBColumn::Number(inName, idIn));
			t.Add(c);
			m.Accumulate(c, wxT("{row}.") + resField, ibQueryColumnExpr::Col(res));
			pairs.push_back({ inName, wxString(), res->GetName() });
			continue;
		}

		const ibBackendQueryColumn* cIn  = t.OwnRaw(ibRawDBColumn::Number(inName,  idIn));
		const ibBackendQueryColumn* cOut = t.OwnRaw(ibRawDBColumn::Number(outName, idOut));
		t.Add(cIn);
		t.Add(cOut);

		// The record type splits one movement into the two sides. The accumulate stays
		// UNCONDITIONAL — only the VALUE branches — so the trigger needs no procedural IF and the
		// delta template stays the same one every engine uses.
		const wxString recField = ibRegValueField(GetRegisterRecordType());
		m.Accumulate(cIn,  wxT("CASE WHEN {row}.") + recField + wxT(" = 0 THEN {row}.") + resField + wxT(" ELSE 0 END"),
			ibQueryColumnExpr::Case(
				{ { ibQueryPredicate::Leaf(ibQueryCondition{ GetRegisterRecordType(), ibQueryFilterOp::Equal, ibValue(0.0) }),
				    ibQueryColumnExpr::Col(res) } },
				ibQueryColumnExpr::Const(ibValue(0.0))));
		m.Accumulate(cOut, wxT("CASE WHEN {row}.") + recField + wxT(" = 0 THEN 0 ELSE {row}.") + resField + wxT(" END"),
			ibQueryColumnExpr::Case(
				{ { ibQueryPredicate::Leaf(ibQueryCondition{ GetRegisterRecordType(), ibQueryFilterOp::Equal, ibValue(0.0) }),
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
	{
		std::vector<const ibBackendQueryColumn*> sourceColumns = t.m_scaffold;
		for (const ibSchemaColumn& c : t.m_columns)
			sourceColumns.push_back(c.m_column);
		t.SelfSource(std::make_shared<ibSchemaTableQueryable>(
			t.m_name, t.m_id, std::move(sourceColumns), GetMetaData()));
	}

	// --- the read views, composed from L2-2 primitives ------------------------------------------
	// TURNOVERS — per period: what came in, what went out, and the net.
	{
		ibMaterializeView& v = m.View(GetTurnoverViewName(), /*withPeriod*/ true);
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
	const bool withPeriod = (shape == ibViewShape::Turnovers);

	const auto cached = m_viewSources.find(viewName);
	if (cached != m_viewSources.end())
		return cached->second.get();

	std::vector<ibTempColumn> columns;
	ibMetaID synthetic = 0x50000000u;   // the view's own column ids — a band clear of any metaID

	if (withPeriod) {
		const wxString periodName = ibRegValueField(GetRegisterPeriod());
		columns.push_back(ibTempColumn(periodName, GetRegisterPeriod()->GetTypeDesc(), synthetic++));

		// The coarser projections the view exposes alongside the stored period. DERIVED from the
		// stored granularity, not listed by hand: the renderer emits exactly the units above it, and
		// a hand-written list would silently drift the moment that granularity changes — naming a
		// column the view does not have is how a reader gets NULLs instead of an error. The suffixes
		// must match the renderer's spelling, because that name IS the contract between the two.
		static const std::pair<ibTotalsPeriod, const wxChar*> kUnits[] = {
			{ ibTotalsPeriod::Second,   wxT("Second")   }, { ibTotalsPeriod::Minute,   wxT("Minute")   },
			{ ibTotalsPeriod::Hour,     wxT("Hour")     }, { ibTotalsPeriod::Day,      wxT("Day")      },
			{ ibTotalsPeriod::Week,     wxT("Week")     }, { ibTotalsPeriod::TenDays,  wxT("TenDays")  },
			{ ibTotalsPeriod::Month,    wxT("Month")    }, { ibTotalsPeriod::Quarter,  wxT("Quarter")  },
			{ ibTotalsPeriod::HalfYear, wxT("HalfYear") }, { ibTotalsPeriod::Year,     wxT("Year")     },
		};
		for (const auto& u : kUnits)
			if (u.first > GetTotalsPeriodUnit())
				columns.push_back(ibTempColumn(periodName + wxT("_") + u.second,
				                               GetRegisterPeriod()->GetTypeDesc(), synthetic++));
	}

	// Dimensions keep their METAID as the column id, so a composed read reaches them by
	// Value(dimension) exactly as it would on the movements table — the view is interchangeable
	// with the register as a source, not a parallel vocabulary.
	for (const auto dimension : GetDimensionArrayObject())
		columns.push_back(ibTempColumn(ibRegValueField(dimension), dimension->GetTypeDesc(), dimension->GetMetaID()));

	// Per-resource columns, by shape. A turnover-only register (no record type) has no expense side
	// and no balance to report, so those columns simply do not exist for it.
	const bool withSign = (GetRegisterType() == ibRegisterType::eBalances);
	auto add = [&](const wxString& name, const ibValueMetaObjectAttributeBase* res) {
		columns.push_back(ibTempColumn(name, res->GetTypeDesc(), synthetic++));
	};

	for (const auto res : GetResourceArrayObject()) {
		const wxString base = res->GetName();
		switch (shape) {
			case ibViewShape::Balance:
				if (withSign) add(base + wxT("_Balance"), res);
				break;

			case ibViewShape::Turnovers:
				add(base + wxT("_Receipt"), res);
				if (withSign) add(base + wxT("_Expense"), res);
				add(base + wxT("_Turnover"), res);
				break;

			// The symbiosis: the turnover part AND the balance on each side of it. One row per key
			// carrying what entered, what moved, and what remains — assembled from the turnovers
			// surface by conditional sums, not stored anywhere.
			case ibViewShape::BalanceAndTurnovers:
				if (withSign) add(base + wxT("_OpeningBalance"), res);
				add(base + wxT("_Receipt"), res);
				if (withSign) add(base + wxT("_Expense"), res);
				add(base + wxT("_Turnover"), res);
				if (withSign) add(base + wxT("_ClosingBalance"), res);
				break;
		}
	}

	auto q = std::make_unique<ibDbTempTableQueryable>(viewName, std::move(columns), GetMetaData());
	const ibBackendQueryable* raw = q.get();
	m_viewSources.emplace(viewName, std::move(q));
	return raw;
}
