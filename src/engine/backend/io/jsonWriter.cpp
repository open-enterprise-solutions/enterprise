/////////////////////////////////////////////////////////////////////////////
// ibJsonWriter — P1 skeleton (see xmlReader.cpp).
/////////////////////////////////////////////////////////////////////////////

#include "jsonWriter.h"

bool ibJsonWriter::SaveMetaObject(const ibValueMetaObject&, const wxString&)
{
	m_lastError = wxT("ibJsonWriter::SaveMetaObject (path): not implemented in P1");
	return false;
}

bool ibJsonWriter::SaveMetaObject(const ibValueMetaObject&, wxOutputStream&)
{
	m_lastError = wxT("ibJsonWriter::SaveMetaObject (stream): not implemented in P1");
	return false;
}

bool ibJsonWriter::SaveForm(const ibValueMetaObjectFormBase&, const wxString&)
{
	m_lastError = wxT("ibJsonWriter::SaveForm (path): not implemented in P1");
	return false;
}

bool ibJsonWriter::SaveForm(const ibValueMetaObjectFormBase&, wxOutputStream&)
{
	m_lastError = wxT("ibJsonWriter::SaveForm (stream): not implemented in P1");
	return false;
}

bool ibJsonWriter::SaveTableDoc(const ibSpreadsheetDescription&, const wxString&)
{
	m_lastError = wxT("ibJsonWriter::SaveTableDoc (path): not implemented in P1");
	return false;
}

bool ibJsonWriter::SaveTableDoc(const ibSpreadsheetDescription&, wxOutputStream&)
{
	m_lastError = wxT("ibJsonWriter::SaveTableDoc (stream): not implemented in P1");
	return false;
}
