// =============================================================================
// OES Enterprise — how a user's roles combine into ONE condition over the ROWS
//
// The row-level twin of test_roleComposition.cpp. One rule, two materials: a
// right is a bool and is folded by ibAccessObject::AccessRight; a row
// restriction is a PREDICATE and is folded here.
//
//     verdict = (union1 OR union2 …) AND intersection1 AND intersection2 …
//
// 🛑 WHY THIS FILE EXISTS. The composition mode landed 2026-08-08 with "compiles
// and starts, but the fold itself is unverified — no test over the two buckets,
// not yet run under a live role", and the RIGHTS half was pinned on 2026-09-09.
// The ROW half could not be: the decision lived inside the body that RUNS the
// role handlers, so exercising the rule meant standing up a runtime, a
// configuration and a live user. `ibFoldRolePolicy` (query/rolePolicyFold.h) is
// that decision lifted out — the caller still runs the handlers and sorts their
// answers into the buckets; the fold only says what the answers add up to.
//
// ⚠ WHAT THIS DOES NOT COVER, said plainly rather than implied: the handlers
// themselves, whether a role's `Restrict` reaches the SQL, and the live-role run.
// Those need a database and a user. What is pinned here is the arithmetic of the
// verdict, which is the half that had no test at all.
// =============================================================================

#include <gtest/gtest.h>

#include "backend/query/rolePolicyFold.h"

namespace {

// A predicate that stands for "some role's WHERE". Its CONTENTS never matter to
// the fold — it groups and orders predicates, it does not read them — so the
// cheapest honest stand-in is a distinct leaf per role.
ibQueryPredicatePtr SomeRestriction()
{
	ibQueryCondition c;
	return ibQueryPredicate::Leaf(c);
}

// How many leaves an OR-tree carries — the fold's only visible arithmetic on the
// union side, since it composes left to right.
int LeafCount(const ibQueryPredicatePtr& pred)
{
	if (!pred)
		return 0;
	if (pred->m_kind != ibQueryPredicateKind::Or)
		return 1;
	int total = 0;
	for (const ibQueryPredicatePtr& child : pred->m_children)
		total += LeafCount(child);
	return total;
}

} // namespace

// -----------------------------------------------------------------------------
// The floor: nobody spoke
// -----------------------------------------------------------------------------

// No roles at all. Nothing narrows, nothing refuses — and that is not the empty
// set: the door has a separate right-on-the-table stage, so a union role NARROWS
// an already permitted table rather than granting it. Reading an empty union as
// FALSE is the answer that would take every existing configuration dark.
TEST(RolePolicyFold, NobodySpoke_NothingIsFolded)
{
	const ibRolePolicyVerdict v = ibFoldRolePolicy(ibRolePolicyInput());

	EXPECT_FALSE(v.m_failClosed);
	EXPECT_FALSE(v.m_union);
	EXPECT_TRUE(v.m_intersection.empty());
}

// A permitting role that declared no handler is SILENCE, not participation.
// Handler-less roles are how every configuration written before this mode
// existed behaves, so they must fold to nothing.
TEST(RolePolicyFold, HandlerlessRolesDoNotParticipate)
{
	ibRolePolicyInput in;
	in.m_unionParticipated = false;
	in.m_union.push_back(SomeRestriction());   // …even if something got collected

	const ibRolePolicyVerdict v = ibFoldRolePolicy(in);

	EXPECT_FALSE(v.m_union) << "a union that nobody participated in must not narrow";
	EXPECT_FALSE(v.m_failClosed);
}

// -----------------------------------------------------------------------------
// The UNION bucket — permitting roles widen each other
// -----------------------------------------------------------------------------

TEST(RolePolicyFold, OnePermittingRole_ItsRestrictionStands)
{
	ibRolePolicyInput in;
	in.m_unionParticipated = true;
	in.m_union.push_back(SomeRestriction());

	const ibRolePolicyVerdict v = ibFoldRolePolicy(in);

	ASSERT_TRUE(v.m_union);
	EXPECT_EQ(1, LeafCount(v.m_union));
	EXPECT_FALSE(v.m_failClosed);
}

// Two permitting roles OR: each admits its own rows and the user sees both sets.
// This is the whole of "Union" on the row side — permissions accumulate.
TEST(RolePolicyFold, TwoPermittingRoles_OrTogether)
{
	ibRolePolicyInput in;
	in.m_unionParticipated = true;
	in.m_union.push_back(SomeRestriction());
	in.m_union.push_back(SomeRestriction());

	const ibRolePolicyVerdict v = ibFoldRolePolicy(in);

	ASSERT_TRUE(v.m_union);
	EXPECT_EQ(ibQueryPredicateKind::Or, v.m_union->m_kind);
	EXPECT_EQ(2, LeafCount(v.m_union));
}

// ⭐ ONE UNRESTRICTED GRANT MAKES THE WHOLE UNION UNRESTRICTED. An OR with an
// unbounded branch is unbounded, so a role that grants the table outright is not
// narrowed by a colleague who restricts — folding the restriction anyway would
// take rows away from a user a role explicitly gave them to.
TEST(RolePolicyFold, AnUnrestrictedGrantOpensTheWholeUnion)
{
	ibRolePolicyInput in;
	in.m_unionParticipated = true;
	in.m_unionUnrestricted = true;
	in.m_union.push_back(SomeRestriction());

	const ibRolePolicyVerdict v = ibFoldRolePolicy(in);

	EXPECT_FALSE(v.m_union) << "an OR with an unbounded branch cannot narrow";
	EXPECT_FALSE(v.m_failClosed);
}

// 🛑 PARTICIPATED AND NOTHING SURVIVED IS FAIL-CLOSED. Roles ran and every one of
// them refused; an empty OR is FALSE, never "no opinion". Answering "no opinion"
// is the multi-role fail-open footgun this mode exists to prevent — a user whose
// every role denied would otherwise see the whole table.
TEST(RolePolicyFold, EveryPermittingRoleFailed_FailsClosed)
{
	ibRolePolicyInput in;
	in.m_unionParticipated = true;   // they RAN
	// …and left nothing behind.

	const ibRolePolicyVerdict v = ibFoldRolePolicy(in);

	EXPECT_TRUE(v.m_failClosed);
	EXPECT_FALSE(v.m_union);
}

// …and an unrestricted grant beats that too: if one role opened the table, the
// others failing is not a refusal of the user.
TEST(RolePolicyFold, AnUnrestrictedGrantIsNotOverriddenByFailures)
{
	ibRolePolicyInput in;
	in.m_unionParticipated = true;
	in.m_unionUnrestricted = true;

	const ibRolePolicyVerdict v = ibFoldRolePolicy(in);

	EXPECT_FALSE(v.m_failClosed);
	EXPECT_FALSE(v.m_union);
}

// -----------------------------------------------------------------------------
// The INTERSECTION bucket — restricting roles subtract
// -----------------------------------------------------------------------------

// ⭐ THE INTERSECTION HALF APPLIES ALWAYS, which is what makes a restricting role
// a SEPARATOR rather than a right. It narrows even where no permitting role
// spoke — the case of a user carrying only a separator beside a table right
// granted at the gate.
TEST(RolePolicyFold, RestrictingRoleAppliesEvenWithNoUnion)
{
	ibRolePolicyInput in;
	in.m_intersection.push_back(SomeRestriction());

	const ibRolePolicyVerdict v = ibFoldRolePolicy(in);

	EXPECT_FALSE(v.m_union);
	ASSERT_EQ(1u, v.m_intersection.size());
	EXPECT_FALSE(v.m_failClosed);
}

// …and it is NOT swallowed by an unrestricted grant. A permitting role opening
// the table cannot widen past a separator: that is the sentence the whole mode
// exists for, and the one place a fail-open would be invisible.
TEST(RolePolicyFold, AnUnrestrictedGrantDoesNotSwallowTheSeparator)
{
	ibRolePolicyInput in;
	in.m_unionParticipated = true;
	in.m_unionUnrestricted = true;
	in.m_intersection.push_back(SomeRestriction());

	const ibRolePolicyVerdict v = ibFoldRolePolicy(in);

	EXPECT_FALSE(v.m_union);
	ASSERT_EQ(1u, v.m_intersection.size()) << "no grant may widen past a restricting role";
}

// Several separators all apply — they AND, so each narrows what the ones before
// it left. N+M roles instead of N×M is exactly this: separators compose.
TEST(RolePolicyFold, SeveralRestrictingRolesAllApply)
{
	ibRolePolicyInput in;
	in.m_intersection.push_back(SomeRestriction());
	in.m_intersection.push_back(SomeRestriction());
	in.m_intersection.push_back(SomeRestriction());

	const ibRolePolicyVerdict v = ibFoldRolePolicy(in);

	EXPECT_EQ(3u, v.m_intersection.size());
}

// And they survive a fail-closed union: the caller refuses on m_failClosed, so
// the fold must not quietly drop the separators on the way to saying so — a
// verdict that lost half its terms would be the wrong evidence in the refusal.
TEST(RolePolicyFold, SeparatorsSurviveAFailClosedUnion)
{
	ibRolePolicyInput in;
	in.m_unionParticipated = true;             // ran, all failed
	in.m_intersection.push_back(SomeRestriction());

	const ibRolePolicyVerdict v = ibFoldRolePolicy(in);

	EXPECT_TRUE(v.m_failClosed);
	EXPECT_EQ(1u, v.m_intersection.size());
}

// -----------------------------------------------------------------------------
// The verdict as a whole
// -----------------------------------------------------------------------------

// Both halves at once: two permitting roles OR, one separator ANDs, and the
// separator is not folded into the OR — the shape the design states in words.
TEST(RolePolicyFold, BothHalves_UnionOrsAndSeparatorAnds)
{
	ibRolePolicyInput in;
	in.m_unionParticipated = true;
	in.m_union.push_back(SomeRestriction());
	in.m_union.push_back(SomeRestriction());
	in.m_intersection.push_back(SomeRestriction());

	const ibRolePolicyVerdict v = ibFoldRolePolicy(in);

	ASSERT_TRUE(v.m_union);
	EXPECT_EQ(2, LeafCount(v.m_union)) << "the separator must not join the OR";
	EXPECT_EQ(1u, v.m_intersection.size());
	EXPECT_FALSE(v.m_failClosed);
}

// ⭐ THE ORDER ROLES WERE ASSIGNED IN CANNOT CHANGE THE ANSWER — the property an
// ACL with DENY does not have. Here it shows as shape: the same counts whichever
// way the buckets were filled.
TEST(RolePolicyFold, AssignmentOrderDoesNotChangeTheShape)
{
	ibRolePolicyInput a;
	a.m_unionParticipated = true;
	a.m_union.push_back(SomeRestriction());
	a.m_union.push_back(SomeRestriction());
	a.m_intersection.push_back(SomeRestriction());
	a.m_intersection.push_back(SomeRestriction());

	ibRolePolicyInput b = a;
	std::reverse(b.m_union.begin(), b.m_union.end());
	std::reverse(b.m_intersection.begin(), b.m_intersection.end());

	const ibRolePolicyVerdict va = ibFoldRolePolicy(a);
	const ibRolePolicyVerdict vb = ibFoldRolePolicy(b);

	EXPECT_EQ(LeafCount(va.m_union), LeafCount(vb.m_union));
	EXPECT_EQ(va.m_intersection.size(), vb.m_intersection.size());
	EXPECT_EQ(va.m_failClosed, vb.m_failClosed);
}
