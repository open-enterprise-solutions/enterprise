#ifndef __ROLE_COMPOSITION_MODE_H__
#define __ROLE_COMPOSITION_MODE_H__

////////////////////////////////////////////////////////////////////////////
//	Description : HOW A ROLE COMBINES with the user's other roles - one vocabulary for every side
////////////////////////////////////////////////////////////////////////////
//
// The fold is the server's (backend/roleHelper.h: the runtime role list and ibAccessObject::AccessRight);
// a user's row carries the mode with each role, and the windows that edit a user — the desktop's, the
// client's from the server's answer — name it. The Role METAOBJECT only carries it as a property. One
// enum, so it lives alone here, as queryBalanceRole.h does.
//
// Named after the SET operation rather than the boolean one, because that is what the fold does:
//
//   Union        - the role ADDS to what is permitted; one of them granting is enough. An ordinary
//                  right, and the DEFAULT — anything that does not say otherwise is one, so an
//                  existing configuration behaves exactly as it did.
//   Intersection - the role GRANTS NOTHING and SUBTRACTS: what it denies stays denied whatever the
//                  union roles granted, and no further role can widen past it. This is what a data
//                  SEPARATOR is (organisation / division / clearance): declared once instead of
//                  copied into every role, so rights and separators ADD (N+M) instead of
//                  multiplying (N×M).
//
// The verdict is (union1 OR union2 …) AND intersection1 AND intersection2 …, so the ORDER roles are
// assigned in never changes the answer — unlike an ACL with DENY, where it decides the outcome. A
// third "deny" mode would add no expressive power: it is an intersection with a negated condition.
//
// A role that says NOTHING participates in neither fold. Without that neutrality a separator would
// strip every right / hide every table it was never told about.
//
// ⚠ STORED BY NUMBER (a user's row, a role's property): appended, never inserted.

enum ibRoleCompositionMode {
	ibRoleCompositionMode_Union,
	ibRoleCompositionMode_Intersection
};

#endif // __ROLE_COMPOSITION_MODE_H__
