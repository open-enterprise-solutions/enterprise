#ifndef _DESIGNER_ASSISTANT_H__
#define _DESIGNER_ASSISTANT_H__

// THE ASSISTANT WINDOW AS THE SERVER HOLDS IT — the desktop designer's ibAssistantDocument and ibAssistantView
// (designer/docManager/templates/docViewAssistant.h): the exchange with the base's assistant access (its MCP server), as
// it happens, in a tab. The transcript is built here, where the server is, entry by entry as the desktop's window
// appended it; the client's copy of the window draws it (ViewKind Assistant) and sends what the person writes. Not the
// desktop's names — see docViewModuleEdit.h.

#include "frmserver/docView/docView.h"

#include "backend/mcp/mcpServer.h"   // ibMcpNotifier — the view is one

#include <mutex>
#include <vector>

class ibClientFrame;

// Its document — the server it shows: handed in by whoever opens the window (ibSchemaAssistant), never reached for.
// Nothing here is a file: the transcript is what happened, not something a person edits and saves.
class FRMSERVER_API ibAssistantTranscriptDocument : public ibDocument {
public:

	explicit ibAssistantTranscriptDocument(ibMcpServer* server = nullptr) : ibDocument(), m_server(server) {}

	virtual bool IsModified() const override { return false; }
	virtual void Modify(bool) override {}

	ibMcpServer* GetMcpServer() const { return m_server; }

protected:

	virtual bool DoSaveDocument(const wxString&) override { return true; }
	virtual bool DoOpenDocument(const wxString&) override { return true; }

private:

	ibMcpServer* const m_server;

	wxDECLARE_NO_COPY_CLASS(ibAssistantTranscriptDocument);
	wxDECLARE_DYNAMIC_CLASS(ibAssistantTranscriptDocument);
};

// ⭐ THE VIEW IS THE NOTIFIER. The desktop's kept a small one apart, for its widgets, which a call in flight could find
// destroyed; here there is no widget — the transcript is the view's — and the server holds no call to a notifier past
// its RemoveNotifier (ibMcpServer::Publish).
class FRMSERVER_API ibAssistantTranscriptView : public ibView, public ibMcpNotifier {
public:

	ibAssistantTranscriptView() : ibView() {}

	// What was already said, then the server's greeting — the desktop's OnCreate; and from then on every exchange.
	virtual bool OnCreate(ibDocument* doc, long flags) override;
	// The transcript, an entry a row, and whether the person waits — and on whom (State {Waiting, Pending}).
	virtual void OnDraw(ibDataNode& frame) override;
	virtual ibProtocolViewKind GetViewKind() const override { return ibProtocolViewKind::Assistant; }
	// What the person wrote (Change {Text}) — the desktop's OnSend: asked of the connected model if it can be asked,
	// left for an assistant to collect if not.
	virtual bool OnClientEvent(ibProtocolEvent event, const ibDataNode& args) override;
	// Removed from the server before the view goes — the desktop's OnClose.
	virtual bool OnClose(bool deleteWindow = true) override;

	// ⚠ ON THE SERVER'S THREAD — the entry the desktop's notifier built, kept, and the client told its frame changed.
	virtual void OnMcpExchange(const wxString& method, const wxString& request, const wxString& answer) override;

private:

	ibMcpServer* GetMcpServer() const;

	// The client this view's tab is in — told when an exchange lands (RefreshFrame), from the server's thread.
	ibClientFrame* m_frame = nullptr;

	// ⚠ WRITTEN ON THE SERVER'S THREAD, READ ON THE CLIENT'S (OnDraw) — the one place the two meet here.
	std::mutex m_transcriptMutex;
	std::vector<wxString> m_transcript;
	// ⭐ WAITING IS A STATE — from Send until an answer lands; the answer is what ends it, never a timeout.
	bool m_waiting = false;

	wxDECLARE_DYNAMIC_CLASS(ibAssistantTranscriptView);
};

#endif
