/////////////////////////////////////////////////////////////////////////////
// ibJsonWriter — JSON-format implementation of ibIOWriter.
//
// Format identifier: "oes-json-1.0". Emits via nlohmann/json's `dump(2)`
// for stable 2-space indentation. Root object carries `format` /
// `version` keys.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_JSON_WRITER_H_
#define _IB_JSON_WRITER_H_

#include "ioWriter.h"

class BACKEND_API ibJsonWriter final : public ibIOWriter {
public:
	ibJsonWriter() = default;
	~ibJsonWriter() override = default;

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

	wxString FormatId()  const override { return wxT("oes-json-1.0"); }
	wxString LastError() const override { return m_lastError; }

private:
	wxString m_lastError;
};

#endif // _IB_JSON_WRITER_H_
