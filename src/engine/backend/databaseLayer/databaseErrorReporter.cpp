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

const ibString& ibDatabaseErrorReporter::GetErrorMessage()
{
	return m_strErrorMessage;
}

int ibDatabaseErrorReporter::GetErrorCode()
{
	return m_nErrorCode;
}

void ibDatabaseErrorReporter::SetErrorMessage(const ibString& strErrorMessage)
{
	m_strErrorMessage = strErrorMessage;
}

void ibDatabaseErrorReporter::SetErrorCode(int nErrorCode)
{
	SetErrorCode(nErrorCode, ibBackendDatabaseException::Kind::Unknown);
}

void ibDatabaseErrorReporter::SetErrorCode(int nErrorCode, ibBackendDatabaseException::Kind kind)
{
	m_nErrorCode = nErrorCode;
	m_errorKind = kind;
}

void ibDatabaseErrorReporter::ResetErrorCodes()
{
	// Every read of every field and every fetch starts here, and there is almost never a message to
	// forget — so an empty one is left alone: clearing a string, even an empty one, drops every
	// iterator it ever had under a checked build's global lock (stack samples 2026-09-12).
	if (!m_strErrorMessage.empty())
		m_strErrorMessage.Clear();
	m_nErrorCode = DATABASE_LAYER_OK;
	m_errorKind = ibBackendDatabaseException::Kind::Unknown;
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
	// The Kind is the one the driver recorded with the code, where it read the error (SetErrorCode).
	// It used to be asked here of a virtual classifier, which only the layers answered: a statement,
	// a wrapper or a result set threw Unknown, and a caller that needed "key violation" or "lock
	// wait" re-read driver codes on its own (#247). GetSqlState() is still the driver's, so admin
	// logs see what the engine reported. Kind / native_code / sqlstate / message all travel on the
	// exception; the catch site decides what to surface.

	// ⭐ AN INTERRUPTED STATEMENT IS THE CANCEL, SAID AS ONE — the platform's own interruption. Somebody stopped
	// it (ibSession::Cancel — a closed report, a cancelled run, an ended session); thrown as a database failure
	// it would be shown as one, and every catch above would have to guess whose it was. The driver says which it
	// was — it records DATABASE_LAYER_QUERY_CANCELLED, only it knows its DBMS's word for that — and from here it
	// is the same exception as the runtime's: the interpreter walks out of its loops, the job boundary files it
	// as cancelled, a composing form says nothing.
	if (m_nErrorCode == DATABASE_LAYER_QUERY_CANCELLED)
		ibBackendInterruptException::Error();

	ibDatabaseLayerException::Throw(
		m_errorKind,
		m_nErrorCode,
		GetSqlState(),
		m_strErrorMessage);
}

