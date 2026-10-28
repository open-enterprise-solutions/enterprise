#ifndef _FRMCLIENT_TYPE_SELECTOR_DIALOG_H__
#define _FRMCLIENT_TYPE_SELECTOR_DIALOG_H__

// THE TYPE PICKER — the old desktop's, with its door: it reads a type and
// returns the edited one, true when the person accepted, false on cancel — and on cancel `inOut` is untouched.
//
// WHAT IS ON OFFER is `filter` here, the types the server offered, each named as it offered it (ibValue::RegisterCtor):
// the desktop asked its configuration for what a field of the shape `kind` may hold (GetTypesByFilter), and the client
// has none to ask. A type the value already holds is offered either way. `single` — one type, not several.
//
// ⚠ A caller with no offer of the server's (`filter` empty) is answered Cancel, the picker not shown — the composer's
// parameter picker is such, until its request carries one.

#include "core/description/typeDescription.h"
#include "frmclient/backend/backend_type.h"   // ibSelectorDataType — which shape the picker renders

#include <vector>

FRMCLIENT_API bool ibShowTypeSelector(class wxWindow* parent, ibSelectorDataType kind,
	const std::vector<ibClassID>& filter, ibTypeDescription& inOut, const class ibMetaData* metaData,
	bool allowEdit = true, bool single = false);

#endif
