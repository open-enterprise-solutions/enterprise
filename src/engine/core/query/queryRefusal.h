#ifndef __QUERY_REFUSAL_H__
#define __QUERY_REFUSAL_H__

////////////////////////////////////////////////////////////////////////////
//	Description : THE TEXT OF A QUERY DOES NOT READ — what the query's lexer and parser give back
////////////////////////////////////////////////////////////////////////////
//
// The lexer and the parser are the core's: the engine and the thin client read a query's text alike. A text that
// does not read comes back as DATA, shaped like the translator's own refusal (ibTranslateRefusal, translateCode.h):
// where — the position and the line — and why. What a refusal IS stays the side's, said at its own door: the
// engine raises ibBackendQuerySyntaxException with that span (ibBackendQuerySyntaxException::Raise,
// backend/query/queryException.h); a window shows the message.
//
// It is thrown, because a parse cannot read on past it, and it is an ibCoreException, so a window that shows any
// refusal of the core's in one handler shows this one too — and the sentence a person reads, with the place in it, is
// the exception's own (GetErrorDescription).
//
////////////////////////////////////////////////////////////////////////////

#include "core/exception.h"   // ibCoreException

class CORE_API ibQueryRefusal : public ibCoreException {
public:

	ibQueryRefusal(unsigned int currPos, unsigned int currLine, const ibString& errorDesc) :
		ibCoreException(errorDesc), m_currPos(currPos), m_currLine(currLine)
	{
	}

	virtual ~ibQueryRefusal();

	unsigned int m_currPos;    // the character offset the token starts at
	unsigned int m_currLine;   // 1-based
};

#endif
