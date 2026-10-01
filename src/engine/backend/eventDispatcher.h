#ifndef __EVENT_DISPATCHER_H__
#define __EVENT_DISPATCHER_H__

#include "backend/backend_core.h"      // BACKEND_API

class ibValue;
class ibProcUnit;

// A PURE INTERFACE — the "callable as an event" facet. It is NOT a wrapper class of its own: the RUNTIME VALUES
// implement it directly, so each is SIMULTANEOUSLY the value AND its own dispatcher — no reinvented wheel:
//   ibValueEvent    (the named-event runtime value) IS-A dispatcher -> Dispatch runs the named form procedure;
//   ibValueFunction (a lambda value)                IS-A dispatcher -> Dispatch runs its own body.
// The single fire site (ibValueFrame::CallAsEvent) takes whatever value the event holds, asks it as an
// ibEventDispatcher, and calls Dispatch — staying agnostic. Both the classic event and a lambda flow through here
// by plain polymorphism.
class BACKEND_API ibEventDispatcher {
public:
	virtual ~ibEventDispatcher();

	// Dispatch on the CURRENT runtime with the current args — an ACTION (it runs code), not a query, so non-const (the
	// lambda invoke machinery is non-const, and const_cast is off the table). `outCancel` is the trailing cancel flag
	// the target's code may set by reference (the classic event contract); the return value IS that flag (true =
	// cancelled). `runtime` is the FORM's procUnit — the named value calls a form procedure through it; the lambda
	// value ignores it and resolves the session's lambda runtime itself. args[0..argc-1] are the event parameters.
	virtual bool Dispatch(ibProcUnit* runtime, ibValue** args, long argc, ibValue& outCancel) = 0;

	// A dispatcher that resolves to nothing (empty name) — CallAsEvent treats it as a no-op (event proceeds).
	virtual bool IsEmpty() const { return false; }
};

#endif // !__EVENT_DISPATCHER_H__
