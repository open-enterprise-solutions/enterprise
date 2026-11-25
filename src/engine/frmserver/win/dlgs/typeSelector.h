#ifndef __FRMSERVER_TYPE_SELECTOR_H__
#define __FRMSERVER_TYPE_SELECTOR_H__

////////////////////////////////////////////////////////////////////////////
// THE TYPE PICKER OVER A FIELD THAT HOLDS A TYPE DESCRIPTION
////////////////////////////////////////////////////////////////////////////
//
// The desktop's dialog, asked of the thin client (frmclient/win/dlgs/typeSelector.h,
// ibProtocol::ibRequest::ibKind::TypeSelector): a characteristic's Type is edited at run time in the window the studio
// edits an attribute's in. What may be picked is `offered` — named here, since the client has no application to ask.
////////////////////////////////////////////////////////////////////////////

#include "frmserver/frmserver.h"
#include "core/description/typeDescription.h"

class ibControlFrame;
class ibMetaData;

// Answers `true` when the window was shown — the description edited goes into the control as a choice
// (ChoiceProcessing); `false` when the control holds no type description, nothing is offered, or there is no window to ask.
bool ibShowTypeSelector(ibControlFrame* ownerValue, const ibTypeDescription& offered, const ibMetaData* metaData);

#endif // __FRMSERVER_TYPE_SELECTOR_H__
