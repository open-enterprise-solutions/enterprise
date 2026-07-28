#include "eventDispatcher.h"

// Out-of-line dtor — the exported vtable anchor for the interface (one typeinfo/vtable in backend.dll, so the
// cross-DLL dynamic_cast to ibEventDispatcher resolves). The concrete Dispatch bodies live ON the runtime values
// that implement this facet: ibValueEvent (see valueEvent.cpp) and ibValueFunction (see procUnitLinq.cpp).
ibEventDispatcher::~ibEventDispatcher() {}
