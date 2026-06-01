#include "databaseErrorReporter.h"
#include "databaseErrorCodes.h"
#include "databaseLayerException.h"   // ibDatabaseLayerException::Throw

ibDatabaseErrorReporter::ibDatabaseErrorReporter()
{
	ResetErrorCodes();
}

ibDatabaseErrorReporter::~ibDatabaseErrorReporter()
{
}

const wxString& ibDatabaseErrorReporter::GetErrorMessage()
{
	return m_strErrorMessage;
}

int ibDatabaseErrorReporter::GetErrorCode()
{
	return m_nErrorCode;
}

void ibDatabaseErrorReporter::SetErrorMessage(const wxString& strErrorMessage)
{
	m_strErrorMessage = strErrorMessage;
}

void ibDatabaseErrorReporter::SetErrorCode(int nErrorCode)
{
	m_nErrorCode = nErrorCode;
}

void ibDatabaseErrorReporter::ResetErrorCodes()
{
	m_strErrorMessage.Clear();
	m_nErrorCode = DATABASE_LAYER_OK;
}

void ibDatabaseErrorReporter::ThrowDatabaseException()
{
	// The reporter's only job is to throw. UI surface decisions belong
	// to the catch site — runtime (ibProcUnit) routes through script-
	// level `Try / Except`; non-runtime callers (startup, login dialog,
	// web handler, registry ThreadBody, worker pool task) each install
	// their own try/catch with the right output (file log, MessageBox,
	// HTTP 500, output window). The previous "Message(GetErrorMessage())
	// only when main-thread" branch was a single hard-coded sink that
	// fired inconsistently (silent on background threads, doubled with
	// the catch-site dialog on main) and pinned the reporter to
	// ibValueSystemFunction — a frontend dependency from a backend
	// layer. Removing it leaves one clean responsibility.
	//
	// Virtual dispatch into the concrete driver's classifier — FB walks
	// its isc_status array, PG/MySQL/ODBC consult SQLSTATE, SQLite stays
	// with the default Unknown. GetSqlState() comes from the same
	// per-driver override so admin logs see what the engine actually
	// reported. Kind / native_code / sqlstate / message all travel on
	// the exception; the catch site decides what to surface.

	ibDatabaseLayerException::Throw(
		ClassifyDatabaseError(m_nErrorCode),
		m_nErrorCode,
		GetSqlState(),
		m_strErrorMessage);
}

