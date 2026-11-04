#include "backend/compiler/value.h"   // value_collection.h comes at its end - the collection is built on ibValue
#include "backend/backend_exception.h"

// Raised for an index out of range for ELEMENT ACCESS - everywhere, the designer included: the one
// evaluation there, the caret's walk for completion, catches what a call raises and offers nothing for it,
// which is what an index into the empty collection it built deserves anyway (census, 2026-10-01).
void ThrowErrorIndexBeyond()
{
	ibBackendCoreException::Error(_("Index goes beyond array"));
}
