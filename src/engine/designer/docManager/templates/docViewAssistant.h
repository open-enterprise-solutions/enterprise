#ifndef _DOCVIEW_ASSISTANT_H__
#define _DOCVIEW_ASSISTANT_H__

// The assistant window — a doc/view tab inside the designer shell, not a
// dialog, so it stays open beside the work it is about and several of them
// could exist if that ever makes sense.
//
// WHAT IT SHOWS: the exchange with the MCP server — what was asked of this
// platform and what it answered — as it happens. A person can see what is being
// done in their name instead of inferring it from the tree changing under them.
//
// ⚠ THE OTHER HALF IS NOT BUILT. Writing here — a person's own message, with
// files attached — needs the platform to call OUT to a model, which is a
// direction that does not exist yet (no outgoing HTTP; see the roadmap's E1).
// The input row is present and disabled, with the reason on screen, because a
// box that accepts text and drops it is worse than one that says it cannot.

#include "frontend/docView/docView.h"

#include "backend/clsid.h"
#include "backend/mcp/mcpServer.h"   // ibMcpNotifier — this window is one

#include <wx/timer.h>   // wxTimer — a member below, so the type has to be complete here

#include <memory>

constexpr ibClassID g_toolAssistantCLSID = make_clsid("TL_ASST", ibClassKind_None);   // tool/doc id — not a registered type

class wxTextCtrl;
class wxButton;
class wxStyledTextCtrl;

class ibAssistantDocument : public ibDocument {
public:
	ibAssistantDocument() : ibDocument() {}

	// Nothing here is a file: the transcript is what happened, not something a
	// person edits and saves.
	bool IsModified() const override { return false; }
	void Modify(bool) override {}

protected:
	bool DoSaveDocument(const wxString&) override { return true; }
	bool DoOpenDocument(const wxString&) override { return true; }

private:
	wxDECLARE_NO_COPY_CLASS(ibAssistantDocument);
	wxDECLARE_DYNAMIC_CLASS(ibAssistantDocument);
};

class ibAssistantView : public ibView {
public:
	ibAssistantView() : ibView() {}

	bool OnCreate(ibDocument* doc, long flags) override;
	void OnUpdate(ibView* sender, wxObject* hint) override;
	void OnDraw(wxDC* dc) override;
	bool OnClose(bool deleteWindow = true) override;

private:

	void BuildLayout(wxWindow* parent);

	// Leaves what was typed on the server for the connected assistant to pick
	// up — the platform itself has no model to ask.
	void OnSend(wxCommandEvent& event);

	// Attached while the window is open, removed before it goes. The server
	// keeps raw pointers, so leaving one behind would outlive the widget it
	// writes into.
	std::unique_ptr<ibMcpNotifier> m_notifier;

	// THE TRANSCRIPT IS A CODE-EDITOR CONTROL, not a plain text box: what an
	// assistant writes is markdown, so the markdown lexer highlights exactly
	// what the AUTHOR chose to emphasise — a heading, a bold clause, a fenced
	// snippet — instead of a rule in the widget deciding for it. And it takes
	// the platform's shared font, the one the code editor uses everywhere.
	wxStyledTextCtrl* m_transcript = nullptr;
	wxTextCtrl*       m_input      = nullptr;
	wxButton*         m_send       = nullptr;

	// ⭐ WAITING IS A STATE, AND SILENCE DOES NOT SHOW IT. Between sending and being answered the
	// window looked exactly as it does when nothing is happening at all — so "it is thinking" and
	// "nobody is there" were the same picture, and the second is the one worth knowing.
	//
	// ⚠ NOT IN THE TRANSCRIPT. A line that changes four times a second does not belong in a record
	// that is scrolled back through; this is a status strip beside the input, which is where a
	// person is already looking when they have just pressed Send.
	void OnWaitingTick(wxTimerEvent& event);
	void RefreshWaiting();

public:

	// Called from the notifier when an answer lands — the arrival of the answer is what ends the
	// wait, and only the notifier knows it happened.
	void StopWaiting();

private:

	// What the strip says, and what animates it. Started on Send, stopped when an answer lands or
	// the window closes — a timer left running writes into a destroyed control.
	wxStaticText* m_waiting     = nullptr;
	wxTimer       m_waitingTick;
	int           m_waitingDots = 0;

	wxDECLARE_DYNAMIC_CLASS(ibAssistantView);
};

#endif // _DOCVIEW_ASSISTANT_H__
