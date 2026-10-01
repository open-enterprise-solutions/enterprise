#ifndef __IB_COMPOSE_EVALUATE_H__
#define __IB_COMPOSE_EVALUATE_H__

////////////////////////////////////////////////////////////////////////////
//	Description : a composition parameter that is WORKED OUT rather than stated
////////////////////////////////////////////////////////////////////////////
//
// ⭐⭐ A HOME OF ITS OWN, BECAUSE IT HAD TWO AND THEY WERE OBLIGED TO AGREE. The same helper stood
// file-local in `composeRunSchema.cpp` (a composition read over the wire) and static in
// `valueDataComposition.cpp` (a composition run from script), each with a comment pointing at the
// other. Two copies of "where does an expression evaluate" is two places for the answer to drift,
// and it drifted: one had learnt the designer's edit manager and the other had not.
//
// ⚠ NOT IN `compositionDescription.{h,cpp}`, though ibStoredValue lives there and looks like a
// neighbour. A DESCRIPTION IS DATA — it is read while a configuration is still loading — and this
// needs the runtime: a session, a module manager, a ProcUnit, a frame. Putting the two together
// would drag the runtime into the load, which is the mistake that file's own notes are about.
//
// A parameter has three roads (compose_run's contract says so in as many words): a literal, the
// PACKED form, and an EXPRESSION. Only the third one is here, and only it needs a runtime — a date,
// a rate, an item found by its code mean nothing outside the application that holds the data.

#include "backend/backend.h"

class ibValue;
class ibMetaData;

// Evaluate `expression` where the data is, and answer whether it produced anything.
//
// False means it could NOT be evaluated, and `produced` then carries the reason as a string — a
// caller must refuse rather than carry on: an expression that failed and an expression that
// legitimately came out empty are the same emptiness afterwards, and a report composed on an
// unfilled parameter looks exactly like a report with no data.
//
// `metaData` is the configuration to fall back on when there is no runtime root — the designer's
// edit manager compiles against it (ibSession::EditModuleManagerFor). It may be null.
BACKEND_API bool ibEvaluateInRoot(const wxString& expression, ibValue& produced,
	const ibMetaData* metaData);

#endif
