// L3 composer — RAM composition cores + co-location ROUTING gates (docs §22).
//
// PURE: no database, no appData. Two layers:
//   1. The RAM cores ibQueryComposer exposes (JoinRamTables / AppendUnionBranch) — the
//      nested-loop join and the heterogeneous union stack, asserted on exact cell values.
//   2. The routing GATES (CanColocateJoin / CanColocateUnion / CanColocateAggregate) — given a
//      query SHAPE, do they correctly decide "run server-side" vs "fall back to RAM"? Driven
//      by a minimal mock queryable (raw columns -> scalar / single-field-joinable, no metadata).
//
// Together: if the cores fold correctly AND the gates route correctly, the L3 engine's
// composition + push-down decisions are proven without a database. (The actual server-side SQL
// EXECUTION + the temp round-trip are integration scope — a real SQLite/PG connection.)

#include <gtest/gtest.h>

#include <memory>
#include <vector>
#include <algorithm>   // stable_sort — RamSortCompareKey null-placement test
#include <functional>  // std::function — the recording computed queryable's row builder

#include "backend/query/queryProvider.h"     // ibQueryComposer + ibQueryRamTable
#include "backend/query/querySelector.h"     // ibSelector — traversal façade over a snapshot
#include "backend/query/dbTableProvider.h"   // ibDbTableProvider::CanColocate* (the gates)
#include "backend/query/dataQueryBuilder.h"  // ibDataQuerySpec / ibQueryNode / AggregateItem
#include "backend/query/queryable.h"         // ibBackendQueryable / ibQueryCondition
#include "backend/query/queryColumn.h"       // ibBackendQueryColumn / ibBackendColumnRawDB
#include "backend/clsid.h"                    // reference_to_clsid — a reference-typed column (multi-field spread)

namespace {

// Minimal RAM-core column — name + model id (the row read key). No metaobject. (As in test_queryTotals.)
class TestCol : public ibBackendQueryColumn {
public:
	TestCol(const wxString& name, ibMetaID id) : m_name(name), m_id(id) {}
	wxString           GetName()         const override { return m_name; }
	wxString           GetPhysicalName() const override { return m_name; }
	ibTypeDescription& GetTypeDesc()     const override { return m_type; }
	ibMetaID           GetColumnId()      const override { return m_id; }
private:
	wxString                  m_name;
	ibMetaID                  m_id;
	mutable ibTypeDescription m_type;
};

const ibTypeDescription kNoType;

// A minimal queryable for the routing gates: a named table over raw columns (raw -> scalar +
// single-field-joinable, so no type-descriptor / metadata setup is needed). Ownership is by
// pointer (each column belongs to exactly its table). Optionally COMPUTED (RAM) for the
// "computed leaf falls back to RAM" cases.
class TestQueryable : public ibBackendQueryable {
public:
	TestQueryable(const wxString& table, ibMetaID metaId, bool computed = false)
		: m_table(table), m_metaId(metaId), m_computed(computed) {}
	void AddCol(const ibBackendQueryColumn* c) { m_cols.push_back(c); }
	// ROW IDENTITY — what the phantom level groups by (a real source answers with its reference, or a
	// register with recorder + line number). Absent by default: a source that has none.
	void SetKey(const ibBackendQueryColumn* c) { m_keys.push_back(c); }

	wxString GetQueryTableName() const override { return m_table; }
	ibMetaID GetQueryTableId()    const override { return m_metaId; }
	// New pure-virtual on ibBackendQueryable (queryable.h) the test source had not
	// caught up with — abstract-class drift, not a logic change. Synthesise a
	// stable, table-distinct guid from the id so identity/colocation logic still
	// tells the two tables apart.
	ibGuid   GetQueryTableGuid()  const override {
		ibGuidImpl impl{};
		impl.m_data1 = static_cast<unsigned long>(m_metaId);
		return ibGuid(impl);
	}
	bool     IsComputedInRam()   const override { return m_computed; }
	const ibMetaData* GetMetaData() const override { return nullptr; }
	std::vector<const ibBackendQueryColumn*> GetColumns() const override { return m_cols; }
	const ibBackendQueryColumn* ResolveColumnByName(const wxString& name) const override {
		for (const ibBackendQueryColumn* c : m_cols) if (c->GetName() == name) return c;
		return nullptr;
	}
	bool OwnsColumn(const ibBackendQueryColumn* col) const override {
		for (const ibBackendQueryColumn* c : m_cols) if (c == col) return true;
		return false;
	}
	std::vector<const ibBackendQueryColumn*> GetPrimaryKeyColumns() const override { return m_keys; }
private:
	wxString m_table;
	ibMetaID m_metaId;
	bool     m_computed;
	std::vector<const ibBackendQueryColumn*> m_cols;
	std::vector<const ibBackendQueryColumn*> m_keys;
};

// A computed (RAM) queryable that RECORDS what the composer pushed into its ComputeRows, and then
// deliberately IGNORES it — exactly like the real register computes (balance / turnover / slice), which
// take the parameter and drop it. Two things ride on that:
//
//   * OBSERVABILITY. The semi-join key reduction is a PURE reduction — it can never change the answer —
//     so results alone cannot tell whether it fired. Reading what was pushed is the only way to see it.
//   * The ADVISORY contract. Honouring `extra` inside ComputeRows is an optimisation; ibComputedProvider
//     applies the conditions over the returned rows regardless. A source that ignores them (this one, and
//     every register today) still comes back correctly filtered.
class RecordingComputedQ : public ibBackendQueryable {
public:
	RecordingComputedQ(const wxString& table, ibMetaID metaId) : m_table(table), m_metaId(metaId) {}
	void AddCol(const ibBackendQueryColumn* c) { m_cols.push_back(c); }
	void SetRows(std::function<ibQueryRamTable()> b) { m_build = std::move(b); }
	const std::vector<ibQueryCondition>& Seen() const { return m_seen; }

	ibBackendQueryProvider& GetProvider() const override { return ibComputedProviderInstance(); }
	bool     IsComputedInRam() const override { return true; }
	ibQueryRamTable ComputeRows(const std::vector<ibQueryCondition>& extra) const override {
		m_seen = extra;                                  // record, then ignore — see the note above
		return m_build ? m_build() : ibQueryRamTable();
	}

	wxString GetQueryTableName() const override { return m_table; }
	ibMetaID GetQueryTableId()   const override { return m_metaId; }
	ibGuid   GetQueryTableGuid() const override {
		ibGuidImpl impl{}; impl.m_data1 = static_cast<unsigned long>(m_metaId); return ibGuid(impl);
	}
	const ibMetaData* GetMetaData() const override { return nullptr; }
	std::vector<const ibBackendQueryColumn*> GetColumns() const override { return m_cols; }
	const ibBackendQueryColumn* ResolveColumnByName(const wxString& name) const override {
		for (const ibBackendQueryColumn* c : m_cols) if (c->GetName() == name) return c;
		return nullptr;
	}
	bool OwnsColumn(const ibBackendQueryColumn* col) const override {
		for (const ibBackendQueryColumn* c : m_cols) if (c == col) return true;
		return false;
	}
private:
	wxString m_table;
	ibMetaID m_metaId;
	std::vector<const ibBackendQueryColumn*> m_cols;
	std::function<ibQueryRamTable()>         m_build;
	mutable std::vector<ibQueryCondition>    m_seen;
};

// One-column key table (model id `id`) holding `keys` — the driving side of the reductions below.
ibQueryRamTable KeyRows(ibMetaID id, const wxString& name, const std::vector<long>& keys)
{
	ibQueryRamTable t;
	t.AddColumn(id, name, kNoType);
	for (const long k : keys) {
		const long r = t.AppendRow();
		t.SetCell(r, id, ibValue(ibNumber(k)));
	}
	return t;
}

// Fill an ibDataQuerySpec with all the (empty) side-vectors + a root + select list. The vectors
// must outlive the spec use, so the caller owns them.
struct SpecBuf {
	std::vector<ibQueryCondition> conds;
	std::vector<ibValue> keyIn;
	std::vector<ibQuerySortItem> sorts;
	std::vector<const ibBackendQueryColumn*> groupBy;
	std::vector<ibDataQueryBuilder::AggregateItem> aggs;
	std::vector<ibDataQueryBuilder::HavingItem> having;
	// A write is a SET of rows now (one row is the degenerate case), so the spec carries a vector
	// of assignment lists and never an empty one — readers say front() without testing first.
	std::vector<ibWriteRow> writeRows{ 1 };
	std::vector<ibDotWalkColumn> dots;
	std::vector<std::pair<const ibBackendQueryColumn*, wxString>> sel;
	std::vector<std::vector<const ibBackendQueryColumn*>> groupPaths;   // parallel to groupBy (dot-walk keys)
	std::vector<ibDotWalkColumn> dimWalks;                              // TotalByDotWalk dimensions
	// The TOTALS door's own two lists. A door that folds hierarchical totals fills THESE and leaves
	// groupBy / aggs empty — which is exactly what the gate used to be blind to.
	std::vector<ibTotalLevel> totals;
	std::vector<ibDataQueryBuilder::AggregateItem> totalAggs;

	ibDataQuerySpec Make(const ibQueryNode* root, const ibBackendQueryable* primary) {
		ibDataQuerySpec s;
		s.m_root = root; s.m_queryable = primary;
		s.m_conditions = &conds; s.m_keyIn = &keyIn; s.m_sorts = &sorts;
		s.m_groupBy = &groupBy; s.m_aggregates = &aggs; s.m_having = &having;
		s.m_writeRows = &writeRows; s.m_dotWalks = &dots; s.m_selectCols = &sel;
		s.m_groupPaths = &groupPaths; s.m_dimWalks = &dimWalks;
		s.m_totals = &totals; s.m_totalAggregates = &totalAggs;
		return s;
	}
};

std::shared_ptr<ibQueryNode> Join2(const ibBackendQueryable* l, const ibBackendQueryable* r,
                                   const ibBackendQueryColumn* onL, const ibBackendQueryColumn* onR)
{
	auto root = std::make_shared<ibQueryNode>();
	root->m_kind = ibQueryNode::Kind::Join;
	root->m_left = ibQueryNode::Source(l); root->m_right = ibQueryNode::Source(r);
	root->m_on.m_colL = onL; root->m_on.m_colR = onR;
	return root;
}

} // namespace

// ===========================================================================
// RAM cores — JoinRamTables (nested-loop inner join, keyed by model id)
// ===========================================================================

TEST(QueryComposerCore, JoinRamTables_MatchesAndCardinality)
{
	const ibMetaID CUST_ID = 1, CUST_CODE = 2, ORD_ID = 3, ORD_CUST = 4;
	TestCol custId(wxT("custId"), CUST_ID), custCode(wxT("custCode"), CUST_CODE);
	TestCol ordId(wxT("ordId"), ORD_ID),     ordCust(wxT("ordCust"), ORD_CUST);

	ibQueryRamTable left;   // customers
	left.AddColumn(CUST_ID, wxT("custId"), kNoType);
	left.AddColumn(CUST_CODE, wxT("custCode"), kNoType);
	auto cust = [&](long id, const wxString& code) {
		const long r = left.AppendRow();
		left.SetCell(r, CUST_ID, ibValue(ibNumber(id)));
		left.SetCell(r, CUST_CODE, ibValue(code));
	};
	cust(1, wxT("A")); cust(2, wxT("B"));

	ibQueryRamTable right;  // orders -> customer
	right.AddColumn(ORD_ID, wxT("ordId"), kNoType);
	right.AddColumn(ORD_CUST, wxT("ordCust"), kNoType);
	auto ord = [&](long id, long cust) {
		const long r = right.AppendRow();
		right.SetCell(r, ORD_ID, ibValue(ibNumber(id)));
		right.SetCell(r, ORD_CUST, ibValue(ibNumber(cust)));
	};
	ord(10, 1); ord(11, 1); ord(12, 2); ord(13, 99);   // 99 = no matching customer

	const std::vector<const ibBackendQueryColumn*> outCols = { &custCode, &ordId };
	const std::vector<bool> fromLeft = { true, false };
	const ibQueryRamTable out = ibQueryComposer::JoinRamTables(left, right, &custId, &ordCust, outCols, fromLeft);

	ASSERT_EQ(out.RowCount(), 3);   // (A,10) (A,11) (B,12); ord 13 dropped
	EXPECT_EQ(out.GetCell(0, CUST_CODE).GetString().ToStdString(), "A");
	EXPECT_EQ(out.GetCell(0, ORD_ID).GetString().ToStdString(), "10");
	EXPECT_EQ(out.GetCell(2, CUST_CODE).GetString().ToStdString(), "B");
	EXPECT_EQ(out.GetCell(2, ORD_ID).GetString().ToStdString(), "12");
}

// ===========================================================================
// RAM cores — AppendUnionBranch (heterogeneous stack; absent column -> NULL)
// ===========================================================================

TEST(QueryComposerCore, AppendUnionBranch_MissingColumnIsNull)
{
	const ibMetaID CODE = 1, NAME = 2;
	TestCol code(wxT("code"), CODE), name(wxT("name"), NAME);

	ibQueryRamTable out;
	out.AddColumn(CODE, wxT("code"), kNoType);
	out.AddColumn(NAME, wxT("name"), kNoType);

	ibQueryRamTable branch;   // has code, NOT name
	branch.AddColumn(CODE, wxT("code"), kNoType);
	const long br = branch.AppendRow();
	branch.SetCell(br, CODE, ibValue(wxString(wxT("X"))));

	// outCols [code, name]; this branch supplies [code, <absent>].
	ibQueryComposer::AppendUnionBranch(out, branch, { &code, &name }, { &code, nullptr });

	ASSERT_EQ(out.RowCount(), 1);
	EXPECT_EQ(out.GetCell(0, CODE).GetString().ToStdString(), "X");
	EXPECT_TRUE(out.GetCell(0, NAME).GetString().IsEmpty());   // absent in this branch -> empty cell
}

// ===========================================================================
// CROSS JOIN (ON TRUE) — JoinRamTables with a NULL key = cartesian product
// ===========================================================================

TEST(QueryComposerCore, JoinRamTables_NullKeyIsCartesian)
{
	const ibMetaID A = 1, B = 2;
	TestCol a(wxT("a"), A), b(wxT("b"), B);

	ibQueryRamTable left;  left.AddColumn(A, wxT("a"), kNoType);
	for (long v : { 1, 2 }) { const long r = left.AppendRow(); left.SetCell(r, A, ibValue(ibNumber(v))); }

	ibQueryRamTable right; right.AddColumn(B, wxT("b"), kNoType);
	for (long v : { 10, 20, 30 }) { const long r = right.AppendRow(); right.SetCell(r, B, ibValue(ibNumber(v))); }

	// null join keys -> every left row pairs with every right row (2 x 3 = 6).
	const ibQueryRamTable out = ibQueryComposer::JoinRamTables(left, right, nullptr, nullptr, { &a, &b }, { true, false });
	EXPECT_EQ(out.RowCount(), 6);
}

TEST(QueryComposerCore, JoinRamTables_OuterKinds)
{
	const ibMetaID LK = 1, LV = 2, RK = 4, RV = 3;
	TestCol lk(wxT("lk"), LK), lv(wxT("lv"), LV), rk(wxT("rk"), RK), rv(wxT("rv"), RV);

	ibQueryRamTable left;  left.AddColumn(LK, wxT("lk"), kNoType); left.AddColumn(LV, wxT("lv"), kNoType);
	auto L = [&](long k, long v) { const long r = left.AppendRow();  left.SetCell(r, LK, ibValue(ibNumber(k))); left.SetCell(r, LV, ibValue(ibNumber(v))); };
	L(1, 11); L(2, 12); L(3, 13);                       // keys 1,2,3

	ibQueryRamTable right; right.AddColumn(RK, wxT("rk"), kNoType); right.AddColumn(RV, wxT("rv"), kNoType);
	auto R = [&](long k, long v) { const long r = right.AppendRow(); right.SetCell(r, RK, ibValue(ibNumber(k))); right.SetCell(r, RV, ibValue(ibNumber(v))); };
	R(2, 22); R(3, 23); R(4, 24);                       // keys 2,3,4  -> matched 2,3 ; left-only 1 ; right-only 4

	const std::vector<const ibBackendQueryColumn*> outc = { &lv, &rv };
	const std::vector<bool> side = { true, false };
	using JK = ibQueryJoinKind;
	EXPECT_EQ(ibQueryComposer::JoinRamTables(left, right, &lk, &rk, outc, side, JK::Inner).RowCount(), 2);   // 2,3
	EXPECT_EQ(ibQueryComposer::JoinRamTables(left, right, &lk, &rk, outc, side, JK::Left ).RowCount(), 3);   // + left-only 1
	EXPECT_EQ(ibQueryComposer::JoinRamTables(left, right, &lk, &rk, outc, side, JK::Right).RowCount(), 3);   // + right-only 4
	EXPECT_EQ(ibQueryComposer::JoinRamTables(left, right, &lk, &rk, outc, side, JK::Full ).RowCount(), 4);   // + both
}

// A NULL-VALUED join key (key column present, but the cell is unset/empty) is distinct from a
// keyless cross (null key COLUMN). SQL `NULL = NULL` is UNKNOWN -> such rows never match: INNER
// drops them, an OUTER join keeps them unmatched on their OWN side (never paired with the other
// side's NULL). Before the fix the empty hash key paired NULL-left with NULL-right.
TEST(QueryComposerCore, JoinRamTables_NullKeyValueDoesNotMatch)
{
	const ibMetaID LK = 1, LV = 2, RK = 4, RV = 3;
	TestCol lk(wxT("lk"), LK), lv(wxT("lv"), LV), rk(wxT("rk"), RK), rv(wxT("rv"), RV);

	ibQueryRamTable left;  left.AddColumn(LK, wxT("lk"), kNoType); left.AddColumn(LV, wxT("lv"), kNoType);
	auto L = [&](long k, long v, bool kNull = false) {
		const long r = left.AppendRow();
		left.SetCell(r, LK, kNull ? ibValue(ibValueTypes::TYPE_NULL) : ibValue(ibNumber(k)));   // SQL NULL key
		left.SetCell(r, LV, ibValue(ibNumber(v)));
	};
	L(1, 11); L(0, 99, /*null key*/ true); L(2, 12);

	ibQueryRamTable right; right.AddColumn(RK, wxT("rk"), kNoType); right.AddColumn(RV, wxT("rv"), kNoType);
	auto R = [&](long k, long v, bool kNull = false) {
		const long r = right.AppendRow();
		right.SetCell(r, RK, kNull ? ibValue(ibValueTypes::TYPE_NULL) : ibValue(ibNumber(k)));   // SQL NULL key
		right.SetCell(r, RV, ibValue(ibNumber(v)));
	};
	R(1, 21); R(0, 98, /*null key*/ true); R(2, 22);

	const std::vector<const ibBackendQueryColumn*> outc = { &lv, &rv };
	const std::vector<bool> side = { true, false };
	using JK = ibQueryJoinKind;
	EXPECT_EQ(ibQueryComposer::JoinRamTables(left, right, &lk, &rk, outc, side, JK::Inner).RowCount(), 2);   // 1-1, 2-2; NULLs unpaired
	EXPECT_EQ(ibQueryComposer::JoinRamTables(left, right, &lk, &rk, outc, side, JK::Left ).RowCount(), 3);   // + NULL-key left, unmatched
	EXPECT_EQ(ibQueryComposer::JoinRamTables(left, right, &lk, &rk, outc, side, JK::Right).RowCount(), 3);   // + NULL-key right, unmatched
	EXPECT_EQ(ibQueryComposer::JoinRamTables(left, right, &lk, &rk, outc, side, JK::Full ).RowCount(), 4);   // matched 2 + both NULLs
}

// ===========================================================================
// THETA JOIN — JoinRamTables with a NON-EQUI op (ibJoinOn::m_op): a nested loop
// over every (L,R) pair compared by the op; a NULL operand never matches. The
// equi tests above never exercise this branch (the D feature).
// ===========================================================================

TEST(QueryComposerCore, JoinRamTables_ThetaNonEqui)
{
	const ibMetaID LVAL = 1, RTHR = 2;
	TestCol lval(wxT("lval"), LVAL), rthr(wxT("rthr"), RTHR);

	ibQueryRamTable left;  left.AddColumn(LVAL, wxT("lval"), kNoType);
	for (long v : { 10, 20, 30 }) { const long r = left.AppendRow();  left.SetCell(r, LVAL, ibValue(ibNumber(v))); }
	ibQueryRamTable right; right.AddColumn(RTHR, wxT("rthr"), kNoType);
	for (long v : { 15, 25 })     { const long r = right.AppendRow(); right.SetCell(r, RTHR, ibValue(ibNumber(v))); }

	const std::vector<const ibBackendQueryColumn*> outc = { &lval, &rthr };
	const std::vector<bool> side = { true, false };
	auto theta = [&](ibJoinCompareOp op) {
		ibJoinOn on; on.m_op = op;
		return ibQueryComposer::JoinRamTables(left, right, &lval, &rthr, outc, side, ibQueryJoinKind::Inner, on).RowCount();
	};
	EXPECT_EQ(theta(ibJoinCompareOp::Gt), 3);   // 20>15 | 30>15 | 30>25
	EXPECT_EQ(theta(ibJoinCompareOp::Ge), 3);   // same (no L equals a threshold)
	EXPECT_EQ(theta(ibJoinCompareOp::Lt), 3);   // 10<15 | 10<25 | 20<25
	EXPECT_EQ(theta(ibJoinCompareOp::Ne), 6);   // all distinct -> every pair (3x2)
}

TEST(QueryComposerCore, JoinRamTables_ThetaOuterKeepsUnmatched)
{
	const ibMetaID LVAL = 1, RTHR = 2;
	TestCol lval(wxT("lval"), LVAL), rthr(wxT("rthr"), RTHR);
	ibQueryRamTable left;  left.AddColumn(LVAL, wxT("lval"), kNoType);
	for (long v : { 5, 30 })  { const long r = left.AppendRow();  left.SetCell(r, LVAL, ibValue(ibNumber(v))); }   // 5 > nothing
	ibQueryRamTable right; right.AddColumn(RTHR, wxT("rthr"), kNoType);
	for (long v : { 15, 25 }) { const long r = right.AppendRow(); right.SetCell(r, RTHR, ibValue(ibNumber(v))); }

	const std::vector<const ibBackendQueryColumn*> outc = { &lval, &rthr };
	const std::vector<bool> side = { true, false };
	ibJoinOn gt; gt.m_op = ibJoinCompareOp::Gt;
	EXPECT_EQ(ibQueryComposer::JoinRamTables(left, right, &lval, &rthr, outc, side, ibQueryJoinKind::Inner, gt).RowCount(), 2);  // (30,15)(30,25)
	EXPECT_EQ(ibQueryComposer::JoinRamTables(left, right, &lval, &rthr, outc, side, ibQueryJoinKind::Left,  gt).RowCount(), 3);  // + unmatched 5
}

// ===========================================================================
// RAM DISTINCT — DedupeRows folds duplicate output rows by cell identity
// (first occurrence wins, order preserved). The #6 SELECT DISTINCT core.
// ===========================================================================

TEST(QueryComposerCore, DedupeRows_FirstOfEachInOrder)
{
	const ibMetaID REGION = 1;
	TestCol region(wxT("region"), REGION);
	ibQueryRamTable t; t.AddColumn(REGION, wxT("region"), kNoType);
	for (const wxChar* v : { wxT("North"), wxT("South"), wxT("North"), wxT("North"), wxT("South") }) {
		const long r = t.AppendRow(); t.SetCell(r, REGION, ibValue(wxString(v)));
	}
	const ibQueryRamTable out = ibQueryComposer::DedupeRows(t, { &region });
	ASSERT_EQ(out.RowCount(), 2);
	EXPECT_EQ(out.GetCell(0, REGION).GetString().ToStdString(), "North");
	EXPECT_EQ(out.GetCell(1, REGION).GetString().ToStdString(), "South");
}

// ===========================================================================
// RAM ORDER BY — RamSortCompareKey (NULL = smallest: NULLS FIRST asc / NULLS LAST desc)
// ===========================================================================

TEST(QueryComposerCore, RamSortCompareKey_NullOrdering)
{
	const ibValue nul(ibValueTypes::TYPE_NULL);                   // SQL NULL
	const ibValue a(wxString(wxT("A"))), b(wxString(wxT("B")));

	// non-NULL total order, both directions; equal keys fall through (0)
	EXPECT_LT(ibQueryComposer::RamSortCompareKey(a, b, true ), 0);   // A before B (asc)
	EXPECT_GT(ibQueryComposer::RamSortCompareKey(a, b, false), 0);   // B before A (desc)
	EXPECT_EQ(ibQueryComposer::RamSortCompareKey(a, a, true ), 0);   // equal -> next key

	// NULL is the smallest value
	EXPECT_LT(ibQueryComposer::RamSortCompareKey(nul, a, true ), 0); // ASC -> NULL first
	EXPECT_GT(ibQueryComposer::RamSortCompareKey(a, nul, true ), 0);
	EXPECT_GT(ibQueryComposer::RamSortCompareKey(nul, a, false), 0); // DESC -> NULL last
	EXPECT_LT(ibQueryComposer::RamSortCompareKey(a, nul, false), 0);

	EXPECT_EQ(ibQueryComposer::RamSortCompareKey(nul, nul, true ), 0); // both NULL -> equal
	EXPECT_EQ(ibQueryComposer::RamSortCompareKey(nul, nul, false), 0);
}

// Drive a stable_sort with the comparator: NULLs cluster first (ASC) / last (DESC) deterministically,
// instead of floating at their input position (the pre-fix behaviour, where operator< returned false
// both ways for a NULL so it compared "equal" to every non-NULL).
TEST(QueryComposerCore, RamSortCompareKey_StableSortPlacesNulls)
{
	const ibValue N(ibValueTypes::TYPE_NULL);
	const std::vector<ibValue> v = { ibValue(wxString(wxT("B"))), N, ibValue(wxString(wxT("A"))), N };

	std::vector<ibValue> asc = v;
	std::stable_sort(asc.begin(), asc.end(),
		[](const ibValue& x, const ibValue& y) { return ibQueryComposer::RamSortCompareKey(x, y, true) < 0; });
	EXPECT_EQ(asc[0].GetType(), ibValueTypes::TYPE_NULL);         // NULLs first
	EXPECT_EQ(asc[1].GetType(), ibValueTypes::TYPE_NULL);
	EXPECT_EQ(asc[2].GetString().ToStdString(), "A");
	EXPECT_EQ(asc[3].GetString().ToStdString(), "B");

	std::vector<ibValue> desc = v;
	std::stable_sort(desc.begin(), desc.end(),
		[](const ibValue& x, const ibValue& y) { return ibQueryComposer::RamSortCompareKey(x, y, false) < 0; });
	EXPECT_EQ(desc[0].GetString().ToStdString(), "B");           // NULLs last
	EXPECT_EQ(desc[1].GetString().ToStdString(), "A");
	EXPECT_EQ(desc[2].GetType(), ibValueTypes::TYPE_NULL);
	EXPECT_EQ(desc[3].GetType(), ibValueTypes::TYPE_NULL);
}

// Total-order decision (deliberate): a value with NO scalar payload — TYPE_NULL or TYPE_EMPTY
// (Undefined) — is the SMALLEST, so operator< is a valid total order for std::sort / set / map.
// Consequence: `Undefined < 5` is true. That is ORDERING, not SQL-null logic — the three-valued
// flag governs NULL comparison in filters; this bare operator< must stay totally ordered.
TEST(QueryComposerCore, ValueOrder_NoValueIsSmallest) {
	const ibValue five(ibNumber(5)), nul(ibValueTypes::TYPE_NULL), undef;
	EXPECT_TRUE (undef < five);   // Undefined sorts before a real value
	EXPECT_FALSE(five  < undef);
	EXPECT_TRUE (nul   < five);   // NULL sorts before a real value
	EXPECT_FALSE(five  < nul);
	EXPECT_FALSE(undef < nul);    // both "no value" -> equal-order (neither is < the other)
	EXPECT_FALSE(nul   < undef);
}

// ===========================================================================
// RAM post-filter — FilterRows (boolean WHERE TREE over a composed JOIN: OR / NOT / IS NULL)
// ===========================================================================

TEST(QueryComposerCore, FilterRows_OrIsNullNot)
{
	const ibMetaID REGION = 1, QTY = 2;
	TestCol region(wxT("region"), REGION), qty(wxT("qty"), QTY);

	ibQueryRamTable t;
	t.AddColumn(REGION, wxT("region"), kNoType);
	t.AddColumn(QTY,    wxT("qty"),    kNoType);
	auto row = [&](const wxString& reg, long q, bool regNull = false) {
		const long r = t.AppendRow();
		t.SetCell(r, REGION, regNull ? ibValue(ibValueTypes::TYPE_NULL) : ibValue(reg));   // SQL NULL
		t.SetCell(r, QTY, ibValue(ibNumber(q)));
	};
	row(wxT("North"), 10); row(wxT("South"), 5); row(wxT("East"), 7); row(wxT(""), 3, /*null*/ true);

	auto eq = [&](const ibBackendQueryColumn* c, const wxString& v) {
		ibQueryCondition cond; cond.m_col = c; cond.m_value = ibValue(v); return ibQueryPredicate::Leaf(cond);
	};

	// region = 'North' OR region = 'South'
	const ibQueryRamTable f1 = ibQueryComposer::FilterRows(
		t, ibQueryPredicate::Compose(ibQueryPredicateKind::Or, eq(&region, wxT("North")), eq(&region, wxT("South"))).get());
	ASSERT_EQ(f1.RowCount(), 2);
	EXPECT_EQ(f1.GetCell(0, REGION).GetString().ToStdString(), "North");
	EXPECT_EQ(f1.GetCell(1, REGION).GetString().ToStdString(), "South");

	// region IS NULL  -> only the unset-region row
	const ibQueryRamTable f2 = ibQueryComposer::FilterRows(t, ibQueryPredicate::Null(&region, /*negated*/ false).get());
	ASSERT_EQ(f2.RowCount(), 1);
	EXPECT_EQ(f2.GetCell(0, QTY).GetString().ToStdString(), "3");

	// NOT (region = 'North')  -> South, East. The null-region row is UNKNOWN under SQL
	// three-valued logic (NULL = 'North' is unknown, NOT unknown is unknown) -> dropped.
	const ibQueryRamTable f3 = ibQueryComposer::FilterRows(t, ibQueryPredicate::Not(eq(&region, wxT("North"))).get());
	EXPECT_EQ(f3.RowCount(), 2);
}

// ===========================================================================
// Computed column over a composed JOIN — EvalColumnExpr (arithmetic + CASE)
// ===========================================================================

TEST(QueryComposerCore, EvalColumnExpr_ArithAndCase)
{
	const ibMetaID QTY = 1, PRICE = 2;
	TestCol qty(wxT("qty"), QTY), price(wxT("price"), PRICE);

	ibQueryRamTable t;
	t.AddColumn(QTY,   wxT("qty"),   kNoType);
	t.AddColumn(PRICE, wxT("price"), kNoType);
	const long r = t.AppendRow();
	t.SetCell(r, QTY,   ibValue(ibNumber(4)));
	t.SetCell(r, PRICE, ibValue(ibNumber(5)));

	// qty * price = 20
	const ibQueryColumnExprPtr mul = ibQueryColumnExpr::Arith(
		ibQueryColumnArithOp::Mul, ibQueryColumnExpr::Col(&qty), ibQueryColumnExpr::Col(&price));
	EXPECT_EQ(ibQueryComposer::EvalColumnExpr(mul.get(), t, r).GetString().ToStdString(), "20");

	// CASE WHEN qty > 3 THEN 'bulk' ELSE 'unit' END -> 'bulk'
	ibQueryCondition gt; gt.m_col = &qty; gt.m_op = ibQueryFilterOp::Greater; gt.m_value = ibValue(ibNumber(3));
	auto caseE = std::make_shared<ibQueryColumnExpr>();
	caseE->m_kind  = ibQueryColumnExprKind::Case;
	caseE->m_cases = { { ibQueryPredicate::Leaf(gt), ibQueryColumnExpr::Const(ibValue(wxString(wxT("bulk")))) } };
	caseE->m_else  = ibQueryColumnExpr::Const(ibValue(wxString(wxT("unit"))));
	EXPECT_EQ(ibQueryComposer::EvalColumnExpr(caseE.get(), t, r).GetString().ToStdString(), "bulk");
}

// ===========================================================================
// Routing gates — CanColocateJoin
// ===========================================================================

TEST(QueryComposerGate, Join_TwoDistinctDbTables_Colocatable)
{
	ibBackendColumnRawDB aKey = ibBackendColumnRawDB::Number(wxT("a_key"));
	ibBackendColumnRawDB aOut = ibBackendColumnRawDB::String(wxT("a_out"));
	ibBackendColumnRawDB bKey = ibBackendColumnRawDB::Number(wxT("b_key"));
	ibBackendColumnRawDB bOut = ibBackendColumnRawDB::String(wxT("b_out"));
	TestQueryable A(wxT("TableA"), 1); A.AddCol(&aKey); A.AddCol(&aOut);
	TestQueryable B(wxT("TableB"), 2); B.AddCol(&bKey); B.AddCol(&bOut);

	auto root = Join2(&A, &B, &aKey, &bKey);
	SpecBuf buf;
	buf.sel = { { &aOut, wxT("ao") }, { &bOut, wxT("bo") } };
	const ibDataQuerySpec spec = buf.Make(root.get(), &A);

	EXPECT_TRUE(ibDbTableProvider::CanColocateJoin(spec));
}

TEST(QueryComposerGate, Join_SelfJoinSameTable_NotColocatable)
{
	ibBackendColumnRawDB aKey = ibBackendColumnRawDB::Number(wxT("a_key"));
	ibBackendColumnRawDB bKey = ibBackendColumnRawDB::Number(wxT("b_key"));
	ibBackendColumnRawDB aOut = ibBackendColumnRawDB::String(wxT("a_out"));
	TestQueryable A(wxT("Same"), 1); A.AddCol(&aKey); A.AddCol(&aOut);
	TestQueryable B(wxT("Same"), 2); B.AddCol(&bKey);   // SAME table name -> ambiguous

	auto root = Join2(&A, &B, &aKey, &bKey);
	SpecBuf buf; buf.sel = { { &aOut, wxT("ao") } };
	const ibDataQuerySpec spec = buf.Make(root.get(), &A);

	EXPECT_FALSE(ibDbTableProvider::CanColocateJoin(spec));   // self-join -> RAM
}

TEST(QueryComposerGate, Join_ComputedLeaf_NotColocatable)
{
	ibBackendColumnRawDB aKey = ibBackendColumnRawDB::Number(wxT("a_key"));
	ibBackendColumnRawDB aOut = ibBackendColumnRawDB::String(wxT("a_out"));
	ibBackendColumnRawDB cKey = ibBackendColumnRawDB::Number(wxT("c_key"));
	TestQueryable A(wxT("TableA"), 1);             A.AddCol(&aKey); A.AddCol(&aOut);
	TestQueryable C(wxT("Slice"), 2, /*computed*/true); C.AddCol(&cKey);   // RAM-computed leaf

	auto root = Join2(&A, &C, &aKey, &cKey);
	SpecBuf buf; buf.sel = { { &aOut, wxT("ao") } };
	const ibDataQuerySpec spec = buf.Make(root.get(), &A);

	EXPECT_FALSE(ibDbTableProvider::CanColocateJoin(spec));   // a computed leaf -> RAM (or temp-promote, not this gate)
}

TEST(QueryComposerGate, Join_ColumnTheta_Colocatable)
{
	// A column-to-column theta join (a_key > b_key) over two DB tables renders server-side now — the ON
	// carries its real op, not a forced Eq. Balance-on-date / range joins ride this instead of RAM-folding.
	// (A COMPUTED ON — a.x+1 > b.y, m_exprL set — still falls to RAM; that path is the computed-leaf gate.)
	ibBackendColumnRawDB aKey = ibBackendColumnRawDB::Number(wxT("a_key"));
	ibBackendColumnRawDB aOut = ibBackendColumnRawDB::String(wxT("a_out"));
	ibBackendColumnRawDB bKey = ibBackendColumnRawDB::Number(wxT("b_key"));
	ibBackendColumnRawDB bOut = ibBackendColumnRawDB::String(wxT("b_out"));
	TestQueryable A(wxT("TableA"), 1); A.AddCol(&aKey); A.AddCol(&aOut);
	TestQueryable B(wxT("TableB"), 2); B.AddCol(&bKey); B.AddCol(&bOut);

	auto root = Join2(&A, &B, &aKey, &bKey);
	root->m_on.m_op = ibJoinCompareOp::Gt;   // theta, not Eq
	SpecBuf buf; buf.sel = { { &aOut, wxT("ao") }, { &bOut, wxT("bo") } };
	const ibDataQuerySpec spec = buf.Make(root.get(), &A);

	EXPECT_TRUE(ibDbTableProvider::CanColocateJoin(spec));   // column theta -> SQL push-down
}

// ===========================================================================
// Routing gates — CanColocateUnion / CanColocateAggregate
// ===========================================================================

TEST(QueryComposerGate, Union_TwoDbBranchesScalar_Colocatable)
{
	ibBackendColumnRawDB aCode = ibBackendColumnRawDB::String(wxT("code"));
	ibBackendColumnRawDB bCode = ibBackendColumnRawDB::String(wxT("code"));
	TestQueryable A(wxT("TableA"), 1); A.AddCol(&aCode);
	TestQueryable B(wxT("TableB"), 2); B.AddCol(&bCode);

	auto root = std::make_shared<ibQueryNode>();
	root->m_kind = ibQueryNode::Kind::Union;
	root->m_parts.push_back(ibQueryNode::Source(&A));
	root->m_parts.push_back(ibQueryNode::Source(&B));

	SpecBuf buf; buf.sel = { { &aCode, wxT("code") } };   // resolved per-branch by NAME
	const ibDataQuerySpec spec = buf.Make(root.get(), &A);

	EXPECT_TRUE(ibDbTableProvider::CanColocateUnion(spec));
}

TEST(QueryComposerGate, Union_ComputedBranch_NotColocatableDirectly)
{
	ibBackendColumnRawDB aCode = ibBackendColumnRawDB::String(wxT("code"));
	ibBackendColumnRawDB cCode = ibBackendColumnRawDB::String(wxT("code"));
	TestQueryable A(wxT("TableA"), 1);             A.AddCol(&aCode);
	TestQueryable C(wxT("Slice"), 2, /*computed*/true); C.AddCol(&cCode);

	auto root = std::make_shared<ibQueryNode>();
	root->m_kind = ibQueryNode::Kind::Union;
	root->m_parts.push_back(ibQueryNode::Source(&A));
	root->m_parts.push_back(ibQueryNode::Source(&C));

	SpecBuf buf; buf.sel = { { &aCode, wxT("code") } };
	const ibDataQuerySpec spec = buf.Make(root.get(), &A);

	// A computed branch fails the DIRECT union gate (the composer's mixed-promote path handles it).
	EXPECT_FALSE(ibDbTableProvider::CanColocateUnion(spec));
}

// The co-located UNION read pushes the full boolean WHERE tree + RLS semi-join into EACH branch
// (BuildBranchPredicate). Regression guard: before this, m_predicate was silently dropped on the
// server-side union -> a boolean OR/NOT WHERE returned too many rows, and an RLS restriction leaked.
TEST(QueryComposerGate, Union_BooleanPredicate_Colocatable)
{
	ibBackendColumnRawDB aReg = ibBackendColumnRawDB::String(wxT("region"));
	ibBackendColumnRawDB bReg = ibBackendColumnRawDB::String(wxT("region"));
	TestQueryable A(wxT("TableA"), 1); A.AddCol(&aReg);
	TestQueryable B(wxT("TableB"), 2); B.AddCol(&bReg);

	auto root = std::make_shared<ibQueryNode>();
	root->m_kind = ibQueryNode::Kind::Union;
	root->m_parts.push_back(ibQueryNode::Source(&A));
	root->m_parts.push_back(ibQueryNode::Source(&B));

	SpecBuf buf; buf.sel = { { &aReg, wxT("region") } };
	ibDataQuerySpec spec = buf.Make(root.get(), &A);
	auto leaf = [&](const wxString& v) {
		ibQueryCondition c; c.m_col = &aReg; c.m_value = ibValue(v); return ibQueryPredicate::Leaf(c);
	};
	spec.m_predicate = ibQueryPredicate::Compose(ibQueryPredicateKind::Or, leaf(wxT("North")), leaf(wxT("South")));

	EXPECT_TRUE(ibDbTableProvider::CanColocateUnion(spec));   // boolean tree renders per branch -> server-side
}

TEST(QueryComposerGate, Union_SemiJoinPredicate_Colocatable)
{
	// An RLS restriction (semi-join on the region key) whose outer key resolves in every branch rides
	// the co-located union server-side -- RLS on a UNION read, not just a JOIN.
	ibBackendColumnRawDB aReg = ibBackendColumnRawDB::String(wxT("region"));
	ibBackendColumnRawDB bReg = ibBackendColumnRawDB::String(wxT("region"));
	ibBackendColumnRawDB permReg = ibBackendColumnRawDB::String(wxT("region"));
	TestQueryable A(wxT("TableA"), 1);         A.AddCol(&aReg);
	TestQueryable B(wxT("TableB"), 2);         B.AddCol(&bReg);
	TestQueryable P(wxT("AllowedRegions"), 3); P.AddCol(&permReg);   // permission source

	auto root = std::make_shared<ibQueryNode>();
	root->m_kind = ibQueryNode::Kind::Union;
	root->m_parts.push_back(ibQueryNode::Source(&A));
	root->m_parts.push_back(ibQueryNode::Source(&B));

	SpecBuf buf; buf.sel = { { &aReg, wxT("region") } };
	ibDataQuerySpec spec = buf.Make(root.get(), &A);
	ibSemiJoinExists sj; sj.m_inner = &P; sj.m_outerKey = &aReg; sj.m_innerKey = &permReg;
	ibQueryCondition c; c.m_semiJoin = std::make_shared<ibSemiJoinExists>(sj);
	spec.m_predicate = ibQueryPredicate::Leaf(c);

	EXPECT_TRUE(ibDbTableProvider::CanColocateUnion(spec));   // semi-join outer key resolves in every branch
}

TEST(QueryComposerGate, Union_PredicateColMissingInBranch_NotColocatable)
{
	// The predicate references a column present only in branch A -> can't render over B -> RAM (applies it).
	ibBackendColumnRawDB aReg = ibBackendColumnRawDB::String(wxT("region"));
	ibBackendColumnRawDB aExtra = ibBackendColumnRawDB::String(wxT("extra"));
	ibBackendColumnRawDB bReg = ibBackendColumnRawDB::String(wxT("region"));
	TestQueryable A(wxT("TableA"), 1); A.AddCol(&aReg); A.AddCol(&aExtra);
	TestQueryable B(wxT("TableB"), 2); B.AddCol(&bReg);   // no "extra"

	auto root = std::make_shared<ibQueryNode>();
	root->m_kind = ibQueryNode::Kind::Union;
	root->m_parts.push_back(ibQueryNode::Source(&A));
	root->m_parts.push_back(ibQueryNode::Source(&B));

	SpecBuf buf; buf.sel = { { &aReg, wxT("region") } };
	ibDataQuerySpec spec = buf.Make(root.get(), &A);
	ibQueryCondition c; c.m_col = &aExtra; c.m_value = ibValue(wxString(wxT("x")));
	spec.m_predicate = ibQueryPredicate::Leaf(c);

	EXPECT_FALSE(ibDbTableProvider::CanColocateUnion(spec));   // "extra" absent in branch B -> RAM
}

TEST(QueryComposerGate, Union_DotWalkPredicate_NotColocatable)
{
	// A dot-walk predicate leaf (a reference path) needs a join the branch scan lacks -> RAM.
	ibBackendColumnRawDB aReg = ibBackendColumnRawDB::String(wxT("region"));
	ibBackendColumnRawDB bReg = ibBackendColumnRawDB::String(wxT("region"));
	ibBackendColumnRawDB refc = ibBackendColumnRawDB::String(wxT("owner"));
	TestQueryable A(wxT("TableA"), 1); A.AddCol(&aReg);
	TestQueryable B(wxT("TableB"), 2); B.AddCol(&bReg);

	auto root = std::make_shared<ibQueryNode>();
	root->m_kind = ibQueryNode::Kind::Union;
	root->m_parts.push_back(ibQueryNode::Source(&A));
	root->m_parts.push_back(ibQueryNode::Source(&B));

	SpecBuf buf; buf.sel = { { &aReg, wxT("region") } };
	ibDataQuerySpec spec = buf.Make(root.get(), &A);
	ibQueryCondition c; c.m_col = &aReg; c.m_value = ibValue(wxString(wxT("x")));
	c.m_path = { &refc, &aReg };   // dot-walk path present -> not a plain branch column
	spec.m_predicate = ibQueryPredicate::Leaf(c);

	EXPECT_FALSE(ibDbTableProvider::CanColocateUnion(spec));   // dot-walk in predicate -> RAM
}

TEST(QueryComposerGate, Aggregate_GroupBySum_Colocatable)
{
	ibBackendColumnRawDB aKey = ibBackendColumnRawDB::Number(wxT("a_key"));
	ibBackendColumnRawDB aDim = ibBackendColumnRawDB::String(wxT("a_dim"));
	ibBackendColumnRawDB bKey = ibBackendColumnRawDB::Number(wxT("b_key"));
	ibBackendColumnRawDB bAmt = ibBackendColumnRawDB::Number(wxT("b_amt"));
	TestQueryable A(wxT("TableA"), 1); A.AddCol(&aKey); A.AddCol(&aDim);
	TestQueryable B(wxT("TableB"), 2); B.AddCol(&bKey); B.AddCol(&bAmt);

	auto root = Join2(&A, &B, &aKey, &bKey);
	SpecBuf buf;
	buf.groupBy = { &aDim };
	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn = ibDataQueryBuilder::AggregateFn::Sum; sum.m_col = &bAmt; sum.m_alias = wxT("total");
	buf.aggs = { sum };
	const ibDataQuerySpec spec = buf.Make(root.get(), &A);

	EXPECT_TRUE(ibDbTableProvider::CanColocateAggregate(spec));
}

// ===========================================================================
// Routing gate — CanColocateRollupTotals (the STRUCTURAL half of the multi-source hierarchical
// TOTALS push-down: a co-located JOIN with SCALAR group LEVELS / aggregate inputs. When it holds AND
// the dialect advertises ROLLUP, a TOTALS over a JOIN runs server-side (GROUP BY ROLLUP) instead of
// the composer materialising both leaves and folding the totals tree in RAM. The dialect capability —
// CanPushColocatedRollupTotals — is DB-intrinsic, exercised at integration scope like the single-
// source CanPushRollupTotals.)
// ===========================================================================

TEST(QueryComposerGate, RollupTotals_JoinScalarLevels_Colocatable)
{
	ibBackendColumnRawDB aKey = ibBackendColumnRawDB::Number(wxT("a_key"));
	ibBackendColumnRawDB aDim = ibBackendColumnRawDB::String(wxT("a_dim"));
	ibBackendColumnRawDB bKey = ibBackendColumnRawDB::Number(wxT("b_key"));
	ibBackendColumnRawDB bAmt = ibBackendColumnRawDB::Number(wxT("b_amt"));
	TestQueryable A(wxT("TableA"), 1); A.AddCol(&aKey); A.AddCol(&aDim);
	TestQueryable B(wxT("TableB"), 2); B.AddCol(&bKey); B.AddCol(&bAmt);

	auto root = Join2(&A, &B, &aKey, &bKey);
	SpecBuf buf;
	buf.groupBy = { &aDim };                                   // one scalar TOTALS level
	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn = ibDataQueryBuilder::AggregateFn::Sum; sum.m_col = &bAmt; sum.m_alias = wxT("total");
	buf.aggs = { sum };
	const ibDataQuerySpec spec = buf.Make(root.get(), &A);

	EXPECT_TRUE(ibDbTableProvider::CanColocateRollupTotals(spec));   // JOIN + scalar level + scalar SUM -> server-side ROLLUP
}

// ===========================================================================
// The gate must read the door's OWN lists. A door that folds TOTALS fills m_totals / m_totalAggregates
// (TotalByLevel + Totals()) and never touches m_groupBy — so a gate that looked only at m_groupBy
// refused every totals query the composer produces, and the whole server-side fold was unreachable
// from it. These are the tests that would have said so: they build the spec the way the door does.
// ===========================================================================

TEST(QueryComposerGate, RollupTotals_LevelsFromTotalsList_ShapeAccepted)
{
	ibBackendColumnRawDB dim = ibBackendColumnRawDB::String(wxT("dim"));
	ibBackendColumnRawDB amt = ibBackendColumnRawDB::Number(wxT("amt"));
	TestQueryable A(wxT("TableA"), 1); A.AddCol(&dim); A.AddCol(&amt);

	SpecBuf buf;
	buf.totals.push_back(ibTotalLevel::One(&dim, ibDimensionKind::Elements));   // TOTALS … BY dim
	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn = ibDataQueryBuilder::AggregateFn::Sum; sum.m_col = &amt; sum.m_alias = wxT("total");
	buf.totalAggs = { sum };                                                   // …and the figure it rolls
	// groupBy / aggs stay EMPTY — that is the whole point.
	const ibDataQuerySpec spec = buf.Make(nullptr, &A);

	EXPECT_TRUE(ibDbTableProvider::CanRollupTotalsShape(spec));
}

TEST(QueryComposerGate, RollupTotals_TupleLevel_ShapeAccepted)
{
	// ONE level over TWO fields — `BY (Partner, Contract)`. It is one heading, and the fold renders it
	// as ONE composite ROLLUP element; the gate must not mistake it for two levels or refuse it.
	ibBackendColumnRawDB partner  = ibBackendColumnRawDB::String(wxT("partner"));
	ibBackendColumnRawDB contract = ibBackendColumnRawDB::String(wxT("contract"));
	ibBackendColumnRawDB amt      = ibBackendColumnRawDB::Number(wxT("amt"));
	TestQueryable A(wxT("TableA"), 1); A.AddCol(&partner); A.AddCol(&contract); A.AddCol(&amt);

	SpecBuf buf;
	ibTotalLevel level;
	level.m_fields.push_back(ibTotalField{ &partner,  ibDimensionKind::Elements });
	level.m_fields.push_back(ibTotalField{ &contract, ibDimensionKind::Elements });
	buf.totals.push_back(level);
	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn = ibDataQueryBuilder::AggregateFn::Sum; sum.m_col = &amt; sum.m_alias = wxT("total");
	buf.totalAggs = { sum };
	const ibDataQuerySpec spec = buf.Make(nullptr, &A);

	EXPECT_TRUE(ibDbTableProvider::CanRollupTotalsShape(spec));
}

TEST(QueryComposerGate, RollupTotals_DetailLevelWithoutRowIdentity_Refused)
{
	// A level with NO fields is the detail records, and they travel as the PHANTOM LEVEL — the row's
	// own identity as the deepest group key. A source with no identity has nothing to group by, and
	// grouping by "the row" without a key would fold equal rows into one: the RAM fold keeps them,
	// where a row is a row because it was read as one.
	ibBackendColumnRawDB dim = ibBackendColumnRawDB::String(wxT("dim"));
	ibBackendColumnRawDB amt = ibBackendColumnRawDB::Number(wxT("amt"));
	TestQueryable A(wxT("TableA"), 1); A.AddCol(&dim); A.AddCol(&amt);   // no SetKey — no identity

	SpecBuf buf;
	buf.totals.push_back(ibTotalLevel::One(&dim, ibDimensionKind::Elements));
	buf.totals.push_back(ibTotalLevel{});                                      // the detail level
	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn = ibDataQueryBuilder::AggregateFn::Sum; sum.m_col = &amt; sum.m_alias = wxT("total");
	buf.totalAggs = { sum };
	const ibDataQuerySpec spec = buf.Make(nullptr, &A);

	EXPECT_FALSE(ibDbTableProvider::CanRollupTotalsShape(spec));
}

TEST(QueryComposerGate, RollupTotals_DetailLevelOverKeyedSource_ShapeAccepted)
{
	// THE PHANTOM LEVEL. The same report over a source that HAS a row identity goes to the server:
	// ROLLUP(dim, <identity>) returns the grand total, the dim headings AND the rows themselves in
	// one pass — which is the whole point of it, and what the RAM road pays detail-row memory for.
	ibBackendColumnRawDB key = ibBackendColumnRawDB::String(wxT("rowkey"));
	ibBackendColumnRawDB dim = ibBackendColumnRawDB::String(wxT("dim"));
	ibBackendColumnRawDB amt = ibBackendColumnRawDB::Number(wxT("amt"));
	TestQueryable A(wxT("TableA"), 1); A.AddCol(&key); A.AddCol(&dim); A.AddCol(&amt);
	A.SetKey(&key);

	SpecBuf buf;
	buf.totals.push_back(ibTotalLevel::One(&dim, ibDimensionKind::Elements));
	buf.totals.push_back(ibTotalLevel{});                                      // the detail level
	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn = ibDataQueryBuilder::AggregateFn::Sum; sum.m_col = &amt; sum.m_alias = wxT("total");
	buf.totalAggs = { sum };
	const ibDataQuerySpec spec = buf.Make(nullptr, &A);

	EXPECT_TRUE(ibDbTableProvider::CanRollupTotalsShape(spec));
}

TEST(QueryComposerGate, RollupTotals_DetailLevelNotLast_Refused)
{
	// The rows are the BOTTOM. A grouping BELOW a detail level would group rows by something they
	// were already split by — the composition does not produce it and the phantom level cannot say
	// it, so the gate refuses rather than lowering something else than what was asked.
	ibBackendColumnRawDB key = ibBackendColumnRawDB::String(wxT("rowkey"));
	ibBackendColumnRawDB dim = ibBackendColumnRawDB::String(wxT("dim"));
	ibBackendColumnRawDB amt = ibBackendColumnRawDB::Number(wxT("amt"));
	TestQueryable A(wxT("TableA"), 1); A.AddCol(&key); A.AddCol(&dim); A.AddCol(&amt);
	A.SetKey(&key);

	SpecBuf buf;
	buf.totals.push_back(ibTotalLevel{});                                      // detail level FIRST
	buf.totals.push_back(ibTotalLevel::One(&dim, ibDimensionKind::Elements));
	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn = ibDataQueryBuilder::AggregateFn::Sum; sum.m_col = &amt; sum.m_alias = wxT("total");
	buf.totalAggs = { sum };
	const ibDataQuerySpec spec = buf.Make(nullptr, &A);

	EXPECT_FALSE(ibDbTableProvider::CanRollupTotalsShape(spec));
}

TEST(QueryComposerGate, RollupTotals_HierarchyLevel_Refused)
{
	// A HIERARCHY unfold walks the reference's parent chain; no GROUP BY can do that, and folding it
	// server-side would come back as a plain grouping under a word that asked for something else.
	ibBackendColumnRawDB dim = ibBackendColumnRawDB::String(wxT("dim"));
	ibBackendColumnRawDB amt = ibBackendColumnRawDB::Number(wxT("amt"));
	TestQueryable A(wxT("TableA"), 1); A.AddCol(&dim); A.AddCol(&amt);

	SpecBuf buf;
	buf.totals.push_back(ibTotalLevel::One(&dim, ibDimensionKind::Hierarchy));
	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn = ibDataQueryBuilder::AggregateFn::Sum; sum.m_col = &amt; sum.m_alias = wxT("total");
	buf.totalAggs = { sum };
	const ibDataQuerySpec spec = buf.Make(nullptr, &A);

	EXPECT_FALSE(ibDbTableProvider::CanRollupTotalsShape(spec));
}

TEST(QueryComposerGate, RollupTotals_SingleSource_NotColocatable)
{
	// A single table is the single-source ROLLUP push (CanPushRollupTotals), NOT the co-located gate.
	ibBackendColumnRawDB dim = ibBackendColumnRawDB::String(wxT("dim"));
	ibBackendColumnRawDB amt = ibBackendColumnRawDB::Number(wxT("amt"));
	TestQueryable A(wxT("TableA"), 1); A.AddCol(&dim); A.AddCol(&amt);

	SpecBuf buf;
	buf.groupBy = { &dim };
	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn = ibDataQueryBuilder::AggregateFn::Sum; sum.m_col = &amt; sum.m_alias = wxT("total");
	buf.aggs = { sum };
	const ibDataQuerySpec spec = buf.Make(nullptr, &A);            // Source root (no join tree)

	EXPECT_FALSE(ibDbTableProvider::CanColocateRollupTotals(spec));
}

// ===========================================================================
// Routing gate — CanPageGroupLevel (single-level group KEYSET paging: one plain scalar dimension over a
// single DB source pages its groups server-side — GROUP BY dim ORDER BY dim [dim > anchor] LIMIT count —
// instead of reading every detail row and folding all groups in RAM). Outside the shape keeps the fold.
// ===========================================================================

TEST(QueryComposerGate, GroupLevelPage_SingleScalarDim_Pageable)
{
	ibBackendColumnRawDB dim = ibBackendColumnRawDB::String(wxT("code"));
	TestQueryable A(wxT("TableA"), 1); A.AddCol(&dim);
	SpecBuf buf;
	buf.groupBy = { &dim };                                  // one plain scalar grouping level
	const ibDataQuerySpec spec = buf.Make(nullptr, &A);      // single source (Source root)
	EXPECT_TRUE(ibDbTableProvider::CanPageGroupLevel(spec));
}

TEST(QueryComposerGate, GroupLevelPage_TwoLevels_NotPageable)
{
	ibBackendColumnRawDB dim1 = ibBackendColumnRawDB::String(wxT("code"));
	ibBackendColumnRawDB dim2 = ibBackendColumnRawDB::String(wxT("region"));
	TestQueryable A(wxT("TableA"), 1); A.AddCol(&dim1); A.AddCol(&dim2);
	SpecBuf buf;
	buf.groupBy = { &dim1, &dim2 };                          // two levels -> not a single-level drill
	const ibDataQuerySpec spec = buf.Make(nullptr, &A);
	EXPECT_FALSE(ibDbTableProvider::CanPageGroupLevel(spec));
}

TEST(QueryComposerGate, GroupLevelPage_DotWalkDim_NotPageable)
{
	ibBackendColumnRawDB owner = ibBackendColumnRawDB::Number(wxT("owner"));
	ibBackendColumnRawDB name  = ibBackendColumnRawDB::String(wxT("name"));
	TestQueryable A(wxT("TableA"), 1); A.AddCol(&owner); A.AddCol(&name);
	SpecBuf buf;
	buf.groupBy    = { &name };
	buf.groupPaths = { { &owner, &name } };                 // dot-walk path (Owner.Name) -> not a single-column keyset
	const ibDataQuerySpec spec = buf.Make(nullptr, &A);
	EXPECT_FALSE(ibDbTableProvider::CanPageGroupLevel(spec));
}

TEST(QueryComposerGate, GroupLevelPage_JoinSource_NotPageable)
{
	ibBackendColumnRawDB aKey = ibBackendColumnRawDB::Number(wxT("a_key"));
	ibBackendColumnRawDB aDim = ibBackendColumnRawDB::String(wxT("a_dim"));
	ibBackendColumnRawDB bKey = ibBackendColumnRawDB::Number(wxT("b_key"));
	TestQueryable A(wxT("TableA"), 1); A.AddCol(&aKey); A.AddCol(&aDim);
	TestQueryable B(wxT("TableB"), 2); B.AddCol(&bKey);
	auto root = Join2(&A, &B, &aKey, &bKey);                // multi-source join tree
	SpecBuf buf;
	buf.groupBy = { &aDim };
	const ibDataQuerySpec spec = buf.Make(root.get(), &A);
	EXPECT_FALSE(ibDbTableProvider::CanPageGroupLevel(spec));   // multi-source group -> RAM fold (co-location is a later step)
}

TEST(QueryComposerGate, GroupLevelPage_ReferenceDim_NotPageable)
{
	// A REFERENCE dimension is NOT a plain scalar: its value is a multi-field spread (TYPE + _RTRef + _RRRef).
	// Keyset-paging by it would drop the TYPE field the read reconstructs from ("field <col>_TYPE not found")
	// and has no meaningful keyset order (its id, not its presentation) -> the gate rejects it, routing to the
	// unpaged full-spread ExecuteAggregate. Regression guard for the "grouping a table by a reference crashes".
	TestCol dim(wxT("counterparty"), 2);
	dim.GetTypeDesc() = ibTypeDescription(reference_to_clsid(999));   // reference-typed dimension (spread > 1 field)
	TestQueryable A(wxT("doc"), 1); A.AddCol(&dim);
	SpecBuf buf;
	buf.groupBy = { &dim };
	const ibDataQuerySpec spec = buf.Make(nullptr, &A);
	EXPECT_FALSE(ibDbTableProvider::CanPageGroupLevel(spec));
}

TEST(QueryComposerGate, RollupTotals_ComputedLeaf_NotColocatable)
{
	// A computed (RAM) leaf in the join can't co-locate -> the composer RAM-folds the totals tree.
	ibBackendColumnRawDB aKey = ibBackendColumnRawDB::Number(wxT("a_key"));
	ibBackendColumnRawDB aDim = ibBackendColumnRawDB::String(wxT("a_dim"));
	ibBackendColumnRawDB cKey = ibBackendColumnRawDB::Number(wxT("c_key"));
	ibBackendColumnRawDB cAmt = ibBackendColumnRawDB::Number(wxT("c_amt"));
	TestQueryable A(wxT("TableA"), 1);                   A.AddCol(&aKey); A.AddCol(&aDim);
	TestQueryable C(wxT("Slice"), 2, /*computed*/ true); C.AddCol(&cKey); C.AddCol(&cAmt);

	auto root = Join2(&A, &C, &aKey, &cKey);
	SpecBuf buf;
	buf.groupBy = { &aDim };
	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn = ibDataQueryBuilder::AggregateFn::Sum; sum.m_col = &cAmt; sum.m_alias = wxT("total");
	buf.aggs = { sum };
	const ibDataQuerySpec spec = buf.Make(root.get(), &A);

	EXPECT_FALSE(ibDbTableProvider::CanColocateRollupTotals(spec));
}

TEST(QueryComposerGate, RollupTotals_NoLevels_NotColocatable)
{
	// A totals query always has at least one level; an empty groupBy is not a totals shape.
	ibBackendColumnRawDB aKey = ibBackendColumnRawDB::Number(wxT("a_key"));
	ibBackendColumnRawDB aOut = ibBackendColumnRawDB::String(wxT("a_out"));
	ibBackendColumnRawDB bKey = ibBackendColumnRawDB::Number(wxT("b_key"));
	ibBackendColumnRawDB bAmt = ibBackendColumnRawDB::Number(wxT("b_amt"));
	TestQueryable A(wxT("TableA"), 1); A.AddCol(&aKey); A.AddCol(&aOut);
	TestQueryable B(wxT("TableB"), 2); B.AddCol(&bKey); B.AddCol(&bAmt);

	auto root = Join2(&A, &B, &aKey, &bKey);
	SpecBuf buf;                                                   // groupBy empty -> not a totals shape
	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn = ibDataQueryBuilder::AggregateFn::Sum; sum.m_col = &bAmt; sum.m_alias = wxT("total");
	buf.aggs = { sum };
	const ibDataQuerySpec spec = buf.Make(root.get(), &A);

	EXPECT_FALSE(ibDbTableProvider::CanColocateRollupTotals(spec));
}

// A UNION-of-branches TOTALS also pushes down: each branch projects the referenced columns under
// stable inner aliases, the DBMS ROLLUPs over the union derived table. The branches resolve the
// group / aggregate columns BY NAME.
TEST(QueryComposerGate, RollupTotals_UnionScalar_Colocatable)
{
	ibBackendColumnRawDB aDim = ibBackendColumnRawDB::String(wxT("dim"));
	ibBackendColumnRawDB aAmt = ibBackendColumnRawDB::Number(wxT("amt"));
	ibBackendColumnRawDB bDim = ibBackendColumnRawDB::String(wxT("dim"));
	ibBackendColumnRawDB bAmt = ibBackendColumnRawDB::Number(wxT("amt"));
	TestQueryable A(wxT("TableA"), 1); A.AddCol(&aDim); A.AddCol(&aAmt);
	TestQueryable B(wxT("TableB"), 2); B.AddCol(&bDim); B.AddCol(&bAmt);

	auto root = std::make_shared<ibQueryNode>();
	root->m_kind = ibQueryNode::Kind::Union;
	root->m_parts.push_back(ibQueryNode::Source(&A));
	root->m_parts.push_back(ibQueryNode::Source(&B));

	SpecBuf buf;
	buf.groupBy = { &aDim };                                   // resolved per branch by NAME ("dim")
	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn = ibDataQueryBuilder::AggregateFn::Sum; sum.m_col = &aAmt; sum.m_alias = wxT("total");
	buf.aggs = { sum };
	const ibDataQuerySpec spec = buf.Make(root.get(), &A);

	EXPECT_TRUE(ibDbTableProvider::CanColocateRollupTotals(spec));   // UNION + scalar level + scalar SUM -> server-side ROLLUP
}

TEST(QueryComposerGate, RollupTotals_UnionComputedBranch_NotColocatable)
{
	ibBackendColumnRawDB aDim = ibBackendColumnRawDB::String(wxT("dim"));
	ibBackendColumnRawDB aAmt = ibBackendColumnRawDB::Number(wxT("amt"));
	ibBackendColumnRawDB cDim = ibBackendColumnRawDB::String(wxT("dim"));
	ibBackendColumnRawDB cAmt = ibBackendColumnRawDB::Number(wxT("amt"));
	TestQueryable A(wxT("TableA"), 1);                   A.AddCol(&aDim); A.AddCol(&aAmt);
	TestQueryable C(wxT("Slice"), 2, /*computed*/ true); C.AddCol(&cDim); C.AddCol(&cAmt);

	auto root = std::make_shared<ibQueryNode>();
	root->m_kind = ibQueryNode::Kind::Union;
	root->m_parts.push_back(ibQueryNode::Source(&A));
	root->m_parts.push_back(ibQueryNode::Source(&C));

	SpecBuf buf;
	buf.groupBy = { &aDim };
	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn = ibDataQueryBuilder::AggregateFn::Sum; sum.m_col = &aAmt; sum.m_alias = wxT("total");
	buf.aggs = { sum };
	const ibDataQuerySpec spec = buf.Make(root.get(), &A);

	EXPECT_FALSE(ibDbTableProvider::CanColocateRollupTotals(spec));   // a computed branch -> RAM fold
}

TEST(QueryComposerGate, RollupTotals_UnionWithPredicate_NotColocatable)
{
	// A boolean WHERE tree / RLS restriction is NOT rendered on the union-branch path, so its presence
	// forces RAM (which applies it) — a co-located UNION totals never emits an under-restricted read.
	ibBackendColumnRawDB aDim = ibBackendColumnRawDB::String(wxT("dim"));
	ibBackendColumnRawDB aAmt = ibBackendColumnRawDB::Number(wxT("amt"));
	ibBackendColumnRawDB bDim = ibBackendColumnRawDB::String(wxT("dim"));
	ibBackendColumnRawDB bAmt = ibBackendColumnRawDB::Number(wxT("amt"));
	TestQueryable A(wxT("TableA"), 1); A.AddCol(&aDim); A.AddCol(&aAmt);
	TestQueryable B(wxT("TableB"), 2); B.AddCol(&bDim); B.AddCol(&bAmt);

	auto root = std::make_shared<ibQueryNode>();
	root->m_kind = ibQueryNode::Kind::Union;
	root->m_parts.push_back(ibQueryNode::Source(&A));
	root->m_parts.push_back(ibQueryNode::Source(&B));

	SpecBuf buf;
	buf.groupBy = { &aDim };
	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn = ibDataQueryBuilder::AggregateFn::Sum; sum.m_col = &aAmt; sum.m_alias = wxT("total");
	buf.aggs = { sum };
	ibDataQuerySpec spec = buf.Make(root.get(), &A);
	ibQueryCondition leaf; leaf.m_col = &aDim; leaf.m_value = ibValue(wxString(wxT("North")));
	spec.m_predicate = ibQueryPredicate::Leaf(leaf);              // stand-in for a boolean WHERE / RLS tree

	EXPECT_FALSE(ibDbTableProvider::CanColocateRollupTotals(spec));
}

// ===========================================================================
// Routing gate — CanRollupTotalsShape (the STRUCTURAL half of the SINGLE-source ROLLUP push; the
// full CanPushRollupTotals adds the ROLLUP-dialect capability, integration scope).
// ===========================================================================

TEST(QueryComposerGate, RollupTotals_SingleSourceScalar_Shape)
{
	ibBackendColumnRawDB dim = ibBackendColumnRawDB::String(wxT("dim"));
	ibBackendColumnRawDB amt = ibBackendColumnRawDB::Number(wxT("amt"));
	TestQueryable A(wxT("TableA"), 1); A.AddCol(&dim); A.AddCol(&amt);

	SpecBuf buf;
	buf.groupBy = { &dim };
	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn = ibDataQueryBuilder::AggregateFn::Sum; sum.m_col = &amt; sum.m_alias = wxT("total");
	buf.aggs = { sum };
	const ibDataQuerySpec spec = buf.Make(nullptr, &A);           // Source root (single source)

	EXPECT_TRUE(ibDbTableProvider::CanRollupTotalsShape(spec));
}

TEST(QueryComposerGate, RollupTotals_MultiSource_NotSingleShape)
{
	// A join tree is NOT the single-source shape (it is the co-located gate's job).
	ibBackendColumnRawDB aKey = ibBackendColumnRawDB::Number(wxT("a_key"));
	ibBackendColumnRawDB aDim = ibBackendColumnRawDB::String(wxT("a_dim"));
	ibBackendColumnRawDB bKey = ibBackendColumnRawDB::Number(wxT("b_key"));
	ibBackendColumnRawDB bAmt = ibBackendColumnRawDB::Number(wxT("b_amt"));
	TestQueryable A(wxT("TableA"), 1); A.AddCol(&aKey); A.AddCol(&aDim);
	TestQueryable B(wxT("TableB"), 2); B.AddCol(&bKey); B.AddCol(&bAmt);

	auto root = Join2(&A, &B, &aKey, &bKey);
	SpecBuf buf;
	buf.groupBy = { &aDim };
	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn = ibDataQueryBuilder::AggregateFn::Sum; sum.m_col = &bAmt; sum.m_alias = wxT("total");
	buf.aggs = { sum };
	const ibDataQuerySpec spec = buf.Make(root.get(), &A);

	EXPECT_FALSE(ibDbTableProvider::CanRollupTotalsShape(spec));   // join tree -> not single-source
}

TEST(QueryComposerGate, RollupTotals_SingleComputed_NotShape)
{
	// A computed (RAM) single source (a register slice) can't push ROLLUP -> RAM fold.
	ibBackendColumnRawDB dim = ibBackendColumnRawDB::String(wxT("dim"));
	ibBackendColumnRawDB amt = ibBackendColumnRawDB::Number(wxT("amt"));
	TestQueryable C(wxT("Slice"), 1, /*computed*/ true); C.AddCol(&dim); C.AddCol(&amt);

	SpecBuf buf;
	buf.groupBy = { &dim };
	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn = ibDataQueryBuilder::AggregateFn::Sum; sum.m_col = &amt; sum.m_alias = wxT("total");
	buf.aggs = { sum };
	const ibDataQuerySpec spec = buf.Make(nullptr, &C);

	EXPECT_FALSE(ibDbTableProvider::CanRollupTotalsShape(spec));   // computed source -> RAM
}

// ===========================================================================
// RAM core — BuildHierarchyTree (recursive parent-ref fold, modes, has-children, subtotals)
// ===========================================================================

namespace {

const ibMetaID KEY_ID = 1, HPARENT = 2, HAMT = 3;
const ibMetaID AGG0 = HAMT;          // aggregate rolls IN-PLACE into its own column (amount), not a synthetic id

// F1(0) ─ E1(10), F2(0) ─ E2(5)      E3(7) top-level element
ibQueryRamTable HierDetail()
{
	ibQueryRamTable d;
	d.AddColumn(KEY_ID,    wxT("key"),    kNoType);
	d.AddColumn(HPARENT, wxT("parent"), kNoType);
	d.AddColumn(HAMT,    wxT("amount"), kNoType);
	auto add = [&](const wxString& key, const wxString& parent, long amt) {
		const long r = d.AppendRow();
		d.SetCell(r, KEY_ID,    ibValue(key));
		d.SetCell(r, HPARENT, parent.IsEmpty() ? ibValue() : ibValue(parent));
		d.SetCell(r, HAMT,    ibValue(ibNumber(amt)));
	};
	add(wxT("1"), wxString(), 0);   // F1 folder
	add(wxT("2"), wxT("1"), 10);    // E1
	add(wxT("3"), wxT("1"), 0);     // F2 folder
	add(wxT("4"), wxT("3"), 5);     // E2
	add(wxT("5"), wxString(), 7);   // E3 top element
	return d;
}

ibDataQueryBuilder::AggregateItem SumOf(const ibBackendQueryColumn* col)
{
	ibDataQueryBuilder::AggregateItem a;
	a.m_fn = ibDataQueryBuilder::AggregateFn::Sum; a.m_col = col; a.m_alias = wxT("total");
	return a;
}

} // namespace

TEST(QueryHierarchy, Hierarchy_TreeSubtotalsHasChildren)
{
	TestCol keyCol(wxT("key"), KEY_ID), parentCol(wxT("parent"), HPARENT), amtCol(wxT("amount"), HAMT);
	const ibQueryRamTable d = HierDetail();
	const ibSelectorTree tree = ibQueryComposer::BuildHierarchyTree(
		d, &keyCol, &parentCol, { SumOf(&amtCol) }, ibDimensionKind::Hierarchy);

	const ibSelectorTree::Node& root = tree.Root();
	ASSERT_EQ(root.m_children.size(), 2u);                                   // F1, E3
	EXPECT_EQ(root.m_values.at(AGG0).GetString().ToStdString(), "22");       // grand total 0+10+0+5+7

	const ibSelectorTree::Node& f1 = *root.m_children[0];
	EXPECT_EQ(f1.m_values.at(KEY_ID).GetString().ToStdString(), "1");
	EXPECT_TRUE(f1.m_hasChildren);
	EXPECT_EQ(f1.m_values.at(AGG0).GetString().ToStdString(), "15");         // subtree subtotal
	ASSERT_EQ(f1.m_children.size(), 2u);                                     // E1, F2

	const ibSelectorTree::Node& f2 = *f1.m_children[1];
	EXPECT_TRUE(f2.m_hasChildren);
	EXPECT_EQ(f2.m_values.at(AGG0).GetString().ToStdString(), "5");

	const ibSelectorTree::Node& e3 = *root.m_children[1];
	EXPECT_FALSE(e3.m_hasChildren);                                          // leaf element
	EXPECT_EQ(e3.m_values.at(AGG0).GetString().ToStdString(), "7");
}

TEST(QueryHierarchy, HierarchyOnly_FoldersOnly)
{
	TestCol keyCol(wxT("key"), KEY_ID), parentCol(wxT("parent"), HPARENT), amtCol(wxT("amount"), HAMT);
	const ibQueryRamTable d = HierDetail();
	const ibSelectorTree tree = ibQueryComposer::BuildHierarchyTree(
		d, &keyCol, &parentCol, { SumOf(&amtCol) }, ibDimensionKind::HierarchyOnly);

	const ibSelectorTree::Node& root = tree.Root();
	ASSERT_EQ(root.m_children.size(), 1u);                                   // only F1 (E3 leaf omitted)
	const ibSelectorTree::Node& f1 = *root.m_children[0];
	ASSERT_EQ(f1.m_children.size(), 1u);                                     // only F2 (E1 leaf omitted)
	const ibSelectorTree::Node& f2 = *f1.m_children[0];
	EXPECT_TRUE(f2.m_hasChildren);                                           // E2 exists in data
	EXPECT_TRUE(f2.m_children.empty());                                      // but E2 is a leaf -> omitted
	EXPECT_EQ(f1.m_values.at(AGG0).GetString().ToStdString(), "15");         // subtotal still counts hidden leaves
}

TEST(QueryHierarchy, Elements_LeafNodesWithGrandTotal)
{
	TestCol keyCol(wxT("key"), KEY_ID), parentCol(wxT("parent"), HPARENT), amtCol(wxT("amount"), HAMT);
	const ibQueryRamTable d = HierDetail();
	const ibSelectorTree tree = ibQueryComposer::BuildHierarchyTree(
		d, &keyCol, &parentCol, { SumOf(&amtCol) }, ibDimensionKind::Elements);

	ASSERT_EQ(tree.Root().m_children.size(), 5u);                            // every row a leaf node, no nesting
	EXPECT_TRUE(tree.Root().m_children[0]->m_children.empty());             // leaves don't nest
	EXPECT_EQ(tree.Root().m_values.at(AGG0).GetString().ToStdString(), "22");
}

// ===========================================================================
// ibSelector — traversal façade: fold + mode over ONE snapshot, subtotals from it
// ===========================================================================

TEST(QuerySelector, Hierarchy_FoldsSnapshot_SubtotalsFromOneSnapshot)
{
	TestCol keyCol(wxT("key"), KEY_ID), parentCol(wxT("parent"), HPARENT), amtCol(wxT("amount"), HAMT);

	// Snapshot moves INTO the Selector; the fold + traversal kind live on the Selector, not the query.
	ibSelector sel(HierDetail(), ibSelectKind::ibSelectKind_ByGroupsHierarchy);
	sel.ByParentRef(&keyCol, &parentCol).Aggregating({ SumOf(&amtCol) });
	const ibSelectorTree tree = sel.Build();

	const ibSelectorTree::Node& root = tree.Root();
	ASSERT_EQ(root.m_children.size(), 2u);                                   // F1, E3
	EXPECT_EQ(root.m_values.at(AGG0).GetString().ToStdString(), "22");       // grand total from the snapshot
	EXPECT_TRUE(root.m_children[0]->m_hasChildren);                          // F1 expandable
	EXPECT_EQ(root.m_children[0]->m_values.at(AGG0).GetString().ToStdString(), "15");
	EXPECT_FALSE(root.m_children[1]->m_hasChildren);                         // E3 leaf
}

// A sub-selection without a WithSource recipe is inert — no DB hit, empty sub-Selector (the eager-only guard).
// The live sub-selection (a selector nested in a selector) runs at runtime over a real holder.
TEST(QuerySelector, Select_NoSource_EmptyAndNoDbHit)
{
	TestCol keyCol(wxT("key"), KEY_ID), parentCol(wxT("parent"), HPARENT), amtCol(wxT("amount"), HAMT);

	ibSelector sel(HierDetail(), ibSelectKind::ibSelectKind_ByGroupsHierarchy);
	sel.ByParentRef(&keyCol, &parentCol).Aggregating({ SumOf(&amtCol) });   // fold set, but NO WithSource

	ASSERT_TRUE(sel.Next());                                                 // stand on the first node (F1)
	const ibSelector child = sel.Select();                                  // no WithSource → guard → empty, no DB
	EXPECT_EQ(child.Snapshot().RowCount(), 0);
	EXPECT_TRUE(child.Snapshot().Columns().empty());
}

// The Selector is a CURSOR: Next() walks the folded tree pre-order, GetValue/Level read the current
// visit. One interface over flat-vs-tree (here: Hierarchy → folder then its subtree).
// ⭐⭐ A SELECTION WALKS ONE LEVEL, and what hangs under a node is reached by descending into it.
//
// This test used to assert the opposite — one cursor over the whole tree, pre-order, five visits —
// and that was the contract until 2026-08-22. It changed on purpose: three groupings are three
// nested loops, which is how the language reads and how a person writes it. Under the old shape the
// reader could tell a heading from a row only by watching Level() change, and a script written the
// obvious way saw every row twice, once mixed into the outer loop.
//
// The tree here: F1 and E3 at the top, E1 and F2 under F1, E2 under F2.
TEST(QuerySelector, Iterate_OneLevelPerSelection_DescendForChildren)
{
	TestCol keyCol(wxT("key"), KEY_ID), parentCol(wxT("parent"), HPARENT), amtCol(wxT("amount"), HAMT);
	ibSelector sel(HierDetail(), ibSelectKind::ibSelectKind_ByGroupsHierarchy);
	sel.ByParentRef(&keyCol, &parentCol).Aggregating({ SumOf(&amtCol) });

	auto keysOf = [&keyCol](ibSelector& s, std::vector<int>* levels = nullptr) {
		std::vector<std::string> out;
		while (s.Next()) {
			out.push_back(s.GetValue(&keyCol).GetString().ToStdString());
			if (levels != nullptr) levels->push_back(s.Level());
		}
		return out;
	};

	// The top level, and ONLY the top level.
	std::vector<int> topLevels;
	const std::vector<std::string> top = keysOf(sel, &topLevels);
	ASSERT_EQ(top.size(), 2u);
	EXPECT_EQ(top[0], "1");   EXPECT_EQ(topLevels[0], 1);   // F1
	EXPECT_EQ(top[1], "5");   EXPECT_EQ(topLevels[1], 1);   // E3

	// Descend into F1 — its own children, and LEVELS STAY ABSOLUTE across the descent.
	sel.Reset();
	ASSERT_TRUE(sel.Next());
	ASSERT_EQ(sel.GetValue(&keyCol).GetString(), wxT("1"));
	ibSelector underF1 = sel.Select(ibSelectKind::ibSelectKind_ByGroupsHierarchy);
	std::vector<int> f1Levels;
	const std::vector<std::string> f1 = keysOf(underF1, &f1Levels);
	ASSERT_EQ(f1.size(), 2u);
	EXPECT_EQ(f1[0], "2");   EXPECT_EQ(f1Levels[0], 2);     // E1
	EXPECT_EQ(f1[1], "3");   EXPECT_EQ(f1Levels[1], 2);     // F2

	// …and one floor further: F2's own child.
	underF1.Reset();
	ASSERT_TRUE(underF1.Next());
	ASSERT_TRUE(underF1.Next());                            // F2
	ASSERT_EQ(underF1.GetValue(&keyCol).GetString(), wxT("3"));
	ibSelector underF2 = underF1.Select(ibSelectKind::ibSelectKind_ByGroupsHierarchy);
	std::vector<int> f2Levels;
	const std::vector<std::string> f2 = keysOf(underF2, &f2Levels);
	ASSERT_EQ(f2.size(), 1u);
	EXPECT_EQ(f2[0], "4");   EXPECT_EQ(f2Levels[0], 3);     // E2, three floors down

	// Reset rewinds — the same tree walks again (no re-query, no re-fold).
	sel.Reset();
	EXPECT_EQ(keysOf(sel).size(), 2u);
}

// …AND AN EXHAUSTED SELECTION STAYS ON ITS LAST ROW. `Next()` answering false means "there is no
// next row", not "you are now nowhere": reading a column after the loop gives the last row's value
// rather than a screenful of NULLs, which is indistinguishable from a real row made of nothing.
TEST(QuerySelector, Exhausted_KeepsTheLastRow)
{
	TestCol keyCol(wxT("key"), KEY_ID), parentCol(wxT("parent"), HPARENT), amtCol(wxT("amount"), HAMT);
	ibSelector sel(HierDetail(), ibSelectKind::ibSelectKind_ByGroupsHierarchy);
	sel.ByParentRef(&keyCol, &parentCol).Aggregating({ SumOf(&amtCol) });

	while (sel.Next()) {}
	EXPECT_FALSE(sel.Next());                               // still refuses, idempotently
	EXPECT_EQ(sel.GetValue(&keyCol).GetString(), wxT("5")); // …and still stands on E3
}

// ===========================================================================
// PlanInnerJoinOrder — the smallest-first inner-chain planner (pure)
// ===========================================================================

TEST(QueryComposerPlan, Chain_StartsSmallest_GrowsByConnectivity)
{
	// A(1000) -e0- B(10) -e1- C(100): start at B; among the connected {A, C} pick C, then A.
	const std::vector<long> counts = { 1000, 10, 100 };
	const std::vector<std::pair<size_t, size_t>> edges = { { 0, 1 }, { 1, 2 } };
	const std::vector<size_t> order = ibQueryComposer::PlanInnerJoinOrder(counts, edges);
	ASSERT_EQ(order.size(), 3u);
	EXPECT_EQ(order[0], 1u);
	EXPECT_EQ(order[1], 2u);
	EXPECT_EQ(order[2], 0u);
}

TEST(QueryComposerPlan, Star_ConnectivityBeatsSize)
{
	// Star centre A(1000), leaves B(5) / C(7): start B; only A is connected to {B} —
	// the big centre joins before the small-but-disconnected C.
	const std::vector<long> counts = { 1000, 5, 7 };
	const std::vector<std::pair<size_t, size_t>> edges = { { 0, 1 }, { 0, 2 } };
	const std::vector<size_t> order = ibQueryComposer::PlanInnerJoinOrder(counts, edges);
	ASSERT_EQ(order.size(), 3u);
	EXPECT_EQ(order[0], 1u);
	EXPECT_EQ(order[1], 0u);
	EXPECT_EQ(order[2], 2u);
}

TEST(QueryComposerPlan, Disconnected_ReturnsEmpty)
{
	// Three units, one edge — no connected order exists; caller must keep the tree order.
	const std::vector<long> counts = { 10, 20, 30 };
	const std::vector<std::pair<size_t, size_t>> edges = { { 0, 1 } };
	EXPECT_TRUE(ibQueryComposer::PlanInnerJoinOrder(counts, edges).empty());
}

TEST(QueryComposerPlan, MalformedEdge_ReturnsEmpty)
{
	const std::vector<long> counts = { 10, 20 };
	// self-loop and out-of-range are both rejected
	EXPECT_TRUE(ibQueryComposer::PlanInnerJoinOrder(counts, { { 0, 0 } }).empty());
	EXPECT_TRUE(ibQueryComposer::PlanInnerJoinOrder(counts, { { 0, 5 } }).empty());
}

TEST(QueryComposerPlan, SingleUnit_TrivialOrder)
{
	const std::vector<size_t> order = ibQueryComposer::PlanInnerJoinOrder({ 42 }, {});
	ASSERT_EQ(order.size(), 1u);
	EXPECT_EQ(order[0], 0u);
}

TEST(QueryComposerPlan, TieBreaks_AreDeterministic)
{
	// Equal counts: the first index wins each pick (stable, reproducible plans).
	const std::vector<long> counts = { 50, 50, 50 };
	const std::vector<std::pair<size_t, size_t>> edges = { { 0, 1 }, { 1, 2 } };
	const std::vector<size_t> order = ibQueryComposer::PlanInnerJoinOrder(counts, edges);
	ASSERT_EQ(order.size(), 3u);
	EXPECT_EQ(order[0], 0u);
	EXPECT_EQ(order[1], 1u);
	EXPECT_EQ(order[2], 2u);
}

// ===========================================================================
// DedupeRows — the RAM dedup core behind plain UNION (and a future RAM DISTINCT)
// ===========================================================================

TEST(QueryComposerCore, DedupeRows_KeepsFirstOccurrence_PreservesOrder)
{
	const ibMetaID C1 = 1, C2 = 2;
	TestCol a(wxT("a"), C1), b(wxT("b"), C2);
	const std::vector<const ibBackendQueryColumn*> cols = { &a, &b };

	ibQueryRamTable t;
	t.AddColumn(C1, wxT("a"), kNoType);
	t.AddColumn(C2, wxT("b"), kNoType);
	auto row = [&](const wxString& va, long vb) {
		const long r = t.AppendRow();
		t.SetCell(r, C1, ibValue(va));
		t.SetCell(r, C2, ibValue(ibNumber(vb)));
	};
	row(wxT("x"), 1);
	row(wxT("y"), 2);
	row(wxT("x"), 1);   // duplicate of row 0
	row(wxT("x"), 3);   // same a, different b — NOT a duplicate
	row(wxT("y"), 2);   // duplicate of row 1

	const ibQueryRamTable out = ibQueryComposer::DedupeRows(t, cols);
	ASSERT_EQ(out.RowCount(), 3);
	EXPECT_EQ(out.GetCell(0, C1).GetString(), wxT("x"));
	EXPECT_EQ(out.GetCell(0, C2).GetInteger(), 1);
	EXPECT_EQ(out.GetCell(1, C1).GetString(), wxT("y"));
	EXPECT_EQ(out.GetCell(2, C2).GetInteger(), 3);
}

TEST(QueryComposerCore, DedupeRows_EmptyAndAllUnique)
{
	const ibMetaID C1 = 1;
	TestCol a(wxT("a"), C1);
	const std::vector<const ibBackendQueryColumn*> cols = { &a };

	ibQueryRamTable empty;
	empty.AddColumn(C1, wxT("a"), kNoType);
	EXPECT_EQ(ibQueryComposer::DedupeRows(empty, cols).RowCount(), 0);

	ibQueryRamTable uniq;
	uniq.AddColumn(C1, wxT("a"), kNoType);
	for (int i = 0; i < 4; ++i) {
		const long r = uniq.AppendRow();
		uniq.SetCell(r, C1, ibValue(ibNumber(i)));
	}
	EXPECT_EQ(ibQueryComposer::DedupeRows(uniq, cols).RowCount(), 4);
}

// ---- semi-join key reduction (sideways information passing) -------------------------
//
// Once one side of a join is materialised its key values are known, so the OTHER side can read only
// rows that can possibly join. Correctness can never depend on it (dropping a non-joining row cannot
// change the result), which is exactly why it needs its own observation: RecordingComputedQ reports
// what was pushed while ignoring it, so each case asserts BOTH that the reduction fired and that the
// answer is right without it.

TEST(QuerySemiJoin, InnerJoin_PushesDistinctKeysToTheOtherSide)
{
	const ibMetaID AK = 1, BK = 2, BV = 3;
	TestCol aKey(wxT("k"), AK), bKey(wxT("k"), BK), bVal(wxT("v"), BV);

	RecordingComputedQ A(wxT("a"), 100); A.AddCol(&aKey);
	RecordingComputedQ B(wxT("b"), 101); B.AddCol(&bKey); B.AddCol(&bVal);
	A.SetRows([&] { return KeyRows(AK, wxT("k"), { 1, 2, 3, 2 }); });   // 2 twice — the set must dedup
	B.SetRows([&] {
		ibQueryRamTable t;
		t.AddColumn(BK, wxT("k"), kNoType);
		t.AddColumn(BV, wxT("v"), kNoType);
		for (const long k : { 2, 3, 4 }) {
			const long r = t.AppendRow();
			t.SetCell(r, BK, ibValue(ibNumber(k)));
			t.SetCell(r, BV, ibValue(ibNumber(k * 10)));
		}
		return t;
	});

	ibDataQueryBuilder q(nullptr);
	q.From(&A).Join(&B, &aKey, &bKey);
	q.Select(&bVal, wxT("v"));
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	int rows = 0;
	while (res.Next()) ++rows;
	EXPECT_EQ(rows, 3);   // A holds 1,2,3,2 and B holds 2,3,4 -> keys 2 (twice) and 3

	ASSERT_EQ(B.Seen().size(), 1u) << "the right side should have been handed exactly one reduction";
	EXPECT_TRUE(B.Seen()[0].m_op == ibQueryFilterOp::In);
	EXPECT_EQ(B.Seen()[0].m_col, &bKey) << "the condition must name the RIGHT side's key column";
	EXPECT_EQ(B.Seen()[0].m_values.size(), 3u) << "distinct keys of the left side: 1, 2, 3";
	EXPECT_TRUE(A.Seen().empty()) << "the driving side is read first and receives nothing";
}

// THE correctness gate. `A LEFT JOIN B` preserves every A row, so the reduction may only ever travel
// left -> right. If the direction were flipped, A would lose its unmatched rows and this fails.
TEST(QuerySemiJoin, LeftJoin_KeepsUnmatchedLeftRows)
{
	const ibMetaID AK = 1, BK = 2, BV = 3;
	TestCol aKey(wxT("k"), AK), bKey(wxT("k"), BK), bVal(wxT("v"), BV);

	RecordingComputedQ A(wxT("a"), 110); A.AddCol(&aKey);
	RecordingComputedQ B(wxT("b"), 111); B.AddCol(&bKey); B.AddCol(&bVal);
	A.SetRows([&] { return KeyRows(AK, wxT("k"), { 1, 2 }); });
	B.SetRows([&] {
		ibQueryRamTable t;
		t.AddColumn(BK, wxT("k"), kNoType);
		t.AddColumn(BV, wxT("v"), kNoType);
		const long r = t.AppendRow();
		t.SetCell(r, BK, ibValue(ibNumber(2)));
		t.SetCell(r, BV, ibValue(ibNumber(20)));
		return t;
	});

	ibDataQueryBuilder q(nullptr);
	q.From(&A).Join(&B, &aKey, &bKey, ibQueryJoinKind::Left);
	q.Select(&aKey, wxT("k"));
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	int rows = 0;
	while (res.Next()) ++rows;
	EXPECT_EQ(rows, 2) << "key 1 has no match and MUST survive a LEFT join";

	ASSERT_EQ(B.Seen().size(), 1u) << "left -> right is the sound direction and stays enabled for LEFT";
	EXPECT_TRUE(B.Seen()[0].m_op == ibQueryFilterOp::In);
}

// A THETA ON (a.k > b.k) cannot be reduced by an equality key set — knowing which keys the left holds
// says nothing about which right rows satisfy `>`. The gate must leave it alone.
TEST(QuerySemiJoin, ThetaJoin_IsNotReduced)
{
	const ibMetaID AK = 1, BK = 2;
	TestCol aKey(wxT("k"), AK), bKey(wxT("k"), BK);

	RecordingComputedQ A(wxT("a"), 120); A.AddCol(&aKey);
	RecordingComputedQ B(wxT("b"), 121); B.AddCol(&bKey);
	A.SetRows([&] { return KeyRows(AK, wxT("k"), { 5 }); });
	B.SetRows([&] { return KeyRows(BK, wxT("k"), { 1, 2, 9 }); });

	ibDataQueryBuilder q(nullptr);
	q.From(&A).Join(&B, &aKey, &bKey, ibJoinCompareOp::Gt);
	q.Select(&bKey, wxT("k"));
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	int rows = 0;
	while (res.Next()) ++rows;
	EXPECT_EQ(rows, 2);   // 5 > 1 and 5 > 2, not 5 > 9

	EXPECT_TRUE(B.Seen().empty()) << "an equality key set must not be pushed into a theta join";
}

// An empty driving side yields no keys, so nothing is pushed — the join is empty anyway and the
// reduction must not manufacture a filter (an empty IN would be correct here, but the plain path is
// simpler and the result is the same; this pins that choice).
TEST(QuerySemiJoin, EmptyDrivingSidePushesNothing)
{
	const ibMetaID AK = 1, BK = 2;
	TestCol aKey(wxT("k"), AK), bKey(wxT("k"), BK);

	RecordingComputedQ A(wxT("a"), 130); A.AddCol(&aKey);
	RecordingComputedQ B(wxT("b"), 131); B.AddCol(&bKey);
	A.SetRows([&] { return KeyRows(AK, wxT("k"), {}); });
	B.SetRows([&] { return KeyRows(BK, wxT("k"), { 1, 2 }); });

	ibDataQueryBuilder q(nullptr);
	q.From(&A).Join(&B, &aKey, &bKey);
	q.Select(&bKey, wxT("k"));
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	int rows = 0;
	while (res.Next()) ++rows;
	EXPECT_EQ(rows, 0);
	EXPECT_TRUE(B.Seen().empty());
}

// A 3+ unit pure-INNER chain takes a DIFFERENT path (FlattenInnerChain + JoinUnitsSmallestFirst, which
// materialises every unit up front). The reduction has to ride there too, and it composes: each unit is
// reduced by the units already read, so the narrowing is transitive down the chain.
TEST(QuerySemiJoin, InnerChainOfThree_ReducesDownTheChain)
{
	const ibMetaID AK = 1, BK = 2, CK = 3;
	TestCol aKey(wxT("k"), AK), bKey(wxT("k"), BK), cKey(wxT("k"), CK);

	RecordingComputedQ A(wxT("a"), 140); A.AddCol(&aKey);
	RecordingComputedQ B(wxT("b"), 141); B.AddCol(&bKey);
	RecordingComputedQ C(wxT("c"), 142); C.AddCol(&cKey);
	A.SetRows([&] { return KeyRows(AK, wxT("k"), { 1, 2 }); });
	B.SetRows([&] { return KeyRows(BK, wxT("k"), { 2, 3 }); });
	C.SetRows([&] { return KeyRows(CK, wxT("k"), { 2, 3, 4 }); });

	ibDataQueryBuilder q(nullptr);
	q.From(&A).Join(&B, &aKey, &bKey).Join(&C, &bKey, &cKey);
	q.Select(&cKey, wxT("k"));
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	int rows = 0;
	while (res.Next()) ++rows;
	EXPECT_EQ(rows, 1);   // only key 2 is in all three

	ASSERT_EQ(B.Seen().size(), 1u);
	EXPECT_TRUE(B.Seen()[0].m_op == ibQueryFilterOp::In);
	EXPECT_EQ(B.Seen()[0].m_values.size(), 2u) << "A's keys 1, 2";

	ASSERT_EQ(C.Seen().size(), 1u) << "the second edge reduces C by what B holds";
	EXPECT_TRUE(C.Seen()[0].m_op == ibQueryFilterOp::In);
	EXPECT_EQ(C.Seen()[0].m_col, &cKey);

	EXPECT_TRUE(A.Seen().empty()) << "the head of the chain is read first";
}

// ---- the advisory contract of ComputeRows(extra) --------------------------------------
//
// Every register compute takes `extra` and drops it (`ComputeRows(const std::vector<ibQueryCondition>&
// /*extra*/)` — balance / turnover / slice). That was invisible while `extra` was always empty; the
// semi-join reduction now pushes a real filter through it. So the PROVIDER enforces: whatever the source
// chose to ignore is applied over the rows it returned. These pin that, on a single source — no join, so
// nothing else could be doing the filtering.

TEST(QueryComputedFilter, IgnoredConditionIsStillEnforced)
{
	const ibMetaID K = 1;
	TestCol key(wxT("k"), K);
	RecordingComputedQ src(wxT("s"), 150);
	src.AddCol(&key);
	src.SetRows([&] { return KeyRows(K, wxT("k"), { 1, 2, 3, 4 }); });   // ignores whatever it is handed

	ibDataQueryBuilder q(nullptr);
	q.From(&src);
	q.Select(&key, wxT("k"));
	q.WhereIn(&key, { ibValue(ibNumber(2)), ibValue(ibNumber(4)) });
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	int rows = 0;
	while (res.Next()) ++rows;
	EXPECT_EQ(rows, 2) << "the source returned all four rows; the provider must apply the filter it dropped";

	ASSERT_EQ(src.Seen().size(), 1u) << "the condition is still HANDED down (a source may push it deeper)";
	EXPECT_TRUE(src.Seen()[0].m_op == ibQueryFilterOp::In);
}

// The scalar ops go through the same enforcement — this is not an `In`-only patch.
TEST(QueryComputedFilter, IgnoredEqualityIsStillEnforced)
{
	const ibMetaID K = 1;
	TestCol key(wxT("k"), K);
	RecordingComputedQ src(wxT("s"), 151);
	src.AddCol(&key);
	src.SetRows([&] { return KeyRows(K, wxT("k"), { 1, 2, 3 }); });

	ibDataQueryBuilder q(nullptr);
	q.From(&src);
	q.Select(&key, wxT("k"));
	q.Where(&key, ibValue(ibNumber(2)));
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	int rows = 0;
	while (res.Next()) ++rows;
	EXPECT_EQ(rows, 1);
}

// A condition on a column the source does NOT vend must not silently empty the result: the missing cell
// would read back as NULL and the three-valued evaluator would drop every row. Such a condition is skipped
// by the enforcement (the source is the only one who could have meant it).
TEST(QueryComputedFilter, ConditionOnUnvendedColumnDoesNotEmptyTheResult)
{
	const ibMetaID K = 1, OTHER = 99;
	TestCol key(wxT("k"), K), absent(wxT("absent"), OTHER);
	RecordingComputedQ src(wxT("s"), 152);
	src.AddCol(&key);
	src.AddCol(&absent);                                       // declared on the source...
	src.SetRows([&] { return KeyRows(K, wxT("k"), { 1, 2 }); });   // ...but NOT in the rows it returns

	ibDataQueryBuilder q(nullptr);
	q.From(&src);
	q.Select(&key, wxT("k"));
	q.Where(&absent, ibValue(ibNumber(7)));
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	int rows = 0;
	while (res.Next()) ++rows;
	EXPECT_EQ(rows, 2) << "an unvended column is the source's business; enforcement must skip it";
}

// ⭐ MEASURES DO NOT CLOSE THE PAGE. The gate has never looked at m_aggregates — ExecuteGroupLevelPage
// projects them beside the dimension — and until 2026-08-20 nothing upstream noticed: the L4 router
// required a level with NO figures, so a grouped list with a SUM took the fold and read every detail
// row of the source to show one page of groups.
//
// This pins the premise that router now stands on. If a later change makes the gate reject a shape
// with aggregates, the routing above it goes quietly back to reading everything.
TEST(QueryComposerGate, GroupLevelPage_WithMeasures_StillPageable)
{
	ibBackendColumnRawDB dim = ibBackendColumnRawDB::String(wxT("code"));
	ibBackendColumnRawDB amt = ibBackendColumnRawDB::Number(wxT("amt"));
	TestQueryable A(wxT("TableA"), 1); A.AddCol(&dim); A.AddCol(&amt);

	SpecBuf buf;
	buf.groupBy = { &dim };                                  // one plain scalar grouping level…
	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn = ibDataQueryBuilder::AggregateFn::Sum; sum.m_col = &amt; sum.m_alias = wxT("total");
	buf.aggs = { sum };                                      // …and a figure on it
	const ibDataQuerySpec spec = buf.Make(nullptr, &A);

	EXPECT_TRUE(ibDbTableProvider::CanPageGroupLevel(spec));
}

// (No sibling test for the COMPUTED-select case: `SpecBuf` does not populate `m_selectExprs`, so the
//  gate's clause for it cannot be reached from this harness without widening the harness itself. The
//  router above refuses that shape on its own — a measure over a projection's name goes to the fold.)
