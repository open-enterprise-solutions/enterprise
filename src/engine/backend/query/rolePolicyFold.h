#ifndef __ROLE_POLICY_FOLD_H__
#define __ROLE_POLICY_FOLD_H__

#include "backend/query/queryable.h"
#include "backend/roleHelper.h"

#include <vector>

////////////////////////////////////////////////////////////////////////////
// How a user's roles combine into ONE condition over the rows
////////////////////////////////////////////////////////////////////////////
//
// ⭐⭐ THE SECOND FOLD OF ONE RULE. A role declares how it combines
// (ibRoleCompositionMode, roleHelper.h), and that declaration decides two
// different things: what the user may DO to an object — folded by
// ibAccessObject::AccessRight, two passes over one list — and which ROWS they
// may see, folded here. Same rule, two materials: a right is a bool, a row
// restriction is a predicate.
//
//   verdict = (union1 OR union2 …) AND intersection1 AND intersection2 …
//
// ⭐ WHY IT IS ITS OWN FUNCTION. Running a role's handler needs a script
// runtime, a configuration and a live user; DECIDING what to do with what the
// handlers said needs none of those. Left inline, the rule could only be
// exercised by standing the whole platform up — which is why it shipped
// "compiles and starts, but the fold itself is unverified" and stayed that way
// for a month. Separated, it is a question anyone can ask: hand it the buckets,
// read the verdict.
//
// The caller still runs the handlers and sorts their answers into the buckets;
// this decides nothing about WHO said what, only what the answers add up to.

// What the union half of a fold is TOLD, beyond its predicates. Both facts are
// about the permitting roles as a GROUP, which is why they are not per-role:
// whether any of them ran at all, and whether any granted with no restriction.
struct ibRolePolicyInput {
	// A permitting role RAN a handler — restricted, granted, or failed. Handler-less
	// roles are not participation: silence is neutral, and a table right granted at
	// the gate stays the whole permission.
	bool m_unionParticipated = false;
	// …and one of them granted with NO restriction at all, which makes the whole
	// union half unrestricted: an OR with an unbounded branch is unbounded.
	bool m_unionUnrestricted = false;

	std::vector<ibQueryPredicatePtr> m_union;
	std::vector<ibQueryPredicatePtr> m_intersection;
};

// The answer. `m_failClosed` is not an error code — it is the verdict "every role
// that spoke refused", which the caller turns into its own refusal with the names
// it has and this function does not.
struct ibRolePolicyVerdict {
	bool                             m_failClosed = false;
	// The OR of the permitting roles, or null when the union half does not narrow.
	ibQueryPredicatePtr              m_union;
	// Each restricting role's own predicate, to be AND-ed in turn.
	std::vector<ibQueryPredicatePtr> m_intersection;
};

inline ibRolePolicyVerdict ibFoldRolePolicy(const ibRolePolicyInput& in)
{
	ibRolePolicyVerdict out;

	// ⭐ THE INTERSECTION HALF APPLIES ALWAYS - that is what makes a restricting role a
	// SEPARATOR rather than a right. It narrows whatever survived the union half and
	// whatever the ones before it left, and because AND is commutative the order the
	// roles were assigned in cannot change the answer.
	out.m_intersection = in.m_intersection;

	// ⭐ THE UNION HALF NARROWS ONLY WHEN A PERMITTING ROLE SPOKE. If none did, the
	// table right granted at the gate is the whole permission and nothing is folded
	// here - which is why an empty union is TRUE rather than the empty set: the door
	// has a separate right-on-the-table stage, so a union role NARROWS an already
	// permitted table instead of granting it. Reading it the other way is the
	// migration-breaking answer: every existing configuration would go dark.
	if (!in.m_unionParticipated || in.m_unionUnrestricted)
		return out;

	// 🛑 PARTICIPATED AND NOTHING SURVIVED = FAIL CLOSED. Roles ran and every one of
	// them refused; an empty OR is FALSE, not "no opinion". Answering "no opinion"
	// here is the fail-open footgun the whole mode exists to avoid.
	if (in.m_union.empty()) {
		out.m_failClosed = true;
		return out;
	}

	ibQueryPredicatePtr orPred = in.m_union.front();
	for (std::size_t i = 1; i < in.m_union.size(); ++i)
		orPred = ibQueryPredicate::Compose(ibQueryPredicateKind::Or, orPred, in.m_union[i]);
	out.m_union = orPred;
	return out;
}

#endif // !__ROLE_POLICY_FOLD_H__
