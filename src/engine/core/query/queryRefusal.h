#ifndef __QUERY_REFUSAL_H__
#define __QUERY_REFUSAL_H__

////////////////////////////////////////////////////////////////////////////
//	Description : THE TEXT OF A QUERY DOES NOT READ — how the query's lexer and parser refuse
////////////////////////////////////////////////////////////////////////////
//
// The lexer and the parser are the core's: the engine and the thin client read a query's text alike. What a refusal
// IS stays the side's — the engine's is ibBackendQuerySyntaxException, which its handlers catch by type and read the
// span of (backend/query/queryException.h). So the core says where and why, and the side that runs the parser raises
// its own: the engine gives its raise when it loads (backend/query/queryException.cpp). With none given — the thin
// client — the core's own refusal is raised (ibCoreException), with the message that carries the place.
//
////////////////////////////////////////////////////////////////////////////

#include "core/core.h"   // CORE_API

#include <wx/string.h>

class CORE_API ibQueryRefusal {
public:

	// How a side raises a syntax refusal: `line` 1-based, `col` the offset the token starts at. Never returns.
	using ibRaise = void (*)(unsigned int line, unsigned int col, const wxString& message);

	// THE TEXT DOES NOT READ, here — raised as the side raises it. Never returns.
	[[noreturn]] static void Syntax(unsigned int line, unsigned int col, const wxString& message);

	// The side's raise — given once, when the side's library loads.
	static void SetRaise(ibRaise raise);
};

#endif
