/////////////////////////////////////////////////////////////////////////////
// ibXmlReader — P1 skeleton.
//
// All entry points record a "not implemented" diagnostic and return false.
// P2 (MetaObject), P3 (Form), P4 (TableDoc) replace each body with the
// real wxXmlDocument parsing path against the corresponding XSD.
/////////////////////////////////////////////////////////////////////////////

#include "xmlReader.h"

bool ibXmlReader::LoadMetaObject(const wxString&, ibValueMetaObject&)
{
	m_lastError = wxT("ibXmlReader::LoadMetaObject (path): not implemented in P1");
	return false;
}

bool ibXmlReader::LoadMetaObject(wxInputStream&, ibValueMetaObject&)
{
	m_lastError = wxT("ibXmlReader::LoadMetaObject (stream): not implemented in P1");
	return false;
}

bool ibXmlReader::LoadForm(const wxString&, ibValueMetaObjectFormBase&)
{
	m_lastError = wxT("ibXmlReader::LoadForm (path): not implemented in P1");
	return false;
}

bool ibXmlReader::LoadForm(wxInputStream&, ibValueMetaObjectFormBase&)
{
	m_lastError = wxT("ibXmlReader::LoadForm (stream): not implemented in P1");
	return false;
}

bool ibXmlReader::LoadTableDoc(const wxString&, ibSpreadsheetDescription&)
{
	m_lastError = wxT("ibXmlReader::LoadTableDoc (path): not implemented in P1");
	return false;
}

bool ibXmlReader::LoadTableDoc(wxInputStream&, ibSpreadsheetDescription&)
{
	m_lastError = wxT("ibXmlReader::LoadTableDoc (stream): not implemented in P1");
	return false;
}
