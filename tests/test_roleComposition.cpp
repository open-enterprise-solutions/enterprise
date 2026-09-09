// =============================================================================
// OES Enterprise — how a user's roles COMBINE into one answer about a right
//
// A role declares HOW IT COMBINES (ibRoleCompositionMode, landed 2026-08-08):
//
//   * Union ("Permitting")    — ADDS. One of them granting is enough.
//   * Intersection ("Restricting") — GRANTS NOTHING and SUBTRACTS: what it denies
//     stays denied whatever the others admitted, and no further role can widen
//     past it. That is what a data separator IS, declared once instead of copied
//     into every role (N+M roles instead of N×M).
//
// The verdict is `(union1 OR union2 …) AND intersection1 AND intersection2 …`,
// which is why the ORDER roles were assigned in cannot change the answer.
//
// 🛑 WHY THIS FILE EXISTS. The fold shipped with "compiles and starts, but the
// fold itself is unverified — no test over the two buckets, not yet run under a
// live role" (docs/ROADMAP.md §2, access-policy-rls row) and stayed that way for
// a month. Every case below is a sentence from that design, turned into a
// question the code has to answer out loud.
//
// DB-free: ibAccessObject is a plain object with a role table on it, so a
// minimal subclass is the whole fixture — no configuration, no database.
// =============================================================================

#include <gtest/gtest.h>

#include "backend/roleHelper.h"

namespace {

// The smallest thing that HAS rights: one object declaring two of them, one
// allowed by default and one denied by default. The defaults matter — "unset" is
// not "denied", and every case below has to say which of the two it is testing.
class RightsBearer : public ibAccessObject {
public:
	// The one thing the base leaves to its owner: who is asking. Every case here
	// passes the roles EXPLICITLY, so this answers for the no-argument overloads
	// only — a user carrying nothing, which is also the floor case below.
	ibRoleUserInfo GetUserRoleInfo() const override { return ibRoleUserInfo(); }

	ibRole* m_allowedByDefault = CreateRole(wxT("Read"),  wxT("Read"),  /*defValue*/ true);
	ibRole* m_deniedByDefault  = CreateRole(wxT("Erase"), wxT("Erase"), /*defValue*/ false);
};

// Role ids as a user carries them. Any distinct numbers will do: the fold looks
// them up in the object's value map and never interprets them.
constexpr ibRoleID kSales     = 1;
constexpr ibRoleID kWarehouse = 2;
constexpr ibRoleID kSeparator = 3;

ibUserRoleEntry Permitting(ibRoleID id)  { return ibUserRoleEntry(id, ibRoleCompositionMode_Union); }
ibUserRoleEntry Restricting(ibRoleID id) { return ibUserRoleEntry(id, ibRoleCompositionMode_Intersection); }

ibRoleUserInfo Roles(std::vector<ibUserRoleEntry> entries) { return ibRoleUserInfo(entries); }

} // namespace

// -----------------------------------------------------------------------------
// The floor: no roles at all
// -----------------------------------------------------------------------------

// A user with no roles gets each right's OWN declared default. This is the case
// every base starts in — a configuration with no accounts in it yet — and reading
// it as "denied" would make an empty base unusable rather than permissive.
TEST(RoleComposition, NoRoles_EachRightAnswersItsOwnDefault)
{
	RightsBearer obj;
	const ibRoleUserInfo none;

	EXPECT_TRUE(obj.AccessRight(obj.m_allowedByDefault, none));
	EXPECT_FALSE(obj.AccessRight(obj.m_deniedByDefault, none));
}

// -----------------------------------------------------------------------------
// The UNION bucket — permitting roles ADD
// -----------------------------------------------------------------------------

// One permitting role that grants is enough, even beside one that denies. This is
// the whole of "Union": rights accumulate, and a role that says no is outvoted by
// a role that says yes rather than the other way round.
TEST(RoleComposition, Union_OneGrantIsEnough)
{
	RightsBearer obj;
	obj.SetRight(obj.m_deniedByDefault, kSales,     false);
	obj.SetRight(obj.m_deniedByDefault, kWarehouse, true);

	EXPECT_TRUE(obj.AccessRight(obj.m_deniedByDefault, Roles({ Permitting(kSales), Permitting(kWarehouse) })));
}

// …and the same pair the other way round. Order-independence is a PROPERTY of the
// verdict, not an accident of how the loop happens to run: an ACL with DENY does
// not have it, and losing it here would make a user's rights depend on the order
// somebody ticked their roles in.
TEST(RoleComposition, Union_OrderOfRolesDoesNotChangeTheAnswer)
{
	RightsBearer obj;
	obj.SetRight(obj.m_deniedByDefault, kSales,     false);
	obj.SetRight(obj.m_deniedByDefault, kWarehouse, true);

	const bool salesFirst = obj.AccessRight(obj.m_deniedByDefault, Roles({ Permitting(kSales), Permitting(kWarehouse) }));
	const bool houseFirst = obj.AccessRight(obj.m_deniedByDefault, Roles({ Permitting(kWarehouse), Permitting(kSales) }));

	EXPECT_EQ(salesFirst, houseFirst);
	EXPECT_TRUE(salesFirst);
}

// Every permitting role denying it leaves it denied — there is nothing to add.
TEST(RoleComposition, Union_AllDeny_StaysDenied)
{
	RightsBearer obj;
	obj.SetRight(obj.m_allowedByDefault, kSales,     false);
	obj.SetRight(obj.m_allowedByDefault, kWarehouse, false);

	EXPECT_FALSE(obj.AccessRight(obj.m_allowedByDefault, Roles({ Permitting(kSales), Permitting(kWarehouse) })));
}

// A role SILENT about a right contributes the right's default, not whatever the
// previously examined role said. "Never flipped in the editor" is a third state
// beside allowed and denied, and it is what makes a role editable one right at a
// time instead of having to restate the whole configuration.
TEST(RoleComposition, Union_SilentRoleContributesTheDefault_NotTheNeighboursAnswer)
{
	RightsBearer obj;
	obj.SetRight(obj.m_allowedByDefault, kSales, false);   // kWarehouse says nothing

	EXPECT_TRUE(obj.AccessRight(obj.m_allowedByDefault, Roles({ Permitting(kSales), Permitting(kWarehouse) })));
}

// -----------------------------------------------------------------------------
// The INTERSECTION bucket — restricting roles SUBTRACT
// -----------------------------------------------------------------------------

// A restricting role's denial holds whatever the permitting ones admitted. This is
// the case the mode was built for: one separator role, assigned beside anything.
TEST(RoleComposition, Intersection_DenialHoldsOverAnyGrant)
{
	RightsBearer obj;
	obj.SetRight(obj.m_deniedByDefault, kSales,     true);
	obj.SetRight(obj.m_deniedByDefault, kSeparator, false);

	EXPECT_TRUE (obj.AccessRight(obj.m_deniedByDefault, Roles({ Permitting(kSales) })));
	EXPECT_FALSE(obj.AccessRight(obj.m_deniedByDefault, Roles({ Permitting(kSales), Restricting(kSeparator) })));
}

// …and it cannot be widened back by adding more permitting roles after it. "No
// further role can widen past it" is the sentence that makes a separator worth
// having at all — otherwise it would be one more voice in the same vote.
TEST(RoleComposition, Intersection_CannotBeWidenedByMorePermittingRoles)
{
	RightsBearer obj;
	obj.SetRight(obj.m_deniedByDefault, kSales,     true);
	obj.SetRight(obj.m_deniedByDefault, kWarehouse, true);
	obj.SetRight(obj.m_deniedByDefault, kSeparator, false);

	EXPECT_FALSE(obj.AccessRight(obj.m_deniedByDefault,
		Roles({ Permitting(kSales), Restricting(kSeparator), Permitting(kWarehouse) })));
}

// 🛑 A RESTRICTING ROLE GRANTS NOTHING. An explicit True in one means "I do not
// object", never "I allow" — so a user carrying ONLY a restricting role that says
// True still falls back to the right's own default. Reading that True as a grant
// would turn every separator into an administrator.
TEST(RoleComposition, Intersection_ExplicitTrueIsNotAGrant)
{
	RightsBearer obj;
	obj.SetRight(obj.m_deniedByDefault, kSeparator, true);

	EXPECT_FALSE(obj.AccessRight(obj.m_deniedByDefault, Roles({ Restricting(kSeparator) })));
}

// …and its SILENCE is neutral, which is what keeps a freshly created separator
// from stripping every right in the configuration the moment it is assigned.
TEST(RoleComposition, Intersection_SilenceStripsNothing)
{
	RightsBearer obj;
	obj.SetRight(obj.m_deniedByDefault, kSales, true);   // kSeparator says nothing at all

	EXPECT_TRUE(obj.AccessRight(obj.m_deniedByDefault, Roles({ Permitting(kSales), Restricting(kSeparator) })));
}

// A restricting role beside NO permitting role subtracts from the DEFAULT, not
// from an implicit grant: the right that was allowed unless stated otherwise is
// exactly the one a separator has to be able to take away.
TEST(RoleComposition, Intersection_SubtractsFromTheDefaultWhenNothingPermits)
{
	RightsBearer obj;
	obj.SetRight(obj.m_allowedByDefault, kSeparator, false);

	EXPECT_FALSE(obj.AccessRight(obj.m_allowedByDefault, Roles({ Restricting(kSeparator) })));
}

// -----------------------------------------------------------------------------
// The verdict as a whole
// -----------------------------------------------------------------------------

// Order-independence over BOTH buckets mixed — the property the design claims in
// so many words. Every permutation of one grant, one denial and one separator has
// to give the same verdict.
TEST(RoleComposition, Verdict_IsIndependentOfAssignmentOrder)
{
	RightsBearer obj;
	obj.SetRight(obj.m_deniedByDefault, kSales,     true);
	obj.SetRight(obj.m_deniedByDefault, kWarehouse, false);
	obj.SetRight(obj.m_deniedByDefault, kSeparator, false);

	const std::vector<std::vector<ibUserRoleEntry>> orders = {
		{ Permitting(kSales), Permitting(kWarehouse), Restricting(kSeparator) },
		{ Restricting(kSeparator), Permitting(kSales), Permitting(kWarehouse) },
		{ Permitting(kWarehouse), Restricting(kSeparator), Permitting(kSales) },
		{ Restricting(kSeparator), Permitting(kWarehouse), Permitting(kSales) },
	};
	for (const std::vector<ibUserRoleEntry>& order : orders)
		EXPECT_FALSE(obj.AccessRight(obj.m_deniedByDefault, Roles(order))) << "order " << order.size();
}

// A user carrying only permitting roles behaves exactly as before the mode
// existed — the compatibility statement the design makes, pinned so a later
// change to the intersection pass cannot quietly move the ordinary case.
TEST(RoleComposition, UnionOnly_BehavesAsBeforeTheModeExisted)
{
	RightsBearer obj;
	obj.SetRight(obj.m_allowedByDefault, kSales, false);
	obj.SetRight(obj.m_deniedByDefault,  kSales, true);

	EXPECT_FALSE(obj.AccessRight(obj.m_allowedByDefault, Roles({ Permitting(kSales) })));
	EXPECT_TRUE (obj.AccessRight(obj.m_deniedByDefault,  Roles({ Permitting(kSales) })));
}

// A right nobody ever flipped, under roles of both kinds, is still the default.
// The fold has to reach that answer without any stored state saying so.
TEST(RoleComposition, UnstatedRight_UnderBothKinds_IsStillTheDefault)
{
	RightsBearer obj;

	EXPECT_TRUE (obj.AccessRight(obj.m_allowedByDefault, Roles({ Permitting(kSales), Restricting(kSeparator) })));
	EXPECT_FALSE(obj.AccessRight(obj.m_deniedByDefault,  Roles({ Permitting(kSales), Restricting(kSeparator) })));
}
