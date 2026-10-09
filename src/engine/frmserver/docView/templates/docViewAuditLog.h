#ifndef _DOCVIEW_AUDIT_LOG_H__
#define _DOCVIEW_AUDIT_LOG_H__

// Registration journal — doc/view tab of a client. Data side (ibLoggerReader) lives on the document; the view
// shows it.
//
// Registration journal does not carry a metaobject — it is a standalone tool tab, the plain ibDocument/ibView
// pair, registered by the ibDocManager ctor.

#include "frmserver/docView/docView.h"

#include "backend/logger/loggerReader.h"

#include <memory>

class FRMSERVER_API ibAuditLogDocument : public ibDocument {
public:
	ibAuditLogDocument();

	ibLoggerReader* GetReader() const { return m_reader.get(); }

	// The journal is read-only — never prompts on close, never tracks dirty state.
	bool IsModified() const override { return false; }
	void Modify(bool) override {}

protected:
	bool DoSaveDocument(const wxString&) override { return true; }
	bool DoOpenDocument(const wxString&) override { return true; }

private:
	std::unique_ptr<ibLoggerReader> m_reader;

	wxDECLARE_NO_COPY_CLASS(ibAuditLogDocument);
	wxDECLARE_DYNAMIC_CLASS(ibAuditLogDocument);
};

class FRMSERVER_API ibAuditLogView : public ibView {
public:
	ibAuditLogView() : ibView() {}

	void OnDraw(ibDataNode& frame) override;

private:
	wxDECLARE_DYNAMIC_CLASS(ibAuditLogView);
};

#endif
