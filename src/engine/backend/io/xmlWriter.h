/////////////////////////////////////////////////////////////////////////////
// ibXmlWriter — XML-format implementation of ibIOWriter.
//
// Format identifier: "oes-xml-1.0". Emits a wxXmlDocument tree and serialises
// with `wxXmlDocument::Save`. Root element carries format / version
// attributes; per-entry layout matches the corresponding XSD in
// data/schemas/.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_XML_WRITER_H_
#define _IB_XML_WRITER_H_

#include "ioWriter.h"

class BACKEND_API ibXmlWriter final : public ibIOWriter {
public:
	ibXmlWriter() = default;
	~ibXmlWriter() override = default;

	bool SaveMetaObject(const ibValueMetaObject& obj,
	                    const wxString& path) override;
	bool SaveMetaObject(const ibValueMetaObject& obj,
	                    wxOutputStream& stream) override;

	bool SaveForm(const ibValueMetaObjectFormBase& form,
	              const wxString& path) override;
	bool SaveForm(const ibValueMetaObjectFormBase& form,
	              wxOutputStream& stream) override;

	bool SaveTableDoc(const ibSpreadsheetDescription& doc,
	                  const wxString& path) override;
	bool SaveTableDoc(const ibSpreadsheetDescription& doc,
	                  wxOutputStream& stream) override;

	wxString FormatId()  const override { return wxT("oes-xml-1.0"); }
	wxString LastError() const override { return m_lastError; }

private:
	wxString m_lastError;
};

#endif // _IB_XML_WRITER_H_
