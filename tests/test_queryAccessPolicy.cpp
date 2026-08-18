// L3 door — ACCESS POLICY coverage on every READ terminal.
//
// The point of this file is one sentence: a read is a read, whichever terminal it leaves by. The
// door used to enforce the policy on the plain reads only, so `SELECT Amount FROM X` was refused
// while `SELECT SUM(Amount) FROM X` answered — an inference leak (totals and counts over rows the
// reader may not see), with `SELECT ALLOWED` inert on that path too. Found by reading, 2026-08-06.
//
// PURE: no database and no session. A REFUSING policy never reaches the composer, which is exactly
// what makes the guard testable without one — the refusal happens at the door or not at all.
//
// See docs/access-policy-rls.md — "Coverage".

#include <gtest/gtest.h>

#include "backend/query/dataQueryBuilder.h"
#include "backend/query/queryable.h"
#include "backend/query/queryColumn.h"
#include "backend/query/queryProvider.h"
#include "backend/backend_exception.h"

namespace {

const ibTypeDescription kNoType;

class PolicyTestCol : public ibBackendQueryColumn {
public:
	PolicyTestCol(const wxString& name, ibMetaID id) : m_name(name), m_id(id) {}
	wxString           GetName()         const override { return m_name; }
	wxString           GetPhysicalName() const override { return m_name; }
	ibTypeDescription& GetTypeDesc()     const override { return m_type; }
	ibMetaID           GetColumnId()     const override { return m_id; }
private:
	wxString                  m_name;
	ibMetaID                  m_id;
	mutable ibTypeDescription m_type;
};

class PolicyTestQueryable : public ibBackendQueryable {
public:
	void AddCol(const ibBackendQueryColumn* c) { m_cols.push_back(c); }

	wxString GetQueryTableName() const override { return wxT("T"); }
	ibMetaID GetQueryTableId()   const override { return 1; }
	ibGuid   GetQueryTableGuid() const override { ibGuidImpl impl{}; impl.m_data1 = 1; return ibGuid(impl); }
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
	std::vector<const ibBackendQueryColumn*> m_cols;
};

// A policy that REFUSES every read and records that it was asked. Refusing is what makes this test
// pure: the door never reaches the composer, so no database is involved in proving the guard runs.
class RefusingPolicy : public ibAccessPolicy {
public:
	bool CheckSelect(ibDataQueryBuilder&, const ibAccessStage&, long) const override { ++m_asked; return false; }
	bool CheckCreate(ibDataQueryBuilder&, const ibAccessStage&, long) const override { return true; }
	bool CheckUpdate(ibDataQueryBuilder&, const ibAccessStage&, long) const override { return true; }
	bool CheckDelete(ibDataQueryBuilder&, const ibAccessStage&, long) const override { return true; }

	int Asked() const { return m_asked; }
private:
	mutable int m_asked = 0;
};

// A query over one table with one summed column — the shape `SELECT SUM(Amount) FROM T`.
struct Fixture
{
	PolicyTestCol       amount{ wxT("Amount"), 10 };
	PolicyTestCol       owner { wxT("Owner"),  11 };
	PolicyTestQueryable table;

	Fixture() { table.AddCol(&amount); table.AddCol(&owner); }
};

} // namespace

// ---------------------------------------------------------------------------
//  The leak itself: an aggregate is a READ and is refused like one
// ---------------------------------------------------------------------------

TEST(QueryAccessPolicy, AggregateReadIsRefused)
{
	Fixture f;
	RefusingPolicy policy;

	ibDataQueryBuilder query;
	query.WithAccessPolicy(&policy).From(&f.table);
	query.Aggregate(ibDataQueryBuilder::AggregateFn::Sum, &f.amount, wxT("total"));

	// (void): the terminals are [[nodiscard]], and here the THROW is the subject, not the result.
	EXPECT_THROW((void)query.SelectAggregate(), ibBackendException);
	EXPECT_EQ(1, policy.Asked());   // asked, not bypassed
}

TEST(QueryAccessPolicy, AggregatePageReadIsRefused)
{
	Fixture f;
	RefusingPolicy policy;

	ibDataQueryBuilder query;
	query.WithAccessPolicy(&policy).From(&f.table);
	query.GroupBy(&f.owner);
	query.Aggregate(ibDataQueryBuilder::AggregateFn::Sum, &f.amount, wxT("total"));

	ibReadPageRequest page;
	EXPECT_THROW((void)query.SelectAggregatePage(page), ibBackendException);
	EXPECT_EQ(1, policy.Asked());
}

// Nothing calls SelectTotals today — which is exactly why it is guarded: an unguarded read terminal
// is a loaded gun, and the first caller would get an unchecked read with nothing looking wrong.
TEST(QueryAccessPolicy, TotalsReadIsRefused)
{
	Fixture f;
	RefusingPolicy policy;

	ibDataQueryBuilder query;
	query.WithAccessPolicy(&policy).From(&f.table);
	query.TotalBy(&f.owner, ibDimensionKind::Elements);

	EXPECT_THROW((void)query.SelectTotals(), ibBackendException);
}

// ---------------------------------------------------------------------------
//  SELECT ALLOWED — the two aggregate shapes mean different things
// ---------------------------------------------------------------------------

// `SELECT SUM(x) FROM T` returns a row whatever T holds, so the honest answer to "the sum of what
// you may see" over nothing you may see is ZERO — not "there is no such question".
TEST(QueryAccessPolicy, AllowedAggregateWithoutGroupingYieldsOneZeroRow)
{
	Fixture f;
	RefusingPolicy policy;

	ibDataQueryBuilder query;
	query.WithAccessPolicy(&policy).From(&f.table).Allowed();
	query.Aggregate(ibDataQueryBuilder::AggregateFn::Sum, &f.amount, wxT("total"));

	ibDataQueryResult result = query.SelectAggregate();
	ASSERT_TRUE(result.Next());
	EXPECT_EQ(0, result.GetColumn(wxT("total")).GetInteger());
	EXPECT_FALSE(result.Next());   // exactly one row
}

// With a GROUP BY the empty result is already right: no visible rows, no groups.
TEST(QueryAccessPolicy, AllowedAggregateWithGroupingYieldsNoRows)
{
	Fixture f;
	RefusingPolicy policy;

	ibDataQueryBuilder query;
	query.WithAccessPolicy(&policy).From(&f.table).Allowed();
	query.GroupBy(&f.owner);
	query.Aggregate(ibDataQueryBuilder::AggregateFn::Sum, &f.amount, wxT("total"));

	ibDataQueryResult result = query.SelectAggregate();
	EXPECT_FALSE(result.Next());
}

TEST(QueryAccessPolicy, AllowedAggregatePageYieldsNoRows)
{
	Fixture f;
	RefusingPolicy policy;

	ibDataQueryBuilder query;
	query.WithAccessPolicy(&policy).From(&f.table).Allowed();
	query.GroupBy(&f.owner);
	query.Aggregate(ibDataQueryBuilder::AggregateFn::Sum, &f.amount, wxT("total"));

	ibReadPageRequest page;
	ibDataQueryResult result = query.SelectAggregatePage(page);
	EXPECT_FALSE(result.Next());
}

// A totals TREE has no empty form that is not a lie about its shape, so ALLOWED does not soften it.
TEST(QueryAccessPolicy, AllowedDoesNotSoftenTotals)
{
	Fixture f;
	RefusingPolicy policy;

	ibDataQueryBuilder query;
	query.WithAccessPolicy(&policy).From(&f.table).Allowed();
	query.TotalBy(&f.owner, ibDimensionKind::Elements);

	EXPECT_THROW((void)query.SelectTotals(), ibBackendException);
}

// ---------------------------------------------------------------------------
//  The system read — said out loud, never inferred
// ---------------------------------------------------------------------------

// A read that must not be filtered clears the policy AT THE CALLSITE. `ibDerivedState` routes every
// builder through one such helper: totals regeneration recomputes STORED numbers, and rebuilding
// them from the rows the caller happens to see would leave everyone reading figures that are wrong.
//
// The check here is that the door really does step aside when told to — the refusing policy is never
// asked, so the read proceeds (and, with no database behind it, that is as far as this can go).
TEST(QueryAccessPolicy, ClearedPolicyIsNotAsked)
{
	Fixture f;
	RefusingPolicy policy;

	ibDataQueryBuilder query;
	query.WithAccessPolicy(&policy).From(&f.table);
	query.Aggregate(ibDataQueryBuilder::AggregateFn::Sum, &f.amount, wxT("total"));
	query.WithAccessPolicy(nullptr);   // the system read

	// No access exception can come out of the door now: the guard is not there to raise one.
	try { (void)query.SelectAggregate(); } catch (...) { /* the composer has no DB here — not our subject */ }
	EXPECT_EQ(0, policy.Asked());
}
