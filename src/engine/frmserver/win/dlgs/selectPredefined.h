#ifndef __FRMSERVER_SELECT_PREDEFINED_H__
#define __FRMSERVER_SELECT_PREDEFINED_H__

////////////////////////////////////////////////////////////////////////////
// THE DESIGNER'S DECLARED-VALUE CHOICE — asked of the thin client
////////////////////////////////////////////////////////////////////////////
//
// At run time a reference is chosen from the ROWS. While a configuration is being WRITTEN there
// are none, so the same Select button asks what the configuration DECLARES: EmptyRef, a predefined
// element, an enumeration member. The desktop does that in-process (ibShowPredefinedSelector); the
// thin designer asks through ibProtocol::ibRequest::ibKind::PredefinedSelector and writes the answer
// into the control.
////////////////////////////////////////////////////////////////////////////

#include "frmserver/frmserver.h"
#include "core/description/typeDescription.h"

class ibControlFrame;
class ibMetaData;

// Answers true when a value was chosen and written into the control; false when nothing
// referenceable is declared, the person cancelled, or nobody to ask.
bool ibShowPredefinedSelector(ibControlFrame* ownerValue, const ibTypeDescription& declared,
	const ibMetaData* metaData);

#endif // __FRMSERVER_SELECT_PREDEFINED_H__
