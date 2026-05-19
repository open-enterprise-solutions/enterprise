/////////////////////////////////////////////////////////////////////////////
// ibXmlWriter — P1 skeleton (see xmlReader.cpp).
/////////////////////////////////////////////////////////////////////////////

#include "xmlWriter.h"

bool ibXmlWriter::SaveMetaObject(const ibValueMetaObject&, const wxString&)
{
	m_lastError = wxT("ibXmlWriter::SaveMetaObject (path): not implemented in P1");
	return false;
}

bool ibXmlWriter::SaveMetaObject(const ibValueMetaObject&, wxOutputStream&)
{
	m_lastError = wxT("ibXmlWriter::SaveMetaObject (stream): not implemented in P1");
	return false;
}

bool ibXmlWriter::SaveForm(const ibValueMetaObjectFormBase&, const wxString&)
{
	m_lastError = wxT("ibXmlWriter::SaveForm (path): not implemented in P1");
	return false;
}

bool ibXmlWriter::SaveForm(const ibValueMetaObjectFormBase&, wxOutputStream&)
{
	m_lastError = wxT("ibXmlWriter::SaveForm (stream): not implemented in P1");
	return false;
}

bool ibXmlWriter::SaveTableDoc(const ibSpreadsheetDescription&, const wxString&)
{
	m_lastError = wxT("ibXmlWriter::SaveTableDoc (path): not implemented in P1");
	return false;
}

bool ibXmlWriter::SaveTableDoc(const ibSpreadsheetDescription&, wxOutputStream&)
{
	m_lastError = wxT("ibXmlWriter::SaveTableDoc (stream): not implemented in P1");
	return false;
}
