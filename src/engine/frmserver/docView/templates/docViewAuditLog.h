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

class ibMetaData;

class FRMSERVER_API ibAuditLogDocument : public ibDocument {
public:
	// The configuration a row's object is opened in (GetMetaData): handed in by whoever opens the journal, never
	// reached for.
	explicit ibAuditLogDocument(const ibMetaData* metaData = nullptr);

	ibLoggerReader* GetReader() const { return m_reader.get(); }
	const ibMetaData* GetMetaData() const { return m_metaData; }

	// The journal is read-only — never prompts on close, never tracks dirty state.
	bool IsModified() const override { return false; }
	void Modify(bool) override {}

protected:
	bool DoSaveDocument(const wxString&) override { return true; }
	bool DoOpenDocument(const wxString&) override { return true; }

private:
	std::unique_ptr<ibLoggerReader> m_reader;
	const ibMetaData* m_metaData;

	wxDECLARE_NO_COPY_CLASS(ibAuditLogDocument);
	wxDECLARE_DYNAMIC_CLASS(ibAuditLogDocument);
};

// THE JOURNAL AS A CLIENT DRAWS IT — with a view of its own (the tab's Kind), the desktop's filter strip, toolbar and
// table: the rows are read here, a page at a time, and a row's object is opened here, where the configuration is.
class FRMSERVER_API ibAuditLogView : public ibView {
public:
	ibAuditLogView() : ibView() {}

	// Nothing of the journal is drawn — the client's view is its own, its toolbar's pictures the engine's.
	virtual void OnDraw(ibDataNode& WXUNUSED(frame)) override {}

	virtual ibProtocolViewKind GetViewKind() const override { return ibProtocolViewKind::AuditLog; }
	// The rows the filter selects, newest first: {From, To, Level, User, Session, Source, Event, Ref, Search, Offset,
	// Count} → a child a row {Time, Level, Session, User, Host, Source, Event, Message, Ref, RefMeta}.
	virtual bool Fetch(const ibDataNode& request, ibDataNode& response) override;

	// Open {Ref, RefMeta} — the row's object, opened as the desktop's journal opened it.
	virtual bool OnClientEvent(ibProtocolEvent event, const ibDataNode& args) override;

private:
	wxDECLARE_DYNAMIC_CLASS(ibAuditLogView);
};

#endif
