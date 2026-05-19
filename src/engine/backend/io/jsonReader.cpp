/////////////////////////////////////////////////////////////////////////////
// ibJsonReader — P1 skeleton (see xmlReader.cpp).
/////////////////////////////////////////////////////////////////////////////

#include "jsonReader.h"

bool ibJsonReader::LoadMetaObject(const wxString&, ibValueMetaObject&)
{
	m_lastError = wxT("ibJsonReader::LoadMetaObject (path): not implemented in P1");
	return false;
}

bool ibJsonReader::LoadMetaObject(wxInputStream&, ibValueMetaObject&)
{
	m_lastError = wxT("ibJsonReader::LoadMetaObject (stream): not implemented in P1");
	return false;
}

bool ibJsonReader::LoadForm(const wxString&, ibValueMetaObjectFormBase&)
{
	m_lastError = wxT("ibJsonReader::LoadForm (path): not implemented in P1");
	return false;
}

bool ibJsonReader::LoadForm(wxInputStream&, ibValueMetaObjectFormBase&)
{
	m_lastError = wxT("ibJsonReader::LoadForm (stream): not implemented in P1");
	return false;
}

bool ibJsonReader::LoadTableDoc(const wxString&, ibSpreadsheetDescription&)
{
	m_lastError = wxT("ibJsonReader::LoadTableDoc (path): not implemented in P1");
	return false;
}

bool ibJsonReader::LoadTableDoc(wxInputStream&, ibSpreadsheetDescription&)
{
	m_lastError = wxT("ibJsonReader::LoadTableDoc (stream): not implemented in P1");
	return false;
}
