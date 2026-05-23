#include "databaseErrorReporter.h"
#include "databaseErrorCodes.h"
#include "databaseLayerException.h"

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

#include "backend/backend_exception.h"
#include "backend/system/systemManager.h"

#include <wx/thread.h>

void ibDatabaseErrorReporter::ThrowDatabaseException()
{
	// Only push to the GUI message panel from the main (GUI) thread.
	// `ibValueSystemFunction::Message` chains into the frontend
	// output window (wxStyledTextCtrl::AppendText), which fires
	// Scintilla notifications that propagate to wxWidgets event
	// dispatch. Driving that from a non-main thread (e.g. our
	// session-registry `JobRefreshSnapshot` background worker
	// surfacing a DB error after the leader vanished) races with
	// the main thread's idle handler over the wxAuiMDIParentFrame
	// recursion guard, fires `wxASSERT(m_flag > 0)` from
	// `~wxRecursionGuard` ("unbalanced wxRecursionGuards!?") and
	// crashes. Background errors still surface via the C++ exception
	// thrown below — caller's catch handler can route the message
	// to the UI on the right thread if it wants to.
	if (wxThread::IsMain())
		ibValueSystemFunction::Message(GetErrorMessage());

#if _USE_DATABASE_LAYER_EXCEPTIONS == 0
	try {
#endif
		ibBackendCoreException::Error(GetErrorMessage());
#if _USE_DATABASE_LAYER_EXCEPTIONS == 0
	}
	catch (...) {
	}
#endif
}

