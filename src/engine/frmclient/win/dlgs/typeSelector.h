#ifndef _FRMCLIENT_TYPE_SELECTOR_DIALOG_H__
#define _FRMCLIENT_TYPE_SELECTOR_DIALOG_H__

// THE TYPE PICKER — the desktop's (frontend/win/dlgs/typeSelector.h): the designer's, over the types the configuration
// has. Here with the desktop's door, and it answers Cancel: `inOut` stays as it was.

#include "core/description/typeDescription.h"
#include "frmclient/backend/backend_type.h"   // ibSelectorDataType — which shape the picker renders

#include <vector>

inline bool ibShowTypeSelector(class wxWindow* WXUNUSED(parent), ibSelectorDataType WXUNUSED(kind),
	const std::vector<ibClassID>& WXUNUSED(filter), ibTypeDescription& WXUNUSED(inOut),
	const class ibMetaData* WXUNUSED(metaData), bool WXUNUSED(allowEdit) = true, bool WXUNUSED(single) = false)
{
	return false;
}

#endif
