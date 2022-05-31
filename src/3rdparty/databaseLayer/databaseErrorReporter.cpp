#include "databaseErrorReporter.h"
#include "databaseErrorCodes.h"
#include "databaseLayerException.h"

DatabaseErrorReporter::DatabaseErrorReporter()
{
	ResetErrorCodes();
}

DatabaseErrorReporter::~DatabaseErrorReporter()
{
}

const wxString& DatabaseErrorReporter::GetErrorMessage()
{
	return m_strErrorMessage;
}

int DatabaseErrorReporter::GetErrorCode()
{
	return m_nErrorCode;
}

void DatabaseErrorReporter::SetErrorMessage(const wxString& strErrorMessage)
{
	m_strErrorMessage = strErrorMessage;
}

void DatabaseErrorReporter::SetErrorCode(int nErrorCode)
{
	m_nErrorCode = nErrorCode;
}

void DatabaseErrorReporter::ResetErrorCodes()
{
	m_strErrorMessage = _("");
	m_nErrorCode = DATABASE_LAYER_OK;
}

#include "compiler/systemObjects.h"
#include "frontend/mainFrame.h"

void DatabaseErrorReporter::ThrowDatabaseException()
{
#ifdef _DEBUG
	wxLogDebug(GetErrorMessage());
#endif 

	if (CMainFrame::Get()) {
		CSystemObjects::Message(GetErrorMessage());
	}

#ifndef DONT_USE_DATABASE_LAYER_EXCEPTIONS 
	DatabaseLayerException error(GetErrorCode(), GetErrorMessage());
	throw error;
#endif
}

