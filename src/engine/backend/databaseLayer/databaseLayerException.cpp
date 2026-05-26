#include "databaseLayerException.h"

// Out-of-line definitions for the DB-tier exception throwers. Living
// next to the header (instead of inside backend_exception.cpp) keeps
// the database-layer-specific concerns physically co-located with the
// rest of the driver code. The base ibBackendException's ctor (which
// pushes onto the per-thread error chain) is reached transparently
// through the derived ctor chain.

bool ibBackendDatabaseException::IsRetryable() const
{
	return m_kind == Kind::Deadlock
	    || m_kind == Kind::Timeout
	    || m_kind == Kind::ConnectionLost;
}

void ibBackendDatabaseException::Throw(Kind kind, const wxString& msg)
{
	throw ibBackendDatabaseException(kind, msg);
}

int ibDatabaseLayerException::GetDriverErrorCode() const
{
	return m_nativeCode;
}

const wxString& ibDatabaseLayerException::GetSqlState() const
{
	return m_sqlState;
}

void ibDatabaseLayerException::Throw(Kind kind, int nativeCode,
                                      const wxString& sqlState,
                                      const wxString& msg)
{
	throw ibDatabaseLayerException(kind, nativeCode, sqlState, msg);
}
