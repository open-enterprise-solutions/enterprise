#include "databaseErrorReporter.h"
#include "databaseErrorCodes.h"
#include "databaseLayerException.h"

CDatabaseErrorReporter::CDatabaseErrorReporter()
{
	ResetErrorCodes();
}

CDatabaseErrorReporter::~CDatabaseErrorReporter()
{
}

const wxString& CDatabaseErrorReporter::GetErrorMessage()
{
	return m_strErrorMessage;
}

int CDatabaseErrorReporter::GetErrorCode()
{
	return m_nErrorCode;
}

void CDatabaseErrorReporter::SetErrorMessage(const wxString& strErrorMessage)
{
	m_strErrorMessage = strErrorMessage;
}

void CDatabaseErrorReporter::SetErrorCode(int nErrorCode)
{
	m_nErrorCode = nErrorCode;
}

void CDatabaseErrorReporter::ResetErrorCodes()
{
	m_strErrorMessage = _("");
	m_nErrorCode = DATABASE_LAYER_OK;
}

#include "backend/backend_exception.h"
#include "backend/systemManager/systemManager.h"

void CDatabaseErrorReporter::ThrowDatabaseException()
{
	CSystemFunction::Message(GetErrorMessage());

#if _USE_DATABASE_LAYER_EXCEPTIONS == 0
	try {
#endif
		CBackendException::Error(GetErrorMessage());
#if _USE_DATABASE_LAYER_EXCEPTIONS == 0
	}
	catch (...) {
	}
#endif
}

