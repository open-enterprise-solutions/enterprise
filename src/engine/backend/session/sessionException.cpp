#include "backend/session/sessionException.h"

// Thrown by value, caught by const reference (project convention).

void ibBackendSessionException::Throw(Kind kind, const ibString& message)
{
	throw ibBackendSessionException(kind, message);
}
