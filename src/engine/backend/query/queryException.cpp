#include "backend/query/queryException.h"

// Thrown by value, caught by const reference (project convention).

// ⭐ THE ENGINE'S DOOR FOR THE CORE'S PARSER: what the text could not be read as, raised as the engine raises it — so
// every handler that catches ibBackendQuerySyntaxException by type, and reads its span, still does.
void ibBackendQuerySyntaxException::Raise(const ibQueryRefusal& refusal)
{
	throw ibBackendQuerySyntaxException(refusal.GetErrorDescription(), refusal.m_currLine, refusal.m_currPos);
}

void ibBackendQueryException::Throw(Kind kind, const ibString& message)
{
	throw ibBackendQueryException(kind, message);
}
