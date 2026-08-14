#include "backend/session/sessionException.h"

// Thrown by value, caught by const reference (project convention).

void ibBackendSessionException::Throw(Kind kind, const wxString& message)
{
	throw ibBackendSessionException(kind, message);
}
