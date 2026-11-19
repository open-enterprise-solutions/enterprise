// L3 composer — the ROADS a read takes and the folds of a result (docs/private/query-one-road.md §7).
//
// PURE: no database of an application, no appData. Three layers:
//   1. Which road: one statement of the base reads a tree whose every leaf it joins (ibCanJoinOn); a tree with a
//      leaf computed in memory, or one another connection reads, is read in a base in memory — each leaf by its own
//      provider, a join's later side only for the keys the side before it holds.
//   2. The gates a source's own provider answers (CanPageGroupLevel, CanRollupTotalsShape, CanFoldGroupByTotals) —
//      given a query SHAPE, do they decide "fold on the server" correctly? Driven by a minimal mock queryable (raw
//      columns -> scalar / single-field-joinable, no metadata).
//   3. The result's folds (hierarchy, levels, the selector) and the reads of computed sources through the door, over
//      a base in memory of the read's own.

#include <gtest/gtest.h>
#include <wx/string.h>   // wxT, wxString - the engine's headers include no wx

#include <memory>
#include <vector>
#include <algorithm>   // stable_sort — RamSortCompareKey null-placement test
#include <functional>  // std::function — the recording computed queryable's row builder
#include <map>         // the minted-id ledger — SyntheticId collision sweep
#include <set>         // distinct ids a declaration mints

#include "backend/query/provider/queryComposer.h"            // ibQueryComposer + ibQueryRamTable
#include "backend/query/querySelector.h"                     // ibSelector — traversal façade over a snapshot
#include "backend/query/provider/database/dbTableProvider.h" // ibDbTableProvider — its gates, its refusal of a tree
#include "backend/query/provider/composedQueryable.h"        // ibCanJoinOn — one statement of the base reads the tree
#include "backend/query/dataQueryBuilder.h"                  // ibDataQuerySpec / ibQueryNode / AggregateItem
#include "backend/query/queryable.h"                         // ibBackendQueryable / ibQueryCondition
#include "backend/query/queryColumn.h"                       // ibBackendQueryColumn / ibBackendColumnRawDB
#include "backend/query/tempTableQueryable.h"                // ibCteQueryable — what a declared query publishes
#include "backend/query/queryException.h"                    // ibBackendQueryException — a tree the database refuses
#include "core/clsid.h"                    // reference_to_clsid — a reference-typed column (multi-field spread)

namespace {

// Minimal RAM-core column — name + model id (the row read key). No metaobject. (As in test_queryTotals.)
class TestCol : public ibBackendQueryColumn {
public:
	TestCol(const ibString& name, ibMetaID id) : m_name(name), m_id(id) {}
	ibString           GetName()         const override { return m_name; }
	ibString           GetPhysicalName() const override { return m_name; }
	ibTypeDescription& GetTypeDesc()     const override { return m_type; }
	ibMetaID           GetColumnId()      const override { return m_id; }
	const ibMetaData*  GetMetaData()     const override { return nullptr; }
private:
	ibString                  m_name;
	ibMetaID                  m_id;
	mutable ibTypeDescription m_type;
};

const ibTypeDescription kNoType;

// A minimal queryable for the routing gates: a named table over raw columns (raw -> scalar +
// single-field-joinable, so no type-descriptor / metadata setup is needed). The table holds a
// share of each column (each column belongs to exactly its table). Optionally COMPUTED, for the cases where
// a computed leaf sends the tree into a base in memory.
class TestQueryable : public ibBackendQueryable {
public:
	TestQueryable(const ibString& table, ibMetaID metaId, bool computed = false)
		: m_table(table), m_metaId(metaId), m_computed(computed) {
		ibGuidImpl impl{}; impl.m_data1 = static_cast<unsigned long>(metaId); m_key = ibGuid(impl);
	}
	void AddCol(const std::shared_ptr<const ibBackendQueryColumn>& c) { m_cols.push_back(c); }
	// ROW IDENTITY — what the phantom level groups by (a real source answers with its reference, or a
	// register with recorder + line number). Absent by default: a source that has none.
	void SetKey(const std::shared_ptr<const ibBackendQueryColumn>& c) { m_keys.push_back(c); }

	ibString GetQueryTableName() const override { return m_table; }
	ibMetaID GetQueryTableId()    const override { return m_metaId; }
	// A stable, table-distinct guid from the id, so identity logic still tells the two tables
	// apart — made once and kept, because the source hands out the key its metaobject keeps (a reference).
	const ibUniqueKey& GetQueryTableGuid() const override { return m_key; }
	// A computed leaf is read by the computed provider, as every real one is — the road is the provider's answer.
	ibBackendQueryProvider& GetProvider() const override {
		return m_computed ? ibComputedProviderInstance() : ibBackendQueryable::GetProvider();
	}
	const ibMetaData* GetMetaData() const override { return nullptr; }
	std::vector<std::shared_ptr<const ibBackendQueryColumn>> GetQueryColumns() const override { return m_cols; }
	bool ContainColumn(const std::shared_ptr<const ibBackendQueryColumn>& col) const override {
		for (const std::shared_ptr<const ibBackendQueryColumn>& c : m_cols) if (c == col) return true;
		return false;
	}
	std::vector<std::shared_ptr<const ibBackendQueryColumn>> GetPrimaryKeyColumns() const override { return m_keys; }
private:
	ibString m_table;
	ibMetaID m_metaId;
	bool     m_computed;
	ibUniqueKey m_key;   // its table guid, the id in the first word
	std::vector<std::shared_ptr<const ibBackendQueryColumn>> m_cols;
	std::vector<std::shared_ptr<const ibBackendQueryColumn>> m_keys;
};

// A computed queryable that RECORDS what the composer pushed into its ComputeRows, and then
// deliberately IGNORES it — exactly like the real register computes (balance / turnover / slice), which
// take the parameter as a narrowing at most. Two things ride on that:
//
//   * OBSERVABILITY. The semi-join key reduction is a PURE reduction — it can never change the answer —
//     so results alone cannot tell whether it fired. Reading what was pushed is the only way to see it.
//   * The ADVISORY contract. Honouring `extra` inside ComputeRows is an optimisation; the statement over the
//     source's copy applies the conditions regardless (ibQueryComposer::OverMemorySpace). A source that ignores
//     them (this one, and every register today) still comes back correctly filtered.
class RecordingComputedQ : public ibBackendQueryable {
public:
	RecordingComputedQ(const ibString& table, ibMetaID metaId) : m_table(table), m_metaId(metaId) {
		ibGuidImpl impl{}; impl.m_data1 = static_cast<unsigned long>(metaId); m_key = ibGuid(impl);
	}
	void AddCol(const std::shared_ptr<const ibBackendQueryColumn>& c) { m_cols.push_back(c); }
	void SetRows(std::function<ibQueryRamTable()> b) { m_build = std::move(b); }
	const std::vector<ibQueryCondition>& Seen() const { return m_seen; }

	ibBackendQueryProvider& GetProvider() const override { return ibComputedProviderInstance(); }
	ibQueryRamTable ComputeRows(const std::vector<ibQueryCondition>& extra) const override {
		m_seen = extra;                                  // record, then ignore — see the note above
		return m_build ? m_build() : ibQueryRamTable();
	}

	ibString GetQueryTableName() const override { return m_table; }
	ibMetaID GetQueryTableId()   const override { return m_metaId; }
	const ibUniqueKey& GetQueryTableGuid() const override { return m_key; }
	const ibMetaData* GetMetaData() const override { return nullptr; }
	std::vector<std::shared_ptr<const ibBackendQueryColumn>> GetQueryColumns() const override { return m_cols; }
	bool ContainColumn(const std::shared_ptr<const ibBackendQueryColumn>& col) const override {
		for (const std::shared_ptr<const ibBackendQueryColumn>& c : m_cols) if (c == col) return true;
		return false;
	}
private:
	ibString m_table;
	ibMetaID m_metaId;
	ibUniqueKey m_key;   // its table guid, the id in the first word
	std::vector<std::shared_ptr<const ibBackendQueryColumn>> m_cols;
	std::function<ibQueryRamTable()>                         m_build;
	mutable std::vector<ibQueryCondition>                    m_seen;
};

// A provider that WRITES ITS OWN STATEMENTS on another connection — a base elsewhere, the journal kept in SQL. It
// computes nothing in memory (IsComputedInRam stays false), and its CanJoinOn says yes to no connection of ours (the
// default), so none of its sources may go into our statement.
class ForeignProvider : public ibBackendQueryProvider {
public:
	ibDataQueryResult ExecuteRead(const ibDataQuerySpec& spec, const ibReadPageRequest& /*page*/) override {
		return ibDataQueryResult(ibQueryRamTable(), spec.m_queryable);
	}
};

// A table of that other connection: a plain table by every answer it gives, but read by the foreign provider.
class ForeignQueryable : public TestQueryable {
public:
	using TestQueryable::TestQueryable;
	ibBackendQueryProvider& GetProvider() const override { static ForeignProvider s_foreign; return s_foreign; }
};

// ⭐ A PROVIDER OF ANOTHER CONNECTION — a base elsewhere, its statements its server's. It pages what is asked of its
// one table (TryPageGroupLevel) and counts what it was handed, so a test tells a tree handed to it whole from leaves
// read one by one.
class RemoteProvider : public ibBackendQueryProvider {
public:
	ibDataQueryResult ExecuteRead(const ibDataQuerySpec& spec, const ibReadPageRequest& /*page*/) override {
		++(IsTree(spec) ? m_treeReads : m_leafReads);
		return NoRows(spec);
	}
	ibDataQueryResult ExecuteAggregate(const ibDataQuerySpec& spec) override {
		++(IsTree(spec) ? m_treeAggregates : m_leafAggregates);
		return NoRows(spec);
	}
	bool TryPageGroupLevel(const ibDataQuerySpec& spec, const ibReadPageRequest& /*page*/, ibDataQueryResult& out) override {
		if (IsTree(spec))
			return false;
		++m_pages;
		out = NoRows(spec);
		return true;
	}

	int m_treeReads = 0, m_leafReads = 0, m_treeAggregates = 0, m_leafAggregates = 0, m_pages = 0;

private:
	static bool IsTree(const ibDataQuerySpec& spec) { return spec.m_root != nullptr && spec.m_root->m_kind != ibQueryNode::Kind::Source; }
	// The source's columns and no rows: what came back is not the question, who was handed what is.
	static ibDataQueryResult NoRows(const ibDataQuerySpec& spec) {
		ibQueryRamTable rows;
		for (const std::shared_ptr<const ibBackendQueryColumn>& c : spec.m_queryable->GetQueryColumns())
			rows.AddColumn(c->GetColumnId(), c->GetName(), kNoType);
		return ibDataQueryResult(std::move(rows), spec.m_queryable);
	}
};

// A table of that connection.
class RemoteQueryable : public TestQueryable {
public:
	RemoteQueryable(const ibString& table, ibMetaID metaId, RemoteProvider& provider)
		: TestQueryable(table, metaId), m_provider(provider) {}
	ibBackendQueryProvider& GetProvider() const override { return m_provider; }
private:
	RemoteProvider& m_provider;
};

// One-column key table (model id `id`) holding `keys` — the driving side of the reductions below.
ibQueryRamTable KeyRows(ibMetaID id, const ibString& name, const std::vector<long>& keys)
{
	ibQueryRamTable t;
	t.AddColumn(id, name, kNoType);
	for (const long k : keys) {
		const long r = t.AppendRow();
		t.SetCell(r, id, ibValue(ibNumber(k)));
	}
	return t;
}

// Fill an ibDataQuerySpec with the side-vectors a test staged + a root + select list. The spec holds
// them by value, so Make() copies what was staged by then.
struct SpecBuf {
	std::vector<ibQueryCondition> conds;
	std::vector<ibValue> keyIn;
	std::vector<ibQuerySortItem> sorts;
	std::vector<std::shared_ptr<const ibBackendQueryColumn>> groupBy;
	std::vector<ibDataQueryBuilder::AggregateItem> aggs;
	std::vector<ibDataQueryBuilder::HavingItem> having;
	// A write is a SET of rows now (one row is the degenerate case), so the spec carries a vector
	// of assignment lists and never an empty one — readers say front() without testing first.
	std::vector<ibWriteRow> writeRows{ 1 };
	std::vector<ibDotWalkColumn> dots;
	std::vector<std::pair<std::shared_ptr<const ibBackendQueryColumn>, ibString>> sel;
	std::vector<std::vector<std::shared_ptr<const ibBackendQueryColumn>>> groupPaths;   // parallel to groupBy (dot-walk keys)
	std::vector<ibDotWalkColumn> dimWalks;                                              // TotalByDotWalk dimensions
	// The TOTALS door's own two lists. A door that folds hierarchical totals fills THESE and leaves
	// groupBy / aggs empty — which is exactly what the gate used to be blind to.
	std::vector<ibTotalLevel> totals;
	std::vector<ibDataQueryBuilder::AggregateItem> totalAggs;
	std::vector<ibQueryColumnSelect> computed;   // SelectExpr — a constant, a window beside the select list

	ibDataQuerySpec Make(const std::shared_ptr<ibQueryNode>& root, const std::shared_ptr<const ibBackendQueryable>& primary) {
		ibDataQuerySpec s;
		s.m_root = root; s.m_queryable = primary;
		s.m_conditions = conds; s.m_keyIn = keyIn; s.m_sorts = sorts;
		s.m_groupBy = groupBy; s.m_aggregates = aggs; s.m_having = having;
		s.m_writeRows = writeRows; s.m_dotWalks = dots; s.m_selectCols = sel;
		s.m_groupPaths = groupPaths; s.m_dimWalks = dimWalks;
		s.m_totals = totals; s.m_totalAggregates = totalAggs;
		s.m_selectExprs = computed;
		return s;
	}
};

std::shared_ptr<ibQueryNode> Join2(const std::shared_ptr<const ibBackendQueryable>& l, const std::shared_ptr<const ibBackendQueryable>& r,
                                   const std::shared_ptr<const ibBackendQueryColumn>& onL,
                                   const std::shared_ptr<const ibBackendQueryColumn>& onR)
{
	auto root = std::make_shared<ibQueryNode>();
	root->m_kind = ibQueryNode::Kind::Join;
	root->m_left = ibQueryNode::Source(l); root->m_right = ibQueryNode::Source(r);
	root->m_on.m_colL = onL; root->m_on.m_colR = onR;
	return root;
}

} // namespace

// ===========================================================================
// RAM ORDER BY — RamSortCompareKey (NULL = smallest: NULLS FIRST asc / NULLS LAST desc)
// ===========================================================================

TEST(QueryComposerCore, RamSortCompareKey_NullOrdering)
{
	const ibValue nul(ibValueTypes::TYPE_NULL);                   // SQL NULL
	const ibValue a(ibString(wxT("A"))), b(ibString(wxT("B")));

	// non-NULL total order, both directions; equal keys fall through (0)
	EXPECT_LT(RamSortCompareKey(a, b, true ), 0);   // A before B (asc)
	EXPECT_GT(RamSortCompareKey(a, b, false), 0);   // B before A (desc)
	EXPECT_EQ(RamSortCompareKey(a, a, true ), 0);   // equal -> next key

	// NULL is the smallest value
	EXPECT_LT(RamSortCompareKey(nul, a, true ), 0); // ASC -> NULL first
	EXPECT_GT(RamSortCompareKey(a, nul, true ), 0);
	EXPECT_GT(RamSortCompareKey(nul, a, false), 0); // DESC -> NULL last
	EXPECT_LT(RamSortCompareKey(a, nul, false), 0);

	EXPECT_EQ(RamSortCompareKey(nul, nul, true ), 0); // both NULL -> equal
	EXPECT_EQ(RamSortCompareKey(nul, nul, false), 0);
}

// Drive a stable_sort with the comparator: NULLs cluster first (ASC) / last (DESC) deterministically,
// instead of floating at their input position (the pre-fix behaviour, where operator< returned false
// both ways for a NULL so it compared "equal" to every non-NULL).
TEST(QueryComposerCore, RamSortCompareKey_StableSortPlacesNulls)
{
	const ibValue N(ibValueTypes::TYPE_NULL);
	const std::vector<ibValue> v = { ibValue(ibString(wxT("B"))), N, ibValue(ibString(wxT("A"))), N };

	std::vector<ibValue> asc = v;
	std::stable_sort(asc.begin(), asc.end(),
		[](const ibValue& x, const ibValue& y) { return RamSortCompareKey(x, y, true) < 0; });
	EXPECT_EQ(asc[0].GetType(), ibValueTypes::TYPE_NULL);         // NULLs first
	EXPECT_EQ(asc[1].GetType(), ibValueTypes::TYPE_NULL);
	EXPECT_EQ(asc[2].GetString().ToStdString(), "A");
	EXPECT_EQ(asc[3].GetString().ToStdString(), "B");

	std::vector<ibValue> desc = v;
	std::stable_sort(desc.begin(), desc.end(),
		[](const ibValue& x, const ibValue& y) { return RamSortCompareKey(x, y, false) < 0; });
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
// Which road — ibCanJoinOn: does ONE statement of the base read the whole tree? Every leaf's provider is asked
// (CanJoinOn); a leaf computed in memory, or one another connection reads, sends the tree into a base in memory.
// ===========================================================================

TEST(QueryComposerRoad, Join_TwoTablesOfTheBase_OneStatement)
{
	const std::shared_ptr<const ibBackendColumnRawDB> aKey = ibBackendColumnRawDB::Number(wxT("a_key"));
	const std::shared_ptr<const ibBackendColumnRawDB> bKey = ibBackendColumnRawDB::Number(wxT("b_key"));
	const auto A = std::make_shared<TestQueryable>(wxT("TableA"), 1); A->AddCol(aKey);
	const auto B = std::make_shared<TestQueryable>(wxT("TableB"), 2); B->AddCol(bKey);

	EXPECT_TRUE(ibCanJoinOn(Join2(A, B, aKey, bKey), nullptr));
}

TEST(QueryComposerRoad, Join_ComputedLeaf_ReadInMemory)
{
	const std::shared_ptr<const ibBackendColumnRawDB> aKey = ibBackendColumnRawDB::Number(wxT("a_key"));
	const std::shared_ptr<const ibBackendColumnRawDB> cKey = ibBackendColumnRawDB::Number(wxT("c_key"));
	const auto A = std::make_shared<TestQueryable>(wxT("TableA"), 1);                   A->AddCol(aKey);
	const auto C = std::make_shared<TestQueryable>(wxT("Slice"), 2, /*computed*/ true); C->AddCol(cKey);

	EXPECT_FALSE(ibCanJoinOn(Join2(A, C, aKey, cKey), nullptr));
}

// ⭐ A LEAF ANOTHER PROVIDER READS IS NOT THIS STATEMENT'S — even one that is no computed source at all: its own
// provider writes SQL for another connection, and rendered into ours it would name a table our server does not have.
TEST(QueryComposerRoad, Join_ForeignProviderLeaf_ReadInMemory)
{
	const std::shared_ptr<const ibBackendColumnRawDB> aKey = ibBackendColumnRawDB::Number(wxT("a_key"));
	const std::shared_ptr<const ibBackendColumnRawDB> fKey = ibBackendColumnRawDB::Number(wxT("f_key"));
	const auto A = std::make_shared<TestQueryable>(wxT("TableA"), 1);    A->AddCol(aKey);
	const auto F = std::make_shared<ForeignQueryable>(wxT("Remote"), 2); F->AddCol(fKey);

	EXPECT_FALSE(F->GetProvider().IsComputedInRam());               // not a computed source…
	EXPECT_FALSE(ibCanJoinOn(Join2(A, F, aKey, fKey), nullptr));    // …and still joined in memory, each side by its own provider
}

TEST(QueryComposerRoad, Union_ByItsBranches)
{
	const std::shared_ptr<const ibBackendColumnRawDB> aCode = ibBackendColumnRawDB::String(wxT("code"));
	const std::shared_ptr<const ibBackendColumnRawDB> bCode = ibBackendColumnRawDB::String(wxT("code"));
	const std::shared_ptr<const ibBackendColumnRawDB> cCode = ibBackendColumnRawDB::String(wxT("code"));
	const std::shared_ptr<const ibBackendColumnRawDB> fCode = ibBackendColumnRawDB::String(wxT("code"));
	const auto A = std::make_shared<TestQueryable>(wxT("TableA"), 1);                   A->AddCol(aCode);
	const auto B = std::make_shared<TestQueryable>(wxT("TableB"), 2);                   B->AddCol(bCode);
	const auto C = std::make_shared<TestQueryable>(wxT("Slice"), 3, /*computed*/ true); C->AddCol(cCode);
	const auto F = std::make_shared<ForeignQueryable>(wxT("Remote"), 4);                F->AddCol(fCode);

	const auto unionOf = [](const std::shared_ptr<const ibBackendQueryable>& first, const std::shared_ptr<const ibBackendQueryable>& second) {
		auto root = std::make_shared<ibQueryNode>();
		root->m_kind = ibQueryNode::Kind::Union;
		root->m_parts.push_back(ibQueryNode::Source(first));  root->m_partAll.push_back(true);
		root->m_parts.push_back(ibQueryNode::Source(second)); root->m_partAll.push_back(true);
		return root;
	};
	EXPECT_TRUE(ibCanJoinOn(unionOf(A, B), nullptr));
	EXPECT_FALSE(ibCanJoinOn(unionOf(A, C), nullptr)) << "a computed branch";
	EXPECT_FALSE(ibCanJoinOn(unionOf(A, F), nullptr)) << "a branch of another connection";
}

// ⭐ THE DOOR REFUSES A TREE. A join, its fold and a union reach the database's read as the composite they are
// (ibQueryComposer::OverComposite); handed one as a tree, the read would take its primary source alone and answer for
// the whole — refused before any statement is built.
TEST(QueryComposerRoad, ATreeHandedToATablesRead_Refused)
{
	const std::shared_ptr<const ibBackendColumnRawDB> aKey = ibBackendColumnRawDB::Number(wxT("a_key"));
	const std::shared_ptr<const ibBackendColumnRawDB> aDim = ibBackendColumnRawDB::String(wxT("a_dim"));
	const std::shared_ptr<const ibBackendColumnRawDB> bKey = ibBackendColumnRawDB::Number(wxT("b_key"));
	const auto A = std::make_shared<TestQueryable>(wxT("TableA"), 1); A->AddCol(aKey); A->AddCol(aDim);
	const auto B = std::make_shared<TestQueryable>(wxT("TableB"), 2); B->AddCol(bKey);

	SpecBuf read; read.sel = { { aDim, wxT("d") } };
	EXPECT_THROW(ibDbTableProviderInstance().ExecuteRead(read.Make(Join2(A, B, aKey, bKey), A), ibReadPageRequest{}),
		ibBackendQueryException);

	SpecBuf fold; fold.groupBy = { aDim };
	EXPECT_THROW(ibDbTableProviderInstance().ExecuteAggregate(fold.Make(Join2(A, B, aKey, bKey), A)), ibBackendQueryException);

	auto stack = std::make_shared<ibQueryNode>();
	stack->m_kind = ibQueryNode::Kind::Union;
	stack->m_parts.push_back(ibQueryNode::Source(A)); stack->m_partAll.push_back(true);
	stack->m_parts.push_back(ibQueryNode::Source(B)); stack->m_partAll.push_back(true);
	EXPECT_THROW(ibDbTableProviderInstance().ExecuteRead(read.Make(stack, A), ibReadPageRequest{}), ibBackendQueryException);
}

// ===========================================================================
// A provider of another connection reads and pages its own tables — the composer asks the provider of the source, and
// a tree of them is read leaf by leaf (RemoteProvider records what it was handed).
// ===========================================================================

// No statement of the base joins its tables, so a tree of them alone is read a table at a time — each by it, on its
// server — and joined in a base in memory (ibQueryComposer::OverMemorySpace).
TEST(QueryComposerRemote, Join_OfItsTables_ReadLeafByLeaf)
{
	RemoteProvider remote;
	const auto aKey = std::make_shared<TestCol>(wxT("k"), 1);
	const auto bKey = std::make_shared<TestCol>(wxT("k"), 2);
	const auto bVal = std::make_shared<TestCol>(wxT("v"), 3);
	const auto A = std::make_shared<RemoteQueryable>(wxT("a"), 100, remote); A->AddCol(aKey);
	const auto B = std::make_shared<RemoteQueryable>(wxT("b"), 101, remote); B->AddCol(bKey); B->AddCol(bVal);

	ibDataQueryBuilder q(nullptr);
	q.From(A).Join(B, aKey, bKey);
	q.Select(bVal, wxT("v"));
	ibReadPageRequest page;
	(void)q.Execute(page);

	EXPECT_EQ(remote.m_treeReads, 0) << "no tree is handed to it";
	EXPECT_EQ(remote.m_leafReads, 2) << "each of its tables is read by it, once";
}

// …and joined to a source it does not write, it reads its own table alone and the join is stitched in memory.
TEST(QueryComposerRemote, Join_ToAnotherProvider_EachSideReadByItsOwn)
{
	RemoteProvider remote;
	const ibMetaID AK = 1, BK = 2, BV = 3;
	const auto aKey = std::make_shared<TestCol>(wxT("k"), AK);
	const auto bKey = std::make_shared<TestCol>(wxT("k"), BK);
	const auto bVal = std::make_shared<TestCol>(wxT("v"), BV);
	const auto A = std::make_shared<RemoteQueryable>(wxT("a"), 100, remote); A->AddCol(aKey);
	const auto B = std::make_shared<RecordingComputedQ>(wxT("b"), 101);     B->AddCol(bKey); B->AddCol(bVal);
	B->SetRows([&] {
		ibQueryRamTable t;
		t.AddColumn(BK, wxT("k"), kNoType);
		t.AddColumn(BV, wxT("v"), kNoType);
		const long r = t.AppendRow();
		t.SetCell(r, BK, ibValue(ibNumber(1)));
		t.SetCell(r, BV, ibValue(ibNumber(10)));
		return t;
	});

	ibDataQueryBuilder q(nullptr);
	q.From(A).Join(B, aKey, bKey);
	q.Select(bVal, wxT("v"));
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	int rows = 0;
	while (res.Next()) ++rows;
	EXPECT_EQ(rows, 0) << "its table came back empty - an inner join of nothing";
	EXPECT_EQ(remote.m_treeReads, 0) << "a tree with a table it does not write is not its statement";
	EXPECT_GT(remote.m_leafReads, 0) << "its own table is read by it, on its server";
}

TEST(QueryComposerRemote, GroupLevelPage_OfItsTable_PagedByIt)
{
	RemoteProvider remote;
	const auto aKey = std::make_shared<TestCol>(wxT("k"), 1);
	const auto A = std::make_shared<RemoteQueryable>(wxT("a"), 100, remote); A->AddCol(aKey);

	SpecBuf buf;
	buf.groupBy = { aKey };
	ibReadPageRequest page;
	ibQueryComposer::ExecuteGroupLevelPage(buf.Make(nullptr, A), page);

	EXPECT_EQ(remote.m_pages, 1);
	EXPECT_EQ(remote.m_leafAggregates, 0) << "paged by it, not every group folded at once";
}

// ===========================================================================
// The gate must read the door's OWN lists. A door that folds TOTALS fills m_totals / m_totalAggregates
// (TotalByLevel + Totals()) and never touches m_groupBy — so a gate that looked only at m_groupBy
// refused every totals query the composer produces, and the whole server-side fold was unreachable
// from it. These are the tests that would have said so: they build the spec the way the door does.
// ===========================================================================

TEST(QueryComposerGate, RollupTotals_LevelsFromTotalsList_ShapeAccepted)
{
	const std::shared_ptr<const ibBackendColumnRawDB> dim = ibBackendColumnRawDB::String(wxT("dim"));
	const std::shared_ptr<const ibBackendColumnRawDB> amt = ibBackendColumnRawDB::Number(wxT("amt"));
	const auto A = std::make_shared<TestQueryable>(wxT("TableA"), 1); A->AddCol(dim); A->AddCol(amt);

	SpecBuf buf;
	buf.totals.push_back(ibTotalLevel::One(dim, ibDimensionKind::Elements));   // TOTALS … BY dim
	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn = ibDataQueryBuilder::AggregateFn::Sum; sum.m_col = amt; sum.m_alias = wxT("total");
	buf.totalAggs = { sum };                                                   // …and the figure it rolls
	// groupBy / aggs stay EMPTY — that is the whole point.
	const ibDataQuerySpec spec = buf.Make(nullptr, A);

	EXPECT_TRUE(ibDbTableProvider::CanRollupTotalsShape(spec));
}

TEST(QueryComposerGate, RollupTotals_TupleLevel_ShapeAccepted)
{
	// ONE level over TWO fields — `BY (Partner, Contract)`. It is one heading, and the fold renders it
	// as ONE composite ROLLUP element; the gate must not mistake it for two levels or refuse it.
	const std::shared_ptr<const ibBackendColumnRawDB> partner  = ibBackendColumnRawDB::String(wxT("partner"));
	const std::shared_ptr<const ibBackendColumnRawDB> contract = ibBackendColumnRawDB::String(wxT("contract"));
	const std::shared_ptr<const ibBackendColumnRawDB> amt      = ibBackendColumnRawDB::Number(wxT("amt"));
	const auto A = std::make_shared<TestQueryable>(wxT("TableA"), 1); A->AddCol(partner); A->AddCol(contract); A->AddCol(amt);

	SpecBuf buf;
	ibTotalLevel level;
	level.m_fields.push_back(ibTotalField{ partner,  ibDimensionKind::Elements });
	level.m_fields.push_back(ibTotalField{ contract, ibDimensionKind::Elements });
	buf.totals.push_back(level);
	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn = ibDataQueryBuilder::AggregateFn::Sum; sum.m_col = amt; sum.m_alias = wxT("total");
	buf.totalAggs = { sum };
	const ibDataQuerySpec spec = buf.Make(nullptr, A);

	EXPECT_TRUE(ibDbTableProvider::CanRollupTotalsShape(spec));
}

TEST(QueryComposerGate, RollupTotals_DetailLevelWithoutRowIdentity_Refused)
{
	// A level with NO fields is the detail records, and they travel as the PHANTOM LEVEL — the row's
	// own identity as the deepest group key. A source with no identity has nothing to group by, and
	// grouping by "the row" without a key would fold equal rows into one: the RAM fold keeps them,
	// where a row is a row because it was read as one.
	const std::shared_ptr<const ibBackendColumnRawDB> dim = ibBackendColumnRawDB::String(wxT("dim"));
	const std::shared_ptr<const ibBackendColumnRawDB> amt = ibBackendColumnRawDB::Number(wxT("amt"));
	const auto A = std::make_shared<TestQueryable>(wxT("TableA"), 1); A->AddCol(dim); A->AddCol(amt);   // no SetKey — no identity

	SpecBuf buf;
	buf.totals.push_back(ibTotalLevel::One(dim, ibDimensionKind::Elements));
	buf.totals.push_back(ibTotalLevel{});                                      // the detail level
	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn = ibDataQueryBuilder::AggregateFn::Sum; sum.m_col = amt; sum.m_alias = wxT("total");
	buf.totalAggs = { sum };
	const ibDataQuerySpec spec = buf.Make(nullptr, A);

	EXPECT_FALSE(ibDbTableProvider::CanRollupTotalsShape(spec));
}

TEST(QueryComposerGate, RollupTotals_DetailLevelOverKeyedSource_ShapeAccepted)
{
	// THE PHANTOM LEVEL. The same report over a source that HAS a row identity goes to the server:
	// ROLLUP(dim, <identity>) returns the grand total, the dim headings AND the rows themselves in
	// one pass — which is the whole point of it, and what the RAM road pays detail-row memory for.
	const std::shared_ptr<const ibBackendColumnRawDB> key = ibBackendColumnRawDB::String(wxT("rowkey"));
	const std::shared_ptr<const ibBackendColumnRawDB> dim = ibBackendColumnRawDB::String(wxT("dim"));
	const std::shared_ptr<const ibBackendColumnRawDB> amt = ibBackendColumnRawDB::Number(wxT("amt"));
	const auto A = std::make_shared<TestQueryable>(wxT("TableA"), 1); A->AddCol(key); A->AddCol(dim); A->AddCol(amt);
	A->SetKey(key);

	SpecBuf buf;
	buf.totals.push_back(ibTotalLevel::One(dim, ibDimensionKind::Elements));
	buf.totals.push_back(ibTotalLevel{});                                      // the detail level
	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn = ibDataQueryBuilder::AggregateFn::Sum; sum.m_col = amt; sum.m_alias = wxT("total");
	buf.totalAggs = { sum };
	const ibDataQuerySpec spec = buf.Make(nullptr, A);

	EXPECT_TRUE(ibDbTableProvider::CanRollupTotalsShape(spec));
}

TEST(QueryComposerGate, RollupTotals_DetailLevelNotLast_Refused)
{
	// The rows are the BOTTOM. A grouping BELOW a detail level would group rows by something they
	// were already split by — the composition does not produce it and the phantom level cannot say
	// it, so the gate refuses rather than lowering something else than what was asked.
	const std::shared_ptr<const ibBackendColumnRawDB> key = ibBackendColumnRawDB::String(wxT("rowkey"));
	const std::shared_ptr<const ibBackendColumnRawDB> dim = ibBackendColumnRawDB::String(wxT("dim"));
	const std::shared_ptr<const ibBackendColumnRawDB> amt = ibBackendColumnRawDB::Number(wxT("amt"));
	const auto A = std::make_shared<TestQueryable>(wxT("TableA"), 1); A->AddCol(key); A->AddCol(dim); A->AddCol(amt);
	A->SetKey(key);

	SpecBuf buf;
	buf.totals.push_back(ibTotalLevel{});                                      // detail level FIRST
	buf.totals.push_back(ibTotalLevel::One(dim, ibDimensionKind::Elements));
	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn = ibDataQueryBuilder::AggregateFn::Sum; sum.m_col = amt; sum.m_alias = wxT("total");
	buf.totalAggs = { sum };
	const ibDataQuerySpec spec = buf.Make(nullptr, A);

	EXPECT_FALSE(ibDbTableProvider::CanRollupTotalsShape(spec));
}

TEST(QueryComposerGate, RollupTotals_ColumnLevel_Refused)
{
	// A LEVEL ACROSS THE PAGE IS TOTALLED UNDER THE ROOT TOO — what each column adds up to over every row — and no
	// prefix of a ladder groups by a column's key alone: folded on the server, a cross table came back with its bottom
	// line empty, «Total» over nothing (2026-11-15). Neither the ROLLUP road nor the GROUP BY one takes it; the RAM
	// fold does (FoldAcross).
	const std::shared_ptr<const ibBackendColumnRawDB> region = ibBackendColumnRawDB::String(wxT("region"));
	const std::shared_ptr<const ibBackendColumnRawDB> product = ibBackendColumnRawDB::String(wxT("product"));
	const std::shared_ptr<const ibBackendColumnRawDB> amt = ibBackendColumnRawDB::Number(wxT("amt"));
	const auto A = std::make_shared<TestQueryable>(wxT("TableA"), 1); A->AddCol(region); A->AddCol(product); A->AddCol(amt);

	SpecBuf buf;
	buf.totals.push_back(ibTotalLevel::One(region, ibDimensionKind::Elements));
	buf.totals.push_back(ibTotalLevel::One(product, ibDimensionKind::Elements));
	buf.totals.back().m_axis = ibTotalsAxis::Columns;
	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn = ibDataQueryBuilder::AggregateFn::Sum; sum.m_col = amt; sum.m_alias = wxT("total");
	buf.totalAggs = { sum };
	const ibDataQuerySpec spec = buf.Make(nullptr, A);

	EXPECT_FALSE(ibDbTableProvider::CanRollupTotalsShape(spec));
	EXPECT_FALSE(ibDbTableProvider::CanFoldGroupByTotals(spec));

	// …and the same two levels down the page are a ladder, which both roads take.
	buf.totals.back().m_axis = ibTotalsAxis::Rows;
	const ibDataQuerySpec ladder = buf.Make(nullptr, A);
	EXPECT_TRUE(ibDbTableProvider::CanRollupTotalsShape(ladder));
	EXPECT_TRUE(ibDbTableProvider::CanFoldGroupByTotals(ladder));
}

TEST(QueryComposerGate, RollupTotals_HierarchyLevel_Refused)
{
	// A HIERARCHY unfold walks the reference's parent chain; no GROUP BY can do that, and folding it
	// server-side would come back as a plain grouping under a word that asked for something else.
	const std::shared_ptr<const ibBackendColumnRawDB> dim = ibBackendColumnRawDB::String(wxT("dim"));
	const std::shared_ptr<const ibBackendColumnRawDB> amt = ibBackendColumnRawDB::Number(wxT("amt"));
	const auto A = std::make_shared<TestQueryable>(wxT("TableA"), 1); A->AddCol(dim); A->AddCol(amt);

	SpecBuf buf;
	buf.totals.push_back(ibTotalLevel::One(dim, ibDimensionKind::Hierarchy));
	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn = ibDataQueryBuilder::AggregateFn::Sum; sum.m_col = amt; sum.m_alias = wxT("total");
	buf.totalAggs = { sum };
	const ibDataQuerySpec spec = buf.Make(nullptr, A);

	EXPECT_FALSE(ibDbTableProvider::CanRollupTotalsShape(spec));
}

// ===========================================================================
// Routing gate — CanPageGroupLevel (single-level group KEYSET paging: one plain scalar dimension over a
// single DB source pages its groups server-side — GROUP BY dim ORDER BY dim [dim > anchor] LIMIT count —
// instead of reading every detail row and folding all groups in RAM). Outside the shape keeps the fold.
// The database's gate of TryPageGroupLevel, which the composer asks of the provider of the spec's primary source.
// ===========================================================================

TEST(QueryComposerGate, GroupLevelPage_SingleScalarDim_Pageable)
{
	const std::shared_ptr<const ibBackendColumnRawDB> dim = ibBackendColumnRawDB::String(wxT("code"));
	const auto A = std::make_shared<TestQueryable>(wxT("TableA"), 1); A->AddCol(dim);
	SpecBuf buf;
	buf.groupBy = { dim };                                  // one plain scalar grouping level
	const ibDataQuerySpec spec = buf.Make(nullptr, A);      // single source (Source root)
	EXPECT_TRUE(ibDbTableProvider::CanPageGroupLevel(spec));
}

TEST(QueryComposerGate, GroupLevelPage_TwoLevels_NotPageable)
{
	const std::shared_ptr<const ibBackendColumnRawDB> dim1 = ibBackendColumnRawDB::String(wxT("code"));
	const std::shared_ptr<const ibBackendColumnRawDB> dim2 = ibBackendColumnRawDB::String(wxT("region"));
	const auto A = std::make_shared<TestQueryable>(wxT("TableA"), 1); A->AddCol(dim1); A->AddCol(dim2);
	SpecBuf buf;
	buf.groupBy = { dim1, dim2 };                          // two levels -> not a single-level drill
	const ibDataQuerySpec spec = buf.Make(nullptr, A);
	EXPECT_FALSE(ibDbTableProvider::CanPageGroupLevel(spec));
}

TEST(QueryComposerGate, GroupLevelPage_DotWalkDim_NotPageable)
{
	const std::shared_ptr<const ibBackendColumnRawDB> owner = ibBackendColumnRawDB::Number(wxT("owner"));
	const std::shared_ptr<const ibBackendColumnRawDB> name  = ibBackendColumnRawDB::String(wxT("name"));
	const auto A = std::make_shared<TestQueryable>(wxT("TableA"), 1); A->AddCol(owner); A->AddCol(name);
	SpecBuf buf;
	buf.groupBy    = { name };
	buf.groupPaths = { { owner, name } };                 // dot-walk path (Owner.Name) -> not a single-column keyset
	const ibDataQuerySpec spec = buf.Make(nullptr, A);
	EXPECT_FALSE(ibDbTableProvider::CanPageGroupLevel(spec));
}

TEST(QueryComposerGate, GroupLevelPage_JoinSource_NotPageable)
{
	const std::shared_ptr<const ibBackendColumnRawDB> aKey = ibBackendColumnRawDB::Number(wxT("a_key"));
	const std::shared_ptr<const ibBackendColumnRawDB> aDim = ibBackendColumnRawDB::String(wxT("a_dim"));
	const std::shared_ptr<const ibBackendColumnRawDB> bKey = ibBackendColumnRawDB::Number(wxT("b_key"));
	const auto A = std::make_shared<TestQueryable>(wxT("TableA"), 1); A->AddCol(aKey); A->AddCol(aDim);
	const auto B = std::make_shared<TestQueryable>(wxT("TableB"), 2); B->AddCol(bKey);
	auto root = Join2(A, B, aKey, bKey);                // multi-source join tree
	SpecBuf buf;
	buf.groupBy = { aDim };
	const ibDataQuerySpec spec = buf.Make(root, A);
	EXPECT_FALSE(ibDbTableProvider::CanPageGroupLevel(spec));   // a tree is not paged as one table: its groups fold whole
}

// The page is written `FROM <its table>` on the base's connection — so the provider of a source the base's statement
// cannot read pages nothing: one computed in memory (a register's balance would page its movements' table)… (That
// the composer asks the source's own provider is QueryComposerRemote's, GroupLevelPage_OfItsTable_PagedByIt.)
TEST(QueryComposerGate, GroupLevelPage_ComputedSource_NotPageable)
{
	const std::shared_ptr<const ibBackendColumnRawDB> dim = ibBackendColumnRawDB::String(wxT("code"));
	const auto C = std::make_shared<TestQueryable>(wxT("Slice"), 1, /*computed*/ true); C->AddCol(dim);
	SpecBuf buf;
	buf.groupBy = { dim };
	const ibDataQuerySpec spec = buf.Make(nullptr, C);
	ibDataQueryResult page = ibMakeEmptyQueryResult();
	EXPECT_FALSE(spec.m_queryable->GetProvider().TryPageGroupLevel(spec, ibReadPageRequest{}, page));
}

// …and one of another connection.
TEST(QueryComposerGate, GroupLevelPage_ForeignSource_NotPageable)
{
	const std::shared_ptr<const ibBackendColumnRawDB> dim = ibBackendColumnRawDB::String(wxT("code"));
	const auto F = std::make_shared<ForeignQueryable>(wxT("Remote"), 1); F->AddCol(dim);
	SpecBuf buf;
	buf.groupBy = { dim };
	const ibDataQuerySpec spec = buf.Make(nullptr, F);
	ibDataQueryResult page = ibMakeEmptyQueryResult();
	EXPECT_FALSE(spec.m_queryable->GetProvider().TryPageGroupLevel(spec, ibReadPageRequest{}, page));
}

TEST(QueryComposerGate, GroupLevelPage_ReferenceDim_NotPageable)
{
	// A REFERENCE dimension is NOT a plain scalar: its value is a multi-field spread (TYPE + _RTRef + _RRRef).
	// Keyset-paging by it would drop the TYPE field the read reconstructs from ("field <col>_TYPE not found")
	// and has no meaningful keyset order (its id, not its presentation) -> the gate rejects it, routing to the
	// unpaged full-spread ExecuteAggregate. Regression guard for the "grouping a table by a reference crashes".
	const auto dim = std::make_shared<TestCol>(wxT("counterparty"), 2);
	dim->GetTypeDesc() = ibTypeDescription(reference_to_clsid(999));   // reference-typed dimension (spread > 1 field)
	const auto A = std::make_shared<TestQueryable>(wxT("doc"), 1); A->AddCol(dim);
	SpecBuf buf;
	buf.groupBy = { dim };
	const ibDataQuerySpec spec = buf.Make(nullptr, A);
	EXPECT_FALSE(ibDbTableProvider::CanPageGroupLevel(spec));
}

// ===========================================================================
// Routing gate — CanRollupTotalsShape (the STRUCTURAL half of the SINGLE-source ROLLUP push; the
// full CanPushRollupTotals adds the ROLLUP-dialect capability, integration scope).
// ===========================================================================

TEST(QueryComposerGate, RollupTotals_SingleSourceScalar_Shape)
{
	const std::shared_ptr<const ibBackendColumnRawDB> dim = ibBackendColumnRawDB::String(wxT("dim"));
	const std::shared_ptr<const ibBackendColumnRawDB> amt = ibBackendColumnRawDB::Number(wxT("amt"));
	const auto A = std::make_shared<TestQueryable>(wxT("TableA"), 1); A->AddCol(dim); A->AddCol(amt);

	SpecBuf buf;
	buf.groupBy = { dim };
	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn = ibDataQueryBuilder::AggregateFn::Sum; sum.m_col = amt; sum.m_alias = wxT("total");
	buf.aggs = { sum };
	const ibDataQuerySpec spec = buf.Make(nullptr, A);           // Source root (single source)

	EXPECT_TRUE(ibDbTableProvider::CanRollupTotalsShape(spec));
}

TEST(QueryComposerGate, RollupTotals_MultiSource_NotSingleShape)
{
	// A join TREE is not the single-source shape — the composer hands the fold the composite the join is read as
	// (ibQueryComposer::OverComposite), never the tree.
	const std::shared_ptr<const ibBackendColumnRawDB> aKey = ibBackendColumnRawDB::Number(wxT("a_key"));
	const std::shared_ptr<const ibBackendColumnRawDB> aDim = ibBackendColumnRawDB::String(wxT("a_dim"));
	const std::shared_ptr<const ibBackendColumnRawDB> bKey = ibBackendColumnRawDB::Number(wxT("b_key"));
	const std::shared_ptr<const ibBackendColumnRawDB> bAmt = ibBackendColumnRawDB::Number(wxT("b_amt"));
	const auto A = std::make_shared<TestQueryable>(wxT("TableA"), 1); A->AddCol(aKey); A->AddCol(aDim);
	const auto B = std::make_shared<TestQueryable>(wxT("TableB"), 2); B->AddCol(bKey); B->AddCol(bAmt);

	auto root = Join2(A, B, aKey, bKey);
	SpecBuf buf;
	buf.groupBy = { aDim };
	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn = ibDataQueryBuilder::AggregateFn::Sum; sum.m_col = bAmt; sum.m_alias = wxT("total");
	buf.aggs = { sum };
	const ibDataQuerySpec spec = buf.Make(root, A);

	EXPECT_FALSE(ibDbTableProvider::CanRollupTotalsShape(spec));   // join tree -> not single-source
}

TEST(QueryComposerGate, RollupTotals_SingleComputed_NotFolded)
{
	// The provider of a computed single source (a register slice) folds nothing on a server — its rows are read in a
	// base in memory and folded there (BuildTotalsTree).
	const std::shared_ptr<const ibBackendColumnRawDB> dim = ibBackendColumnRawDB::String(wxT("dim"));
	const std::shared_ptr<const ibBackendColumnRawDB> amt = ibBackendColumnRawDB::Number(wxT("amt"));
	const auto C = std::make_shared<TestQueryable>(wxT("Slice"), 1, /*computed*/ true); C->AddCol(dim); C->AddCol(amt);

	SpecBuf buf;
	buf.groupBy = { dim };
	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn = ibDataQueryBuilder::AggregateFn::Sum; sum.m_col = amt; sum.m_alias = wxT("total");
	buf.aggs = { sum };
	const ibDataQuerySpec spec = buf.Make(nullptr, C);

	ibSelectorTree folded;
	EXPECT_FALSE(spec.m_queryable->GetProvider().TryFoldTotalsInDbms(spec, folded));   // computed source -> folded in memory
}

TEST(QueryComposerGate, RollupTotals_SingleForeign_NotFolded)
{
	// The provider of one of another connection folds nothing either — never our ROLLUP over a table the base does
	// not have.
	const std::shared_ptr<const ibBackendColumnRawDB> dim = ibBackendColumnRawDB::String(wxT("dim"));
	const std::shared_ptr<const ibBackendColumnRawDB> amt = ibBackendColumnRawDB::Number(wxT("amt"));
	const auto F = std::make_shared<ForeignQueryable>(wxT("Remote"), 1); F->AddCol(dim); F->AddCol(amt);

	SpecBuf buf;
	buf.groupBy = { dim };
	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn = ibDataQueryBuilder::AggregateFn::Sum; sum.m_col = amt; sum.m_alias = wxT("total");
	buf.aggs = { sum };
	const ibDataQuerySpec spec = buf.Make(nullptr, F);

	ibSelectorTree folded;
	EXPECT_FALSE(spec.m_queryable->GetProvider().TryFoldTotalsInDbms(spec, folded));   // its provider folds no totals -> folded in memory
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
	auto add = [&](const ibString& key, const ibString& parent, long amt) {
		const long r = d.AppendRow();
		d.SetCell(r, KEY_ID,    ibValue(key));
		d.SetCell(r, HPARENT, parent.IsEmpty() ? ibValue() : ibValue(parent));
		d.SetCell(r, HAMT,    ibValue(ibNumber(amt)));
	};
	add(wxT("1"), ibString(), 0);   // F1 folder
	add(wxT("2"), wxT("1"), 10);    // E1
	add(wxT("3"), wxT("1"), 0);     // F2 folder
	add(wxT("4"), wxT("3"), 5);     // E2
	add(wxT("5"), ibString(), 7);   // E3 top element
	return d;
}

ibDataQueryBuilder::AggregateItem SumOf(const std::shared_ptr<const ibBackendQueryColumn>& col)
{
	ibDataQueryBuilder::AggregateItem a;
	a.m_fn = ibDataQueryBuilder::AggregateFn::Sum; a.m_col = col; a.m_alias = wxT("total");
	return a;
}

} // namespace

TEST(QueryHierarchy, Hierarchy_TreeSubtotalsHasChildren)
{
	const auto keyCol    = std::make_shared<TestCol>(wxT("key"), KEY_ID);
	const auto parentCol = std::make_shared<TestCol>(wxT("parent"), HPARENT);
	const auto amtCol    = std::make_shared<TestCol>(wxT("amount"), HAMT);
	const ibQueryRamTable d = HierDetail();
	const ibSelectorTree tree = BuildHierarchyTree(
		d, keyCol, parentCol, { SumOf(amtCol) }, ibDimensionKind::Hierarchy);

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
	const auto keyCol    = std::make_shared<TestCol>(wxT("key"), KEY_ID);
	const auto parentCol = std::make_shared<TestCol>(wxT("parent"), HPARENT);
	const auto amtCol    = std::make_shared<TestCol>(wxT("amount"), HAMT);
	const ibQueryRamTable d = HierDetail();
	const ibSelectorTree tree = BuildHierarchyTree(
		d, keyCol, parentCol, { SumOf(amtCol) }, ibDimensionKind::HierarchyOnly);

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
	const auto keyCol    = std::make_shared<TestCol>(wxT("key"), KEY_ID);
	const auto parentCol = std::make_shared<TestCol>(wxT("parent"), HPARENT);
	const auto amtCol    = std::make_shared<TestCol>(wxT("amount"), HAMT);
	const ibQueryRamTable d = HierDetail();
	const ibSelectorTree tree = BuildHierarchyTree(
		d, keyCol, parentCol, { SumOf(amtCol) }, ibDimensionKind::Elements);

	ASSERT_EQ(tree.Root().m_children.size(), 5u);                            // every row a leaf node, no nesting
	EXPECT_TRUE(tree.Root().m_children[0]->m_children.empty());             // leaves don't nest
	EXPECT_EQ(tree.Root().m_values.at(AGG0).GetString().ToStdString(), "22");
}

// ===========================================================================
// ibSelector — traversal façade: fold + mode over ONE snapshot, subtotals from it
// ===========================================================================

TEST(QuerySelector, Hierarchy_FoldsSnapshot_SubtotalsFromOneSnapshot)
{
	const auto keyCol    = std::make_shared<TestCol>(wxT("key"), KEY_ID);
	const auto parentCol = std::make_shared<TestCol>(wxT("parent"), HPARENT);
	const auto amtCol    = std::make_shared<TestCol>(wxT("amount"), HAMT);

	// Snapshot moves INTO the Selector; the fold + traversal kind live on the Selector, not the query.
	ibSelector sel(HierDetail(), ibSelectKind::ibSelectKind_ByGroupsHierarchy);
	sel.ByParentRef(keyCol, parentCol).Aggregating({ SumOf(amtCol) });
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
	const auto keyCol    = std::make_shared<TestCol>(wxT("key"), KEY_ID);
	const auto parentCol = std::make_shared<TestCol>(wxT("parent"), HPARENT);
	const auto amtCol    = std::make_shared<TestCol>(wxT("amount"), HAMT);

	ibSelector sel(HierDetail(), ibSelectKind::ibSelectKind_ByGroupsHierarchy);
	sel.ByParentRef(keyCol, parentCol).Aggregating({ SumOf(amtCol) });   // fold set, but NO WithSource

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
	const auto keyCol    = std::make_shared<TestCol>(wxT("key"), KEY_ID);
	const auto parentCol = std::make_shared<TestCol>(wxT("parent"), HPARENT);
	const auto amtCol    = std::make_shared<TestCol>(wxT("amount"), HAMT);
	ibSelector sel(HierDetail(), ibSelectKind::ibSelectKind_ByGroupsHierarchy);
	sel.ByParentRef(keyCol, parentCol).Aggregating({ SumOf(amtCol) });

	auto keysOf = [keyCol](ibSelector& s, std::vector<int>* levels = nullptr) {
		std::vector<std::string> out;
		while (s.Next()) {
			out.push_back(s.GetValue(keyCol).GetString().ToStdString());
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
	ASSERT_EQ(sel.GetValue(keyCol).GetString(), wxT("1"));
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
	ASSERT_EQ(underF1.GetValue(keyCol).GetString(), wxT("3"));
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
	const auto keyCol    = std::make_shared<TestCol>(wxT("key"), KEY_ID);
	const auto parentCol = std::make_shared<TestCol>(wxT("parent"), HPARENT);
	const auto amtCol    = std::make_shared<TestCol>(wxT("amount"), HAMT);
	ibSelector sel(HierDetail(), ibSelectKind::ibSelectKind_ByGroupsHierarchy);
	sel.ByParentRef(keyCol, parentCol).Aggregating({ SumOf(amtCol) });

	while (sel.Next()) {}
	EXPECT_FALSE(sel.Next());                               // still refuses, idempotently
	EXPECT_EQ(sel.GetValue(keyCol).GetString(), wxT("5")); // …and still stands on E3
}

// ===========================================================================
// A LEVEL'S OWN ORDER — the half of a level's settings nothing used to read.
//
// A report's window writes a sort onto the grouping a person selected, and the walk visited the
// headings in fold order regardless. The order belongs to the selection because the selection is
// what holds one level's nodes; a query's ORDER BY cannot say it — those rows were folded already.
// ===========================================================================

namespace {

const ibMetaID REGION_ID = 11, PRODUCT_ID = 12, GAMT_ID = 13;

// North(5) / South(10) / North(3) — two headings, and the smaller one arrives FIRST, so fold order
// and "biggest figure first" are different sequences and a test can tell them apart.
ibQueryRamTable GroupDetail()
{
	ibQueryRamTable d;
	d.AddColumn(REGION_ID,  wxT("region"),  kNoType);
	d.AddColumn(PRODUCT_ID, wxT("product"), kNoType);
	d.AddColumn(GAMT_ID,    wxT("amount"),  kNoType);
	auto add = [&](const ibString& region, const ibString& product, long amt) {
		const long r = d.AppendRow();
		d.SetCell(r, REGION_ID,  ibValue(region));
		d.SetCell(r, PRODUCT_ID, ibValue(product));
		d.SetCell(r, GAMT_ID,    ibValue(ibNumber(amt)));
	};
	add(wxT("North"), wxT("Apple"), 3);
	add(wxT("South"), wxT("Pear"),  10);
	add(wxT("North"), wxT("Pear"),  5);
	return d;
}

} // namespace

// Select(ByGroups) on a query that named no grouping. `SELECT 1 AS A` then
// Select(QueryResultIteration.ByGroups) used to die in BuildHierarchyTree: the
// walk was handed no totals, no group columns and no parent key, and the fold
// dereferenced the key. The records themselves are the walk, the same rows a
// direct selection hands out, and an empty result is an empty walk.
TEST(QuerySelector, ByGroupsWithoutAGroupingReadsTheRows)
{
	const ibMetaID A = 1;
	const auto twoRows = [&]() {
		ibQueryRamTable rows;
		rows.AddColumn(A, wxT("A"), ibTypeDescription());
		const auto add = [&](const ibString& v) {
			const long r = rows.AppendRow();
			rows.SetCell(r, A, ibValue(v));
		};
		add(wxT("1"));
		add(wxT("2"));
		return rows;
	};
	const auto walked = [](ibSelector& sel) {
		std::vector<ibString> got;
		while (sel.Next())
			got.push_back(sel.GetColumn(wxT("A")).GetString());
		return got;
	};

	ibSelector direct(twoRows(), ibSelectKind::ibSelectKind_Direct);
	const std::vector<ibString> asDirect = walked(direct);

	ibSelector groups(twoRows(), ibSelectKind::ibSelectKind_ByGroups);
	EXPECT_EQ(groups.NodeCount(), static_cast<long>(asDirect.size()));
	EXPECT_EQ(walked(groups), asDirect);

	ibSelector hier(twoRows(), ibSelectKind::ibSelectKind_ByGroupsHierarchy);
	EXPECT_EQ(hier.NodeCount(), static_cast<long>(asDirect.size()));
	EXPECT_EQ(walked(hier), asDirect);

	// The road result.Select takes: a cursor, and no totals configured on it.
	ibQueryRamTable held = twoRows();
	ibSelector fromRead(std::make_unique<ibRamTableCursor>(held), ibSelectKind::ibSelectKind_ByGroups);
	EXPECT_EQ(fromRead.NodeCount(), static_cast<long>(asDirect.size()));
	EXPECT_EQ(walked(fromRead), asDirect);

	ibQueryRamTable empty;
	empty.AddColumn(A, wxT("A"), ibTypeDescription());
	ibSelector none(std::move(empty), ibSelectKind::ibSelectKind_ByGroups);
	EXPECT_EQ(none.NodeCount(), 0);
	EXPECT_FALSE(none.Next());
}

TEST(QuerySelector, WithNoOrderStatedALevelKeepsFoldOrder)
{
	const auto regionCol = std::make_shared<TestCol>(wxT("region"), REGION_ID);
	const auto amtCol    = std::make_shared<TestCol>(wxT("amount"), GAMT_ID);
	ibSelector sel(GroupDetail(), ibSelectKind::ibSelectKind_ByGroups);
	sel.ByGroups({ regionCol }).Aggregating({ SumOf(amtCol) });

	ASSERT_TRUE(sel.Next());
	EXPECT_EQ(sel.GetValue(regionCol).GetString(), wxT("North"));   // the order the rows arrived in
}

// ⭐ SORTING A LEVEL BY ITS FIGURE — "the biggest customer first", the sort a report is most often
// given. The key is the aggregate's own column, read exactly as the walk reads it.
TEST(QuerySelector, ALevelHandsItsHeadingsOutInItsOwnOrder)
{
	const auto regionCol = std::make_shared<TestCol>(wxT("region"), REGION_ID);
	const auto amtCol    = std::make_shared<TestCol>(wxT("amount"), GAMT_ID);
	ibSelector sel(GroupDetail(), ibSelectKind::ibSelectKind_ByGroups);
	sel.ByGroups({ regionCol }).Aggregating({ SumOf(amtCol) });
	sel.OrderBy({ { amtCol, ibString(), /*ascending*/ false } });

	ASSERT_TRUE(sel.Next());
	EXPECT_EQ(sel.GetValue(regionCol).GetString(), wxT("South"));   // 10 …
	ASSERT_TRUE(sel.Next());
	EXPECT_EQ(sel.GetValue(regionCol).GetString(), wxT("North"));   // … before 8
	EXPECT_FALSE(sel.Next());
}

// ⚠ AND A KEY NO HEADING CARRIES ORDERS NOTHING. A group node holds the level's key and the figures
// rolled into it — `product` belongs to the rows, and a report without a details level has no row
// nodes at all. So this sort is a no-op HERE, and it is not a gap: a sort by an ordinary field is
// stated as the order the DETAIL is read in, and the group then stands where its first row does
// (dataComposer::AppendSettingsClauses). Pinned so the two roads cannot quietly both try.
TEST(QuerySelector, AKeyNoHeadingCarriesLeavesTheOrderAlone)
{
	const auto regionCol  = std::make_shared<TestCol>(wxT("region"), REGION_ID);
	const auto productCol = std::make_shared<TestCol>(wxT("product"), PRODUCT_ID);
	const auto amtCol     = std::make_shared<TestCol>(wxT("amount"), GAMT_ID);
	ibSelector sel(GroupDetail(), ibSelectKind::ibSelectKind_ByGroups);
	sel.ByGroups({ regionCol }).Aggregating({ SumOf(amtCol) });
	sel.OrderBy({ { productCol, ibString(), /*ascending*/ false } });

	ASSERT_TRUE(sel.Next());
	EXPECT_EQ(sel.GetValue(regionCol).GetString(), wxT("North"));   // fold order, untouched
}

// ⭐ …AND IT STOPS AT THE LEVEL THAT STATED IT. A descent is the NEXT level, which has an order of
// its own or none — inheriting would arrange it by a key its author wrote one level up.
TEST(QuerySelector, AnOrderIsNotInheritedByADescent)
{
	const auto regionCol  = std::make_shared<TestCol>(wxT("region"), REGION_ID);
	const auto productCol = std::make_shared<TestCol>(wxT("product"), PRODUCT_ID);
	const auto amtCol     = std::make_shared<TestCol>(wxT("amount"), GAMT_ID);
	ibSelector sel(GroupDetail(), ibSelectKind::ibSelectKind_ByGroups);
	sel.ByGroups({ regionCol, productCol }).Aggregating({ SumOf(amtCol) });
	sel.OrderBy({ { amtCol, ibString(), /*ascending*/ false } });

	ASSERT_TRUE(sel.Next());
	EXPECT_EQ(sel.GetValue(regionCol).GetString(), wxT("South"));   // the stated order holds here…

	ASSERT_TRUE(sel.Next());
	EXPECT_EQ(sel.GetValue(regionCol).GetString(), wxT("North"));
	ibSelector under = sel.Select(ibSelectKind::ibSelectKind_ByGroups);
	// …and North's products come in FOLD order: Apple(3) arrived first, Pear(5) second. The parent's
	// key would have put Pear first, so the two sequences genuinely disagree — which is the only way
	// this test can tell inheriting from not inheriting.
	ASSERT_TRUE(under.Next());
	EXPECT_EQ(under.GetValue(productCol).GetString(), wxT("Apple"));
	ASSERT_TRUE(under.Next());
	EXPECT_EQ(under.GetValue(productCol).GetString(), wxT("Pear"));
}

// ===========================================================================
// SyntheticId — THE NUMBER A COLUMN NOBODY DECLARED GOES BY
//
// A row is a map keyed by these (queryRamTable.h), so two columns that come out with one number
// share one cell and the second silently overwrites the first. The hard case is DEPTH: a third
// reading of one table wraps the second, so one number is a chain of stamps over a body — and it
// must not read as a shorter chain over a different body. It used to: `|value|` dropped the sign,
// which was the only thing saying where the body ended (`Alias(Alias(col 2)) == Alias(col 18)`).
// ===========================================================================

TEST(SyntheticId, ChainsAndKindsNeverCollide)
{
	using Col  = ibBackendQueryColumn;
	using Kind = ibBackendQueryColumn::SyntheticKind;

	std::map<ibMetaID, std::string> seen;
	const auto claim = [&](ibMetaID id, const std::string& who) {
		EXPECT_TRUE(Col::IsSyntheticId(id)) << who << " is not negative: " << id;
		const auto it = seen.find(id);
		if (it != seen.end() && it->second != who)
			ADD_FAILURE() << "id " << id << ": " << who << " == " << it->second;
		seen.emplace(id, who);
	};

	// Every kind over an ordinary ordinal — how all but the twin mint theirs. The TWIN is left to the
	// loop below, which covers its depth 1 as well: naming one and the same minting twice would make
	// the ledger report a collision with itself.
	for (unsigned k = 1; k <= 6; ++k) {
		if (k == static_cast<unsigned>(Kind::Alias))
			continue;
		for (ibMetaID i = 0; i <= 3000; ++i)
			claim(Col::SyntheticId(static_cast<Kind>(k), i),
			      "kind " + std::to_string(k) + "(" + std::to_string(i) + ")");
	}

	// …and the twin chains: a second reading over a column, a third over the second, and further.
	for (ibMetaID body = 0; body <= 3000; ++body) {
		ibMetaID id = body;
		for (int depth = 1; depth <= 4; ++depth) {
			id = Col::SyntheticId(Kind::Alias, id);
			claim(id, "Alias^" + std::to_string(depth) + "(col " + std::to_string(body) + ")");
		}
	}
}

// The exact pair that used to land on one number, kept as itself: reading a table a third time
// against reading another of its columns a second time.
TEST(SyntheticId, ADeeperChainIsNotAShallowerOneOverAnotherColumn)
{
	using Col  = ibBackendQueryColumn;
	using Kind = ibBackendQueryColumn::SyntheticKind;

	EXPECT_NE(Col::SyntheticId(Kind::Alias, Col::SyntheticId(Kind::Alias, 2)),
	          Col::SyntheticId(Kind::Alias, 18));
	EXPECT_NE(Col::SyntheticId(Kind::Alias, Col::SyntheticId(Kind::Alias, 1)),
	          Col::SyntheticId(Kind::Alias, 10));
}

// ⚠ THE CEILING IS ANSWERED BEFORE IT IS PASSED. An id is an `int` and every stamp multiplies it, so
// a chain runs out — and an overflow would wrap the number into the positive half, where it reads as
// a declared attribute's metaID. The guard is asked, so the chain ends by refusal, never by wrapping.
TEST(SyntheticId, TheCeilingIsAskedRatherThanMet)
{
	using Col  = ibBackendQueryColumn;
	using Kind = ibBackendQueryColumn::SyntheticKind;

	ibMetaID id = 10000;            // an ordinary attribute number, on the large side
	int depth = 0;
	while (Col::CanComposeSyntheticId(id)) {
		id = Col::SyntheticId(Kind::Alias, id);
		EXPECT_TRUE(Col::IsSyntheticId(id)) << "wrapped at depth " << depth;
		ASSERT_LT(++depth, 12);     // it must end by the guard, not by running away
	}
	EXPECT_GE(depth, 4);            // …and the room is real: four readings of one table, and deeper
}

// ===========================================================================
// ibCteQueryable — WHAT A DECLARED QUERY PUBLISHES
//
// Most of it is minted: one field per output, named by the alias the select wrote. A SYNTHETIC
// column is the exception — the document's MOMENT has no field of its own, so it is handed over as
// it stands and reads itself out of the fields its parts wrote.
// ===========================================================================

TEST(CteQueryable, MintsAFieldPerOutput)
{
	std::vector<ibCteQueryable::Field> fields(1);
	fields[0].m_name     = wxT("Date");
	fields[0].m_physical = wxT("fld1025");

	std::shared_ptr<const ibBackendQueryColumn> date;
	{
		const ibCteQueryable cte(wxT("q_sub0"), fields, 0);   // the ORDINAL — the kind is stamped on inside
		ASSERT_EQ(cte.GetQueryColumns().size(), 1u);
		date = cte.ResolveColumnByName(wxT("Date"));
	}
	ASSERT_NE(date, nullptr);                         // handed out held, so it outlives the declaration that minted it
	EXPECT_EQ(date->GetPhysicalName(), wxT("fld1025"));
	// …and the id it minted is SYNTHETIC — a declared attribute's metaID is positive, and a column
	// nobody declared must never be mistaken for one (this is what `id++` over a composed base broke).
	EXPECT_TRUE(ibBackendQueryColumn::IsSyntheticId(date->GetColumnId()));
}

// ⭐ AND A WHOLE DECLARATION'S WORTH OF THEM STAYS SYNTHETIC AND DISTINCT. Ten fields is where the
// old numbering went wrong: it counted UP from a composed base, so the seventh field onwards came
// out positive — a declared attribute's number.
TEST(CteQueryable, EveryMintedIdIsSyntheticAndDistinct)
{
	std::vector<ibCteQueryable::Field> fields(10);
	for (size_t i = 0; i < fields.size(); ++i)
		fields[i].m_name = ibString::Format(wxT("F%u"), static_cast<unsigned>(i));

	const auto cte = std::make_shared<ibCteQueryable>(wxT("q_sub0"), fields, 0);
	ASSERT_EQ(cte->GetQueryColumns().size(), 10u);
	std::set<ibMetaID> ids;
	for (const std::shared_ptr<const ibBackendQueryColumn>& c : cte->GetQueryColumns()) {
		EXPECT_TRUE(ibBackendQueryColumn::IsSyntheticId(c->GetColumnId())) << c->GetName();
		ids.insert(c->GetColumnId());
	}
	EXPECT_EQ(ids.size(), 10u);                       // ten fields, ten different cells in a row
}

// ⭐ A BORROWED COLUMN IS THE SOURCE'S OWN — same pointer, same id, same layout. This is how the
// MOMENT survives a declaration: it is not written into the SELECT (its fields are the date's and
// the reference's, already there), but it IS named from outside, which is what a report groups by.
TEST(CteQueryable, PublishesABorrowedColumnAsItself)
{
	const auto moment = std::make_shared<TestCol>(wxT("PointInTime"), 77);
	std::vector<ibCteQueryable::Field> fields(2);
	fields[0].m_name     = wxT("Date");
	fields[0].m_physical = wxT("fld1025");
	fields[1].m_name     = wxT("PointInTime");
	fields[1].m_borrowed = moment;

	const auto cte = std::make_shared<ibCteQueryable>(wxT("q_sub0"), fields, 0);
	ASSERT_EQ(cte->GetQueryColumns().size(), 2u);
	EXPECT_EQ(cte->ResolveColumnByName(wxT("PointInTime")), moment);   // the very column, not a copy of its name
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
	const auto aKey = std::make_shared<TestCol>(wxT("k"), AK);
	const auto bKey = std::make_shared<TestCol>(wxT("k"), BK);
	const auto bVal = std::make_shared<TestCol>(wxT("v"), BV);

	const auto A = std::make_shared<RecordingComputedQ>(wxT("a"), 100); A->AddCol(aKey);
	const auto B = std::make_shared<RecordingComputedQ>(wxT("b"), 101); B->AddCol(bKey); B->AddCol(bVal);
	A->SetRows([&] { return KeyRows(AK, wxT("k"), { 1, 2, 3, 2 }); });   // 2 twice — the set must dedup
	B->SetRows([&] {
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
	q.From(A).Join(B, aKey, bKey);
	q.Select(bVal, wxT("v"));
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	int rows = 0;
	while (res.Next()) ++rows;
	EXPECT_EQ(rows, 3);   // A holds 1,2,3,2 and B holds 2,3,4 -> keys 2 (twice) and 3

	ASSERT_EQ(B->Seen().size(), 1u) << "the right side should have been handed exactly one reduction";
	EXPECT_TRUE(B->Seen()[0].m_op == ibQueryFilterOp::In);
	EXPECT_EQ(B->Seen()[0].m_col, bKey) << "the condition must name the RIGHT side's key column";
	EXPECT_EQ(B->Seen()[0].m_values.size(), 3u) << "distinct keys of the left side: 1, 2, 3";
	EXPECT_TRUE(A->Seen().empty()) << "the driving side is read first and receives nothing";
}

// THE correctness gate. `A LEFT JOIN B` preserves every A row, so the reduction may only ever travel
// left -> right. If the direction were flipped, A would lose its unmatched rows and this fails.
TEST(QuerySemiJoin, LeftJoin_KeepsUnmatchedLeftRows)
{
	const ibMetaID AK = 1, BK = 2, BV = 3;
	const auto aKey = std::make_shared<TestCol>(wxT("k"), AK);
	const auto bKey = std::make_shared<TestCol>(wxT("k"), BK);
	const auto bVal = std::make_shared<TestCol>(wxT("v"), BV);

	const auto A = std::make_shared<RecordingComputedQ>(wxT("a"), 110); A->AddCol(aKey);
	const auto B = std::make_shared<RecordingComputedQ>(wxT("b"), 111); B->AddCol(bKey); B->AddCol(bVal);
	A->SetRows([&] { return KeyRows(AK, wxT("k"), { 1, 2 }); });
	B->SetRows([&] {
		ibQueryRamTable t;
		t.AddColumn(BK, wxT("k"), kNoType);
		t.AddColumn(BV, wxT("v"), kNoType);
		const long r = t.AppendRow();
		t.SetCell(r, BK, ibValue(ibNumber(2)));
		t.SetCell(r, BV, ibValue(ibNumber(20)));
		return t;
	});

	ibDataQueryBuilder q(nullptr);
	q.From(A).Join(B, aKey, bKey, ibQueryJoinKind::Left);
	q.Select(aKey, wxT("k"));
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	int rows = 0;
	while (res.Next()) ++rows;
	EXPECT_EQ(rows, 2) << "key 1 has no match and MUST survive a LEFT join";

	ASSERT_EQ(B->Seen().size(), 1u) << "left -> right is the sound direction and stays enabled for LEFT";
	EXPECT_TRUE(B->Seen()[0].m_op == ibQueryFilterOp::In);
}

// A THETA ON (a.k > b.k) cannot be reduced by an equality key set — knowing which keys the left holds
// says nothing about which right rows satisfy `>`. The gate must leave it alone.
TEST(QuerySemiJoin, ThetaJoin_IsNotReduced)
{
	const ibMetaID AK = 1, BK = 2;
	const auto aKey = std::make_shared<TestCol>(wxT("k"), AK);
	const auto bKey = std::make_shared<TestCol>(wxT("k"), BK);

	const auto A = std::make_shared<RecordingComputedQ>(wxT("a"), 120); A->AddCol(aKey);
	const auto B = std::make_shared<RecordingComputedQ>(wxT("b"), 121); B->AddCol(bKey);
	A->SetRows([&] { return KeyRows(AK, wxT("k"), { 5 }); });
	B->SetRows([&] { return KeyRows(BK, wxT("k"), { 1, 2, 9 }); });

	ibDataQueryBuilder q(nullptr);
	q.From(A).Join(B, aKey, bKey, ibJoinCompareOp::Gt);
	q.Select(bKey, wxT("k"));
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	int rows = 0;
	while (res.Next()) ++rows;
	EXPECT_EQ(rows, 2);   // 5 > 1 and 5 > 2, not 5 > 9

	EXPECT_TRUE(B->Seen().empty()) << "an equality key set must not be pushed into a theta join";
}

// An empty driving side yields no keys, so nothing is pushed — the join is empty anyway and the
// reduction must not manufacture a filter (an empty IN would be correct here, but the plain path is
// simpler and the result is the same; this pins that choice).
TEST(QuerySemiJoin, EmptyDrivingSidePushesNothing)
{
	const ibMetaID AK = 1, BK = 2;
	const auto aKey = std::make_shared<TestCol>(wxT("k"), AK);
	const auto bKey = std::make_shared<TestCol>(wxT("k"), BK);

	const auto A = std::make_shared<RecordingComputedQ>(wxT("a"), 130); A->AddCol(aKey);
	const auto B = std::make_shared<RecordingComputedQ>(wxT("b"), 131); B->AddCol(bKey);
	A->SetRows([&] { return KeyRows(AK, wxT("k"), {}); });
	B->SetRows([&] { return KeyRows(BK, wxT("k"), { 1, 2 }); });

	ibDataQueryBuilder q(nullptr);
	q.From(A).Join(B, aKey, bKey);
	q.Select(bKey, wxT("k"));
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	int rows = 0;
	while (res.Next()) ++rows;
	EXPECT_EQ(rows, 0);
	EXPECT_TRUE(B->Seen().empty());
}

// A chain of three: each source is reduced by the sources already read, so the narrowing is transitive down
// the chain.
TEST(QuerySemiJoin, InnerChainOfThree_ReducesDownTheChain)
{
	const ibMetaID AK = 1, BK = 2, CK = 3;
	const auto aKey = std::make_shared<TestCol>(wxT("k"), AK);
	const auto bKey = std::make_shared<TestCol>(wxT("k"), BK);
	const auto cKey = std::make_shared<TestCol>(wxT("k"), CK);

	const auto A = std::make_shared<RecordingComputedQ>(wxT("a"), 140); A->AddCol(aKey);
	const auto B = std::make_shared<RecordingComputedQ>(wxT("b"), 141); B->AddCol(bKey);
	const auto C = std::make_shared<RecordingComputedQ>(wxT("c"), 142); C->AddCol(cKey);
	A->SetRows([&] { return KeyRows(AK, wxT("k"), { 1, 2 }); });
	B->SetRows([&] { return KeyRows(BK, wxT("k"), { 2, 3 }); });
	C->SetRows([&] { return KeyRows(CK, wxT("k"), { 2, 3, 4 }); });

	ibDataQueryBuilder q(nullptr);
	q.From(A).Join(B, aKey, bKey).Join(C, bKey, cKey);
	q.Select(cKey, wxT("k"));
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	int rows = 0;
	while (res.Next()) ++rows;
	EXPECT_EQ(rows, 1);   // only key 2 is in all three

	ASSERT_EQ(B->Seen().size(), 1u);
	EXPECT_TRUE(B->Seen()[0].m_op == ibQueryFilterOp::In);
	EXPECT_EQ(B->Seen()[0].m_values.size(), 2u) << "A's keys 1, 2";

	ASSERT_EQ(C->Seen().size(), 1u) << "the second edge reduces C by what B holds";
	EXPECT_TRUE(C->Seen()[0].m_op == ibQueryFilterOp::In);
	EXPECT_EQ(C->Seen()[0].m_col, cKey);

	EXPECT_TRUE(A->Seen().empty()) << "the head of the chain is read first";
}

// ---- the advisory contract of ComputeRows(extra) --------------------------------------
//
// Every register compute takes `extra` and drops it (`ComputeRows(const std::vector<ibQueryCondition>&
// /*extra*/)` — balance / turnover / slice). That was invisible while `extra` was always empty; the
// semi-join reduction now pushes a real filter through it. So the STATEMENT over the source's copy enforces:
// whatever the source chose to ignore is applied over the rows it returned (ibQueryComposer::OverMemorySpace).
// These pin that, on a single source — no join, so nothing else could be doing the filtering.

TEST(QueryComputedFilter, IgnoredConditionIsStillEnforced)
{
	const ibMetaID K = 1;
	const auto key = std::make_shared<TestCol>(wxT("k"), K);
	const auto src = std::make_shared<RecordingComputedQ>(wxT("s"), 150);
	src->AddCol(key);
	src->SetRows([&] { return KeyRows(K, wxT("k"), { 1, 2, 3, 4 }); });   // ignores whatever it is handed

	ibDataQueryBuilder q(nullptr);
	q.From(src);
	q.Select(key, wxT("k"));
	q.WhereIn(key, { ibValue(ibNumber(2)), ibValue(ibNumber(4)) });
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	int rows = 0;
	while (res.Next()) ++rows;
	EXPECT_EQ(rows, 2) << "the source returned all four rows; the provider must apply the filter it dropped";

	ASSERT_EQ(src->Seen().size(), 1u) << "the condition is still HANDED down (a source may push it deeper)";
	EXPECT_TRUE(src->Seen()[0].m_op == ibQueryFilterOp::In);
}

// The scalar ops go through the same enforcement — this is not an `In`-only patch.
TEST(QueryComputedFilter, IgnoredEqualityIsStillEnforced)
{
	const ibMetaID K = 1;
	const auto key = std::make_shared<TestCol>(wxT("k"), K);
	const auto src = std::make_shared<RecordingComputedQ>(wxT("s"), 151);
	src->AddCol(key);
	src->SetRows([&] { return KeyRows(K, wxT("k"), { 1, 2, 3 }); });

	ibDataQueryBuilder q(nullptr);
	q.From(src);
	q.Select(key, wxT("k"));
	q.Where(key, ibValue(ibNumber(2)));
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
	const auto key    = std::make_shared<TestCol>(wxT("k"), K);
	const auto absent = std::make_shared<TestCol>(wxT("absent"), OTHER);
	const auto src = std::make_shared<RecordingComputedQ>(wxT("s"), 152);
	src->AddCol(key);
	src->AddCol(absent);                                       // declared on the source...
	src->SetRows([&] { return KeyRows(K, wxT("k"), { 1, 2 }); });   // ...but NOT in the rows it returns

	ibDataQueryBuilder q(nullptr);
	q.From(src);
	q.Select(key, wxT("k"));
	q.Where(absent, ibValue(ibNumber(7)));
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	int rows = 0;
	while (res.Next()) ++rows;
	EXPECT_EQ(rows, 2) << "an unvended column is the source's business; enforcement must skip it";
}

// A question put to a value — PRESENTATION here — has no SQL. Over a source computed in memory it is answered over the
// source's rows before they are copied into the base in memory, whose statement could only refuse it.
TEST(QueryComputedFilter, AQuestionPutToTheValueIsAnsweredOverTheRows)
{
	const ibMetaID K = 1;
	const auto key = std::make_shared<TestCol>(wxT("k"), K);
	const auto src = std::make_shared<RecordingComputedQ>(wxT("s"), 153);
	src->AddCol(key);
	src->SetRows([&] { return KeyRows(K, wxT("k"), { 1, 2, 3 }); });

	ibDataQueryBuilder q(nullptr);
	q.From(src);
	q.Select(key, wxT("k"));
	q.WhereExpr(ibQueryColumnExpr::ValueAsk(ibQueryColumnExpr::Col(key), ibQueryValueAsk::Presentation),
		ibQueryFilterOp::Equal, ibValue(ibString(wxT("2"))));
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	std::vector<long> keys;
	while (res.Next())
		keys.push_back(res.GetValue(key).GetInteger());
	EXPECT_EQ(keys, std::vector<long>({ 2 }));
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
	const std::shared_ptr<const ibBackendColumnRawDB> dim = ibBackendColumnRawDB::String(wxT("code"));
	const std::shared_ptr<const ibBackendColumnRawDB> amt = ibBackendColumnRawDB::Number(wxT("amt"));
	const auto A = std::make_shared<TestQueryable>(wxT("TableA"), 1); A->AddCol(dim); A->AddCol(amt);

	SpecBuf buf;
	buf.groupBy = { dim };                                  // one plain scalar grouping level…
	ibDataQueryBuilder::AggregateItem sum;
	sum.m_fn = ibDataQueryBuilder::AggregateFn::Sum; sum.m_col = amt; sum.m_alias = wxT("total");
	buf.aggs = { sum };                                      // …and a figure on it
	const ibDataQuerySpec spec = buf.Make(nullptr, A);

	EXPECT_TRUE(ibDbTableProvider::CanPageGroupLevel(spec));
}

// (No sibling test for the COMPUTED-select case: `SpecBuf` does not populate `m_selectExprs`, so the
//  gate's clause for it cannot be reached from this harness without widening the harness itself. The
//  router above refuses that shape on its own — a measure over a projection's name goes to the fold.)

// ---- ORDER BY over the groups a computed source folds in memory ------------------------------------
//
// A computed source grouped in memory answered its groups in the order its rows came: `GROUP BY m ORDER BY m`
// over a register's reading (ActualActionPeriod, ScheduleData) came back unsorted while the same query over a
// table was sorted, and TOP n kept the first n groups MET rather than the first n in order (2026-09-17). The fold
// orders its groups as the SQL road does — by a group key, or by a total's name — and caps them after.

namespace {

// (month, amount) rows in an order no sort gives: the months out of turn, several rows to a month.
ibQueryRamTable MonthRows(ibMetaID month, ibMetaID amount)
{
	ibQueryRamTable t;
	t.AddColumn(month, wxT("m"), kNoType);
	t.AddColumn(amount, wxT("a"), kNoType);
	const std::vector<std::pair<long, long>> rows = { { 3, 10 }, { 1, 5 }, { 2, 40 }, { 3, 1 }, { 1, 7 }, { 4, 2 } };
	for (const std::pair<long, long>& row : rows) {
		const long r = t.AppendRow();
		t.SetCell(r, month, ibValue(ibNumber(row.first)));
		t.SetCell(r, amount, ibValue(ibNumber(row.second)));
	}
	return t;
}

} // namespace

TEST(QueryComputedAggregate, GroupsComeInTheOrderAskedFor)
{
	const ibMetaID M = 1, A = 2;
	const auto month  = std::make_shared<TestCol>(wxT("m"), M);
	const auto amount = std::make_shared<TestCol>(wxT("a"), A);
	const auto src = std::make_shared<RecordingComputedQ>(wxT("s"), 160);
	src->AddCol(month);
	src->AddCol(amount);
	src->SetRows([&] { return MonthRows(M, A); });

	for (const bool ascending : { true, false }) {
		ibDataQueryBuilder q(nullptr);
		q.From(src);
		q.GroupBy(month);
		q.Sum(amount, wxT("total"));
		q.OrderBy(month, ascending);
		ibDataQueryResult res = q.SelectAggregate();

		std::vector<long> months;
		while (res.Next())
			months.push_back(res.GetValue(month).GetInteger());
		EXPECT_EQ(ascending ? std::vector<long>({ 1, 2, 3, 4 }) : std::vector<long>({ 4, 3, 2, 1 }), months);
	}
}

TEST(QueryComputedAggregate, TopTakesTheFirstGroupsInTheirOrder)
{
	const ibMetaID M = 1, A = 2;
	const auto month  = std::make_shared<TestCol>(wxT("m"), M);
	const auto amount = std::make_shared<TestCol>(wxT("a"), A);
	const auto src = std::make_shared<RecordingComputedQ>(wxT("s"), 161);
	src->AddCol(month);
	src->AddCol(amount);
	src->SetRows([&] { return MonthRows(M, A); });   // totals: month 1 = 12, 2 = 40, 3 = 11, 4 = 2

	ibDataQueryBuilder q(nullptr);
	q.From(src);
	q.GroupBy(month);
	q.Sum(amount, wxT("total"));
	q.OrderByOutput(wxT("total"), /*ascending*/ false);
	q.Top(2);
	ibDataQueryResult res = q.SelectAggregate();

	std::vector<long> months;
	std::vector<ibNumber> totals;
	while (res.Next()) {
		months.push_back(res.GetValue(month).GetInteger());
		totals.push_back(res.GetColumn(wxT("total")).GetNumber());
	}
	EXPECT_EQ(std::vector<long>({ 2, 1 }), months) << "the two biggest months, biggest first — not the first two met";
	ASSERT_EQ(2u, totals.size());
	EXPECT_EQ(ibNumber(40), totals[0]);
	EXPECT_EQ(ibNumber(12), totals[1]);
}

// HAVING over groups folded in memory answers in the database's three values: a group whose figure is NULL (a sum of
// rows that all have no value) is UNKNOWN to `<> 40` and goes, as the database's HAVING drops it. The fold compared in
// two values and kept it.
TEST(QueryComputedAggregate, HavingDropsAGroupWhoseFigureIsNull)
{
	const ibMetaID M = 1, A = 2;
	const auto month  = std::make_shared<TestCol>(wxT("m"), M);
	const auto amount = std::make_shared<TestCol>(wxT("a"), A);
	const auto src = std::make_shared<RecordingComputedQ>(wxT("s"), 162);
	src->AddCol(month);
	src->AddCol(amount);
	src->SetRows([&] {                                   // totals: month 1 = 12, 2 = 40, 3 = 11, 4 = 2, 5 = NULL
		ibQueryRamTable t = MonthRows(M, A);
		const long r = t.AppendRow();
		t.SetCell(r, M, ibValue(ibNumber(5)));
		t.SetCell(r, A, ibValue(ibValueTypes::TYPE_NULL));
		return t;
	});

	ibDataQueryBuilder q(nullptr);
	q.From(src);
	q.GroupBy(month);
	q.Sum(amount, wxT("total"));
	q.Having(ibDataQueryBuilder::AggregateFn::Sum, amount, ibQueryFilterOp::NotEqual, ibValue(ibNumber(40)));
	q.OrderBy(month, /*ascending*/ true);
	ibDataQueryResult res = q.SelectAggregate();

	std::vector<long> months;
	while (res.Next())
		months.push_back(res.GetValue(month).GetInteger());
	EXPECT_EQ(std::vector<long>({ 1, 3, 4 }), months) << "month 2 is 40; month 5 is NULL, which is not 'not 40'";
}

// ---- a composition sorted and paged in memory ------------------------------------------------------------------
//
// Two sources no statement of the base can join are read in a base in memory, and its statement sorts and pages them
// as a server's sorts and pages a table. Composed by hand, they were sorted apart: a key with no column was skipped,
// and the page was the first rows whatever the anchor said.

namespace {

// A ⋈ B on k, both computed: k = 1..5 on each side, B carrying v = 10 * k.
struct ComposedPair {
	explicit ComposedPair(ibMetaID base) {
		A = std::make_shared<RecordingComputedQ>(wxT("a"), base);     A->AddCol(aKey);
		B = std::make_shared<RecordingComputedQ>(wxT("b"), base + 1); B->AddCol(bKey); B->AddCol(bVal);
		A->SetRows([] { return KeyRows(1, wxT("k"), { 1, 2, 3, 4, 5 }); });
		B->SetRows([] {
			ibQueryRamTable t;
			t.AddColumn(2, wxT("k"), kNoType);
			t.AddColumn(3, wxT("v"), kNoType);
			for (const long k : { 1, 2, 3, 4, 5 }) {
				const long r = t.AppendRow();
				t.SetCell(r, 2, ibValue(ibNumber(k)));
				t.SetCell(r, 3, ibValue(ibNumber(k * 10)));
			}
			return t;
		});
	}
	const std::shared_ptr<TestCol> aKey = std::make_shared<TestCol>(wxT("k"), 1);
	const std::shared_ptr<TestCol> bKey = std::make_shared<TestCol>(wxT("k"), 2);
	const std::shared_ptr<TestCol> bVal = std::make_shared<TestCol>(wxT("v"), 3);
	std::shared_ptr<RecordingComputedQ> A, B;
};

std::vector<long> KeysRead(ibDataQueryResult& res, const std::shared_ptr<TestCol>& key)
{
	std::vector<long> keys;
	while (res.Next())
		keys.push_back(res.GetValue(key).GetInteger());
	return keys;
}

} // namespace

// ORDER BY an expression over a column the select list does not carry: the key is computed for each composed row,
// and what it reads rides into the composition for it.
TEST(QueryComposedRead, SortsByAnExpression)
{
	ComposedPair pair(170);
	ibDataQueryBuilder q(nullptr);
	q.From(pair.A).Join(pair.B, pair.aKey, pair.bKey);
	q.Select(pair.aKey, wxT("k"));
	q.OrderByExpr(ibQueryColumnExpr::Arith(ibQueryColumnArithOp::Sub,
		ibQueryColumnExpr::Const(ibValue(ibNumber(0))), ibQueryColumnExpr::Col(pair.bVal)), /*ascending*/ true);
	ibReadPageRequest page;
	ibDataQueryResult res = q.Execute(page);

	EXPECT_EQ(std::vector<long>({ 5, 4, 3, 2, 1 }), KeysRead(res, pair.aKey)) << "0 - v ascending is v descending";
}

// A fold of an expression over the two sides of a join is the composite's: one statement over the copies, not the
// join computed into rows and copied again to be folded (2026-11-19, query 9 of the million rows).
TEST(QueryComposedRead, FoldsAnExpression)
{
	ComposedPair pair(190);
	ibDataQueryBuilder q(nullptr);
	q.From(pair.A).Join(pair.B, pair.aKey, pair.bKey);
	q.Aggregate(ibDataQueryBuilder::AggregateFn::Sum, ibQueryColumnExpr::Arith(ibQueryColumnArithOp::Mul,
		ibQueryColumnExpr::Col(pair.aKey), ibQueryColumnExpr::Col(pair.bVal)), wxT("total"));
	ibDataQueryResult res = q.SelectAggregate();

	ASSERT_TRUE(res.Next());
	EXPECT_EQ(ibNumber(550), res.GetColumn(wxT("total")).GetNumber()) << "k * 10k summed over k = 1..5";
	EXPECT_FALSE(res.Next());
}

// A page read past its anchor starts after it — not again at the first row.
TEST(QueryComposedRead, PageStartsPastItsAnchor)
{
	ComposedPair pair(172);
	ibDataQueryBuilder q(nullptr);
	q.From(pair.A).Join(pair.B, pair.aKey, pair.bKey);
	q.Select(pair.aKey, wxT("k"));
	q.OrderBy(pair.aKey, /*ascending*/ true);
	ibReadPageRequest page;
	page.m_count = 2;
	page.m_hasAnchor = true;
	page.m_anchorSortValues = { ibValue(ibNumber(2)) };
	ibDataQueryResult res = q.Execute(page);

	EXPECT_EQ(std::vector<long>({ 3, 4 }), KeysRead(res, pair.aKey)) << "the two rows after k = 2";
}

// ---- a union whose branches give one output different types ---------------------------------------------------
//
// A union's output is of every type its branches give it. Read through the first branch's column alone, a string under
// a number's spread had no field to go into and came back empty.
TEST(QueryComposedRead, AUnionOutputHoldsEveryBranchsType)
{
	const auto number = std::make_shared<TestCol>(wxT("v"), 1);
	const auto text   = std::make_shared<TestCol>(wxT("v"), 2);
	number->GetTypeDesc().AppendMetaType(ibValueTypes::TYPE_NUMBER);
	text->GetTypeDesc().AppendMetaType(ibValueTypes::TYPE_STRING);
	const auto A = std::make_shared<RecordingComputedQ>(wxT("a"), 178); A->AddCol(number);
	const auto B = std::make_shared<RecordingComputedQ>(wxT("b"), 179); B->AddCol(text);
	A->SetRows([] {
		ibQueryRamTable t;
		t.AddColumn(1, wxT("v"), kNoType);
		t.SetCell(t.AppendRow(), 1, ibValue(ibNumber(7)));
		return t;
	});
	B->SetRows([] {
		ibQueryRamTable t;
		t.AddColumn(2, wxT("v"), kNoType);
		t.SetCell(t.AppendRow(), 2, ibValue(ibString(wxT("x"))));
		return t;
	});

	ibDataQueryBuilder q(nullptr);
	q.From(A);
	q.Union(B);
	q.Select(number, wxT("v"));
	ibDataQueryResult res = q.Execute(ibReadPageRequest{});
	std::vector<ibValue> seen;
	while (res.Next())
		seen.push_back(res.GetColumn(wxT("v")));
	ASSERT_EQ(seen.size(), 2u);
	EXPECT_EQ(seen[0].GetType(), ibValueTypes::TYPE_NUMBER);
	EXPECT_EQ(seen[0].GetNumber(), ibNumber(7));
	EXPECT_EQ(seen[1].GetType(), ibValueTypes::TYPE_STRING) << "the second branch's string, not the number's empty";
	EXPECT_EQ(seen[1].GetString().ToStdString(), "x");
}

// A question put to a union's output is asked of every branch. The union's outputs are named by its first branch's
// columns; answered over that branch's rows alone, the condition left the other branches unfiltered.
TEST(QueryComposedRead, AQuestionPutToAUnionOutputIsAskedOfEveryBranch)
{
	const auto keyA = std::make_shared<TestCol>(wxT("k"), 1);
	const auto keyB = std::make_shared<TestCol>(wxT("k"), 2);
	const auto A = std::make_shared<RecordingComputedQ>(wxT("a"), 182); A->AddCol(keyA);
	const auto B = std::make_shared<RecordingComputedQ>(wxT("b"), 183); B->AddCol(keyB);
	A->SetRows([] { return KeyRows(1, wxT("k"), { 1, 2, 3 }); });
	B->SetRows([] { return KeyRows(2, wxT("k"), { 2, 4 }); });

	ibDataQueryBuilder q(nullptr);
	q.From(A);
	q.Union(B);
	q.Select(keyA, wxT("k"));
	q.WhereExpr(ibQueryColumnExpr::ValueAsk(ibQueryColumnExpr::Col(keyA), ibQueryValueAsk::Presentation),
		ibQueryFilterOp::Equal, ibValue(ibString(wxT("2"))));
	ibDataQueryResult res = q.Execute(ibReadPageRequest{});
	std::vector<long> keys;
	while (res.Next())
		keys.push_back(res.GetColumn(wxT("k")).GetInteger());
	EXPECT_EQ(keys, std::vector<long>({ 2, 2 })) << "the second branch's 2 as well, and not its 4";
}

// ---- the totals door over a source its provider cannot fold ----------------------------------------------------
//
// SelectTotals takes a report's roads: the provider folds the levels where it can, else the detail is read and the
// result folds it. It had a fold of its own by the GROUP BY list, while TotalBy fills the levels — a source whose
// provider could not fold came back as a tree with no level at all.
TEST(QueryComputedTotals, SelectTotalsFoldsTheLevelsInMemory)
{
	const ibMetaID M = 1, A = 2;
	const auto month  = std::make_shared<TestCol>(wxT("m"), M);
	const auto amount = std::make_shared<TestCol>(wxT("a"), A);
	const auto src = std::make_shared<RecordingComputedQ>(wxT("s"), 174);
	src->AddCol(month);
	src->AddCol(amount);
	src->SetRows([&] { return MonthRows(M, A); });   // totals: month 1 = 12, 2 = 40, 3 = 11, 4 = 2

	ibDataQueryBuilder q(nullptr);
	q.From(src);
	q.TotalBy(month, ibDimensionKind::Elements);
	q.Totals().Sum(amount, wxT("total"));
	const ibSelectorTree tree = q.SelectTotals();

	const ibSelectorTree::Node& root = tree.Root();
	EXPECT_EQ(root.m_children.size(), 4u) << "one node per month";
	ASSERT_TRUE(root.m_values.count(A) == 1) << "the grand total rolls into the summed column";
	EXPECT_EQ(ibNumber(65), root.m_values.at(A).GetNumber());
}

// A division by zero over rows in memory is refused, as the database refuses it — it was answered 0, a figure the
// same query read by the server never gives.
TEST(QueryComputedRead, DivisionByZeroIsRefused)
{
	const ibMetaID K = 1;
	const auto key = std::make_shared<TestCol>(wxT("k"), K);
	const auto src = std::make_shared<RecordingComputedQ>(wxT("s"), 176);
	src->AddCol(key);
	src->SetRows([&] { return KeyRows(K, wxT("k"), { 1, 0 }); });

	ibDataQueryBuilder q(nullptr);
	q.From(src);
	q.Select(key, wxT("k"));
	q.SelectExpr(ibQueryColumnExpr::Arith(ibQueryColumnArithOp::Div,
		ibQueryColumnExpr::Const(ibValue(ibNumber(10))), ibQueryColumnExpr::Col(key)), wxT("q"));
	ibReadPageRequest page;
	EXPECT_THROW({
		ibDataQueryResult res = q.Execute(page);
		while (res.Next())
			(void)res.GetColumn(wxT("q"));
	}, ibBackendException) << "10 / 0 is an error, not 0";
}

// The page cache's signature compares the query's parameters as VALUES. It used to print them into one text:
// a date became its raw count, and a string spelling `;b=` read the same as two parameters.
TEST(PageSignature, TheParametersAreComparedAsValuesNotAsTheirText)
{
	const auto sign = [](std::vector<ibString> names, std::vector<ibValue> values) {
		ibPageSignature s;
		s.m_text = wxT("SELECT 1");
		s.m_count = 50;
		s.m_names = std::move(names);
		s.m_values = std::move(values);
		return s;
	};
	const ibValue day(ibDateTime(2026, 3, 29));
	EXPECT_EQ(sign({ wxT("d") }, { day }), sign({ wxT("d") }, { ibValue(ibDateTime(2026, 3, 29)) }));
	EXPECT_NE(sign({ wxT("d") }, { day }), sign({ wxT("d") }, { ibValue(ibDateTime(2026, 3, 29).AddMilliseconds(1)) }));
	EXPECT_NE(sign({ wxT("a") }, { ibValue(ibT("x;b=S1")) }),
	          sign({ wxT("a"), wxT("b") }, { ibValue(ibT("x")), ibValue(ibT("1")) })) << "a string cannot stand for a second parameter";
	EXPECT_NE(sign({ wxT("n") }, { ibValue(1) }), sign({ wxT("n") }, { ibValue(ibT("1")) })) << "a number is not its text";
	EXPECT_EQ(sign({ wxT("n") }, { ibValue(1) }), sign({ wxT("n") }, { ibValue(ibNumber(wxT("1.0"))) })) << "1 and 1.0 are one number";
}
