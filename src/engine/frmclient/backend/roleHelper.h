#ifndef _FRMCLIENT_BACKEND_ROLE_HELPER_H__
#define _FRMCLIENT_BACKEND_ROLE_HELPER_H__

// THE ROLE LAYER (backend/roleHelper.h), the part the code copied from the desktop names: how a role combines with the
// user's other roles. The fold itself — the rights a user's roles grant — is the server's.

#include "frmclient/backend/backend_core.h"

// How a role COMBINES with the user's other roles — the desktop's, value for value (the server writes it on a role's
// row in the tree):
//
//   Union        - the role ADDS to what is permitted; one of them granting is enough. The DEFAULT.
//   Intersection - the role GRANTS NOTHING and SUBTRACTS: what it denies stays denied whatever the union roles granted.
enum ibRoleCompositionMode {
	ibRoleCompositionMode_Union,
	ibRoleCompositionMode_Intersection
};

#endif
