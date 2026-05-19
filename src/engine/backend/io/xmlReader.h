/////////////////////////////////////////////////////////////////////////////
// ibXmlReader — XML-format implementation of ibIOReader.
//
// Format identifier: "oes-xml-1.0". Backed by wxXmlDocument (no libxml2
// dependency). Schemas live at data/schemas/oes-{metaobject,form,tabledoc}.xsd.
//
// Loading is strict: root element must declare format="oes-xml-1.0" and
// version="1"; mismatches populate LastError() and return false. Per-entry
// schema violations skip the entry and record a diagnostic; the load
// continues so a partial corpus survives a single broken file.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_XML_READER_H_
#define _IB_XML_READER_H_

#include "ioReader.h"

class BACKEND_API ibXmlReader final : public ibIOReader {
public:
	ibXmlReader() = default;
	~ibXmlReader() override = default;

	bool LoadMetaObject(const wxString& path,
	                    ibValueMetaObject& obj) override;
	bool LoadMetaObject(wxInputStream& stream,
	                    ibValueMetaObject& obj) override;

	bool LoadForm(const wxString& path,
	              ibValueMetaObjectFormBase& form) override;
	bool LoadForm(wxInputStream& stream,
	              ibValueMetaObjectFormBase& form) override;

	bool LoadTableDoc(const wxString& path,
	                  ibSpreadsheetDescription& doc) override;
	bool LoadTableDoc(wxInputStream& stream,
	                  ibSpreadsheetDescription& doc) override;

	wxString FormatId()  const override { return wxT("oes-xml-1.0"); }
	wxString LastError() const override { return m_lastError; }

private:
	wxString m_lastError;
};

#endif // _IB_XML_READER_H_
