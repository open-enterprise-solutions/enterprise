#ifndef _SELECT_PREDEFINED_WND_H__
#define _SELECT_PREDEFINED_WND_H__

////////////////////////////////////////////////////////////////////////////
// THE DESIGNER'S CHOICE OF A DECLARED VALUE
////////////////////////////////////////////////////////////////////////////
//
// At run time a reference is chosen from the ROWS — the metaobject's own selection form. While a
// configuration is being WRITTEN there are none, and what a person needs to say is one of the things
// the configuration DECLARES: the empty reference of a type, a predefined element, an enumeration
// member. That is a different window, and this is it.
//
// It asks two questions with the product's existing pieces: WHICH TYPE, through the same "Select
// data type" list every other road opens — where every reference the declaration admits stands as
// ONE technical entry (`CompositionPredefinedValue`) — and then WHICH DECLARED VALUE, as a tree with
// a branch per admitted reference type.
//
// ⚠ IT LIVES HERE, beside the type list it uses, and NOT in typeControl.cpp. That translation unit
// decides which ROAD a Select button takes; a dialog's widgets, tree filling and modal are not that
// decision, and putting them there made a router into a window (Max, 2026-08-28).
////////////////////////////////////////////////////////////////////////////

#include "frontend/frontend.h"
#include "backend/typeDescription.h"

class BACKEND_API ibMetaData;
class FRONTEND_API ibControlFrame;

// Answers `true` when a value was chosen and written into the control; `false` when nothing
// referenceable is declared, or the person closed either window.
bool ibShowPredefinedSelector(ibControlFrame* ownerValue, const ibTypeDescription& declared,
	const ibMetaData* metaData, wxWindow* parent);

#endif // _SELECT_PREDEFINED_WND_H__
