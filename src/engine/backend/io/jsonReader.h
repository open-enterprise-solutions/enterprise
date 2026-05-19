/////////////////////////////////////////////////////////////////////////////
// ibJsonReader — JSON-format implementation of ibIOReader.
//
// Format identifier: "oes-json-1.0". Backed by nlohmann/json v3.11.3.
// Per-entry shape parallels the XML schema so a JSON artifact is a
// transliteration of the corresponding XML artifact (element name →
// object key, attribute → property, text content → "value"-key string).
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_JSON_READER_H_
#define _IB_JSON_READER_H_

#include "ioReader.h"

class BACKEND_API ibJsonReader final : public ibIOReader {
public:
	ibJsonReader() = default;
	~ibJsonReader() override = default;

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

	wxString FormatId()  const override { return wxT("oes-json-1.0"); }
	wxString LastError() const override { return m_lastError; }

private:
	wxString m_lastError;
};

#endif // _IB_JSON_READER_H_
