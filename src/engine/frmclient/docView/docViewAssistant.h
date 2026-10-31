#ifndef _DOCVIEW_ASSISTANT_H__
#define _DOCVIEW_ASSISTANT_H__

// The assistant window — the desktop's (designer/docManager/templates/docViewAssistant.h) IN A TAB THE SERVER RAISED
// (Kind Assistant): the exchange with the base's assistant access, as it happens. The server holds the window
// (frmserver ibAssistantTranscriptView) — it watches the exchange and builds the transcript, an entry a row, and says
// whether the person waits; this one draws it and sends what the person writes (Change {Text}).

#include "frmclient/docView/tabDocument.h"

#include <wx/timer.h>   // wxTimer — a member below, so the type has to be complete here

class wxTextCtrl;
class wxButton;
class wxStaticText;
class wxStyledTextCtrl;

class ibAssistantDocument : public ibTabDocument {
public:

	explicit ibAssistantDocument(long long tabId) : ibTabDocument(tabId) {}

	// Nothing here is a file: the transcript is what happened, not something a
	// person edits and saves.
	virtual bool IsModified() const override { return false; }
	virtual void Modify(bool) override {}

	// The transcript and the waiting — the view's Rows and State.
	virtual void DrawView(const ibProtocolNode& view, const ibProtocolNode& patch) override;

protected:

	virtual ibFrontendView* DoCreateView() override;

private:

	wxDECLARE_NO_COPY_CLASS(ibAssistantDocument);
};

class ibAssistantView : public ibFrontendView {
public:

	ibAssistantView() : ibFrontendView() {}

	virtual bool OnCreate(ibFrontendDocument* doc, long flags) override;
	virtual void OnDraw(wxDC* dc) override;
	virtual bool OnClose(bool deleteWindow = true) override;

	// THE SERVER'S VIEW OF THE WINDOW — the entries not shown yet appended, and the waiting as it says.
	void Draw(const ibProtocolNode& view, const ibProtocolNode& patch);

	// The server says the wait is over — the arrival of the answer is what ends it.
	void StopWaiting();

private:

	void BuildLayout(wxWindow* parent);

	// Sends what was typed to the server, which asks the connected model or
	// leaves it for an assistant to collect — the platform itself has no model to ask.
	void OnSend(wxCommandEvent& event);

	// ⭐ WAITING IS A STATE, AND SILENCE DOES NOT SHOW IT — the server's (State Waiting): from Send until an answer lands.
	//
	// ⚠ NOT IN THE TRANSCRIPT. A line that changes four times a second does not belong in a record
	// that is scrolled back through; this is a status strip beside the input, which is where a
	// person is already looking when they have just pressed Send.
	void OnWaitingTick(wxTimerEvent& event);
	void RefreshWaiting();

	// THE TRANSCRIPT IS A CODE-EDITOR CONTROL, not a plain text box: what an
	// assistant writes is markdown, so the markdown lexer highlights exactly
	// what the AUTHOR chose to emphasise — a heading, a bold clause, a fenced
	// snippet — instead of a rule in the widget deciding for it. And it takes
	// the platform's shared font, the one the code editor uses everywhere.
	wxStyledTextCtrl* m_transcript = nullptr;
	wxTextCtrl*       m_input      = nullptr;
	wxButton*         m_send       = nullptr;

	// The entries of the server's transcript already shown — it only grows.
	size_t m_shownEntries = 0;

	// What the strip says, and what animates it — the dots only: what it says is the server's. Stopped when the wait
	// ends or the window closes — a timer left running writes into a destroyed control.
	wxStaticText* m_waiting     = nullptr;
	wxTimer       m_waitingTick;
	int           m_waitingDots = 0;
	// The messages no assistant has collected — the server's (State Pending).
	long long     m_pendingCount = 0;
};

#endif // _DOCVIEW_ASSISTANT_H__
