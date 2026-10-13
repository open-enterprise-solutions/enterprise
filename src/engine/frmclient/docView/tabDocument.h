#ifndef _FRMCLIENT_TAB_DOCUMENT_H__
#define _FRMCLIENT_TAB_DOCUMENT_H__

#include "frmclient/docView/docView.h"
#include "protocol/protocol.h"
#include "protocol/protocolNode.h"

// ----------------------------------------------------------------------------
// THE DOCUMENT OF A TAB THE SERVER RAISED — what every tab is, whatever its view: the tab it is (by its id), closed and
// saved by the server, modified and taking commands as the tab says. ⚠ NO TEMPLATE, as the desktop's had none: made by
// the window for each tab the frame brings (ibFrontendMainFrame::DrawTabs), each kind with a view of its own — a form
// (ibFormVisualDocument), the registration journal (ibAuditLogDocument).
// ----------------------------------------------------------------------------

class FRMCLIENT_API ibTabDocument : public ibFrontendDocument {
public:

	explicit ibTabDocument(long long tabId);
	// …and gone out of the frame's tabs (ibFrontendMainFrame::RemoveDocument).
	virtual ~ibTabDocument();

	long long GetTabId() const { return m_tabId; }

	virtual wxString GetUniqueIdentifier() const override;

	virtual bool OnCloseDocument() override;

	// Closed with the form that owns it when the server closed it too — its tab is gone from the frame; one the server
	// kept goes on under the doc manager.
	virtual bool IsCloseOnOwnerClose() const override;

	// ⚠ THE SERVER'S ANSWER: modified is "the tab takes Save now".
	virtual bool IsModified() const override;
	virtual void Modify(bool WXUNUSED(modify)) override {}

	// The server closes it, asking about what is unsaved — and may keep it; the tab goes when the frame says so. A tab
	// the frame no longer has was closed there: the document goes with it.
	virtual bool Close() override;
	// A form is saved where it is, by the server; a document of a file, where the person keeps it — asked where the first
	// time (Save as), saved by the server as a file of that name and taken down to it (DoSaveDocument).
	virtual bool Save() override;
	virtual bool SaveAs() override;
	// What it saves as — the server's, the tab's Formats; empty: a form.
	virtual wxString GetSaveFilter() const override;

	// What is unsaved the server asks about, on its own close.
	virtual bool OnSaveModified() override { return true; }
	virtual void OnSaveBeforeForceClose() override {}

	// THE FRAME'S VIEW OF THE TAB, drawn while it is the active one — `patch`: what the answer changed of it; none — the
	// whole view. A view of its own (the journal's) reads itself, and draws nothing of the frame's.
	virtual void DrawView(const ibProtocolNode& WXUNUSED(view), const ibProtocolNode& WXUNUSED(patch)) {}

	// Does the tab take this command now — the server's answer, the tab's Commands.
	bool IsCommandEnabled(ibProtocolCommand command) const;
	// The command done to it, by the server — after whatever the window is doing now.
	void DoCommand(ibProtocolCommand command) const;

protected:

	virtual bool DoSaveDocument(const wxString& file) override;

private:

	const long long m_tabId;
};

#endif
