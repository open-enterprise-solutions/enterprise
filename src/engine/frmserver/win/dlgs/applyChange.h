#ifndef __CHANGING_STRUCTURE_H__
#define __CHANGING_STRUCTURE_H__

#include "backend/restructureInfo.h"

// THE RESTRUCTURE'S LEDGER PUT TO THE PERSON — the desktop's ibDialogApplyChange (frontend/win/dlgs/applyChange.h), its
// window the client's (frmclient/win/dlgs/applyChange.h): the ledger is sent (ibProtocol::ibRequest::ibKind::ApplyChange), Apply
// comes back. The configuration's apply asks it as the desktop's did — its `decide`, the base held until it is answered.
class ibDialogApplyChange {
public:

	// Nothing to show — applied, as the desktop's; no one to ask — not.
	static bool ShowApplyChange(const ibRestructureInfo& info);
};

#endif
