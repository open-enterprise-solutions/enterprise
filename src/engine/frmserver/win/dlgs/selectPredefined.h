#ifndef __FRMSERVER_SELECT_PREDEFINED_H__
#define __FRMSERVER_SELECT_PREDEFINED_H__

////////////////////////////////////////////////////////////////////////////
// THE DESIGNER'S CHOICE OF A DECLARED VALUE
////////////////////////////////////////////////////////////////////////////
//
// The desktop's (frontend/win/dlgs/selectPredefined.h), asked of the thin client. At run time a reference is chosen
// from the ROWS — the metaobject's own selection form. While a configuration is being WRITTEN there are none, and what
// a person needs to say is one of the things the configuration DECLARES: the empty reference of a type, a predefined
// element, an enumeration member.
//
// It asks the desktop's two questions: WHICH TYPE, through the same "Select data type" list every other road opens —
// where every reference the declaration admits stands as ONE technical entry (`CompositionPredefinedValue`) — and then
// WHICH DECLARED VALUE, as a tree with a branch per admitted reference type. The tree is the client's window
// (ibProtocol::ibRequest::ibKind::PredefinedSelector): the values are numbered here, and the number is the answer.
////////////////////////////////////////////////////////////////////////////

#include "frmserver/frmserver.h"
#include "core/description/typeDescription.h"

class ibControlFrame;
class ibMetaData;

// Answers `true` when a value was chosen and written into the control; `false` when nothing
// referenceable is declared, or the person closed either window.
bool ibShowPredefinedSelector(ibControlFrame* ownerValue, const ibTypeDescription& declared,
	const ibMetaData* metaData);

#endif // __FRMSERVER_SELECT_PREDEFINED_H__
