////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : debug-time tracing switches, off unless asked for
////////////////////////////////////////////////////////////////////////////

#ifndef _DEBUG_TRACE_H__
#define _DEBUG_TRACE_H__

#include <cstdlib>

// The live-object traces (ibValue's Create/Delete counter, ibPropertyObject's, the type-factory's
// register/unregister log) were written for a leak hunt and left ON for every Debug build. One
// designer run costs ~18000 lines of them, which is 72% of the log — enough that the lines worth
// reading are the ones you cannot find.
//
// Turning them into environment switches keeps the instrument and drops the noise, the same shape
// the leak tracker already uses (OES_TRACK_*, see designer/mainApp.cpp): dormant by default, one
// variable away when a question needs it.
//
//     set OES_TRACE_VALUES=1     every ibValue construction / destruction, with a live count
//     set OES_TRACE_PROPS=1      the same for ibPropertyObject
//     set OES_TRACE_TYPES=1      every value-ctor registered into / removed from the factory
//
// Read ONCE into a static: getenv is not free, and these sit on paths that run tens of thousands
// of times per run.
inline bool ibDebugTraceEnabled(const char* variable)
{
	const char* const value = std::getenv(variable);
	return value != nullptr && *value != '\0' && *value != '0';
}

#endif
