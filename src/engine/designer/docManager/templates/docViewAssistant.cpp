#include "docViewAssistant.h"

#include "backend/appData.h"
#include "mainFrame/mainFrameDesigner.h"   // the shared font/colour settings live on the frame

#include <wx/app.h>
#include <wx/button.h>
#include <wx/datetime.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/stattext.h>
#include <wx/stc/stc.h>
#include <wx/textctrl.h>
#include <wx/weakref.h>

wxIMPLEMENT_DYNAMIC_CLASS(ibAssistantDocument, ibDocument);
wxIMPLEMENT_DYNAMIC_CLASS(ibAssistantView, ibView);

namespace {

// THE WINDOW AS A NOTIFIER. Kept apart from the view so the thing the server
// holds a raw pointer to is a small object with one job, and so the rule below
// is stated once.
// APPENDING TO A READ-ONLY STC. Scintilla's read-only flag refuses EVERY
// modification, our own included — so the flag comes off for the write and goes
// straight back on. Without this the transcript silently stays empty, which
// reads as "nothing happened" rather than as "the control refused".
void AppendTranscript(wxStyledTextCtrl* transcript, const wxString& text)
{
	if (transcript == nullptr)
		return;

	transcript->SetReadOnly(false);
	transcript->AppendText(text);
	transcript->SetReadOnly(true);

	// Follow the tail, the way a log window does.
	transcript->GotoPos(transcript->GetLength());
}

class ibAssistantNotifier : public ibMcpNotifier {
public:

	ibAssistantNotifier(wxStyledTextCtrl* transcript, ibAssistantView* owner)
		: m_transcript(transcript), m_owner(owner) {}

	void OnMcpExchange(const wxString& method, const wxString& request, const wxString& answer) override
	{
		// ⚠ THIS RUNS ON THE SERVER'S THREAD. A widget belongs to the main one,
		// so nothing here touches it: the line is built and handed over, and the
		// append happens where it is legal.
		//
		// The reference is WEAK — a queued append that arrives after the tab is
		// closed finds a null and does nothing, instead of writing into a
		// destroyed control. (The notifier is removed on close, but a call
		// already in flight has already passed that point.)
		wxWeakRef<wxStyledTextCtrl> target = m_transcript;

		// A TRANSCRIPT, NOT A WIRE LOG. Three shapes, and each is what its reader
		// needs: what a person said, what the assistant answered, and a single
		// line for anything the assistant DID along the way. Every entry carries
		// the time, because a conversation that accumulates over an afternoon is
		// read by when things happened.
		const wxString at = wxDateTime::Now().FormatISOTime().Left(5);   // HH:MM

		wxString line;

		if (method == wxT("you")) {
			line << wxT("## ") << at << wxT(" · ") << _("You") << wxT("\n\n");
			line << request << wxT("\n\n");
		}
		else if (method == wxT("assistant")) {
			line << wxT("## ") << at << wxT(" · ") << _("Assistant") << wxT("\n\n");
			line << answer << wxT("\n\n");
		}
		else if (method == wxT("did")) {
			// ⭐ A HEADLINE, AND UNDER IT WHAT THERE IS TO READ. The line alone answers "what was
			// touched", which is what makes a running log scannable — but when the thing touched
			// IS text a person would otherwise open an editor to see (a module, a note), that
			// text belongs here, already written. The tool decides which of its arguments is
			// worth showing; most have nothing and stay one line, exactly as before.
			//
			// ⚠ Still not the JSON. What is under the headline is the SUBSTANCE, never the wire.
			line << wxT("> ") << at << wxT(" · ") << request << wxT("\n\n");

			if (!answer.IsEmpty())
				line << answer << wxT("\n");
		}
		else {
			line << wxT("> ") << at << wxT(" · ") << method << wxT("\n\n");
		}

		// ⚠ THE OWNER IS TOUCHED ONLY IF THE TRANSCRIPT IS STILL THERE. The view removes this
		// notifier before its widgets go, so a call already in flight is the one case left — and
		// the weak reference that guards the control guards the view with it, since neither
		// outlives the other.
		const bool answered = method == wxT("assistant");
		ibAssistantView* const owner = m_owner;

		wxTheApp->CallAfter([target, line, owner, answered]() {
			if (!target)
				return;

			AppendTranscript(target, line);

			if (answered && owner != nullptr)
				owner->StopWaiting();
		});
	}

private:
	wxWeakRef<wxStyledTextCtrl> m_transcript;
	ibAssistantView*            m_owner = nullptr;
};

} // namespace

bool ibAssistantView::OnCreate(ibDocument* doc, long flags)
{
	BuildLayout(m_viewFrame);

	// The strip's heartbeat. Owned by the view so the tick arrives here, and stopped in OnClose —
	// a timer outliving its label writes into a destroyed control.
	m_waitingTick.SetOwner(this);
	Bind(wxEVT_TIMER, &ibAssistantView::OnWaitingTick, this);

	// ATTACHED ONLY WHILE THE WINDOW IS OPEN. The server keeps raw pointers to
	// its notifiers, so this pairs with the removal in OnClose.
	if (ibMcpServer* server = ibApplicationData::GetMcpServer()) {

		// ⭐ WHAT WAS ALREADY SAID, BEFORE ANYTHING NEW. This window used to be filled ONLY by
		// live notifications, so closing the tab threw the conversation away and reopening it
		// showed a blank page - the exchange looked like it had never happened. The server keeps
		// it now; this is the reading end.
		//
		// Restored BEFORE the notifier is attached, so a message arriving mid-restore lands after
		// the history rather than in the middle of it.
		for (const ibMcpServer::Turn& turn : server->GetConversation())
			AppendTranscript(m_transcript, wxString::Format(
				turn.fromPerson ? wxT("## %s\n\n%s\n\n") : wxT("%s%s\n\n"),
				turn.fromPerson ? _("You") : wxString(), turn.text));

		m_notifier.reset(new ibAssistantNotifier(m_transcript, this));
		server->AddNotifier(m_notifier.get());

		// ⭐ THE STATE, WHICHEVER IT IS - and from the server, which is the only thing that knows.
		// An empty pane is unreadable: "nothing has happened yet", "nobody is connected" and
		// "this is broken" all look the same, and the one a person most needs to tell apart is
		// the last. Greeting() carries the not-running sentence too, so this is one call rather
		// than a branch that could learn to disagree with itself.
		AppendTranscript(m_transcript, server->Greeting() + wxT("\n\n"));
	}

	return ibView::OnCreate(doc, flags);
}

void ibAssistantView::BuildLayout(wxWindow* parent)
{
	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

	// A SPLITTER, because how much room the conversation deserves and how much
	// the question being typed does is the reader's business and changes by the
	// minute — a fixed input strip is right for nobody.
	wxSplitterWindow* splitter = new wxSplitterWindow(parent, wxID_ANY,
		wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3DSASH);
	splitter->SetMinimumPaneSize(80);

	// GROWTH GOES TO THE CONVERSATION. Gravity 1.0 means the TOP pane takes the
	// space when the window is resized, so enlarging the designer enlarges what
	// there is to read rather than the box you type one line into.
	splitter->SetSashGravity(1.0);

	// THE PLATFORM'S FONT, not this window's idea of one — the same settings the
	// code editor and the output window take, so the designer looks like one
	// program. Resolved FIRST, because the transcript and the input box below
	// must share it: two panes of one conversation in two typefaces reads as two
	// programs.
	//
	// ⚠ AND ASKED WHETHER IT IS A FONT AT ALL. The settings come from the
	// options file, and on a machine that has never saved one GetFont() answers
	// a default-constructed wxFont — not a small font, NOT A FONT. Scintilla
	// takes it apart without checking and the designer dies opening this tab
	// (crash of 2026-08-30, StyleSetFont at the top of the stack). An absent
	// setting is an ordinary state, not an error, so it gets an answer rather
	// than a guard that hides the window.
	wxFont font = mainFrame != nullptr
		? mainFrame->GetFontColorSettings().GetFont() : wxFont();

	if (!font.IsOk()) {
		// The same kind of face a transcript wants anyway: fixed pitch, so a
		// markdown table lines up.
		font = wxFont(10, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
	}

	m_transcript = new wxStyledTextCtrl(splitter, wxID_ANY);

	if (font.IsOk()) {
		m_transcript->StyleSetFont(wxSTC_STYLE_DEFAULT, font);
		m_transcript->StyleClearAll();
	}

	// The author chose the emphasis when they wrote the markdown; this only
	// renders it.
	//
	// ⚠ BY NAME, not by number. This wx carries Lexilla (src/stc/lexilla), where
	// lexers are created on request from a NAME — the old numeric SetLexer is
	// the legacy door and colours nothing, which is exactly how this arrived:
	// styles set, lexer silent, text flat.
	m_transcript->SetLexerLanguage(wxT("markdown"));

	// And colours, because weight alone is nearly invisible on a heading. Kept
	// to a handful of foreground colours over the inherited background, so the
	// window still follows whatever theme the shared settings carry.
	m_transcript->StyleSetForeground(wxSTC_MARKDOWN_HEADER1, wxColour(0, 64, 128));
	m_transcript->StyleSetForeground(wxSTC_MARKDOWN_HEADER2, wxColour(0, 64, 128));
	m_transcript->StyleSetForeground(wxSTC_MARKDOWN_HEADER3, wxColour(0, 64, 128));
	m_transcript->StyleSetBold(wxSTC_MARKDOWN_HEADER1, true);
	m_transcript->StyleSetBold(wxSTC_MARKDOWN_HEADER2, true);
	m_transcript->StyleSetBold(wxSTC_MARKDOWN_HEADER3, true);
	m_transcript->StyleSetBold(wxSTC_MARKDOWN_STRONG1, true);
	m_transcript->StyleSetBold(wxSTC_MARKDOWN_STRONG2, true);
	m_transcript->StyleSetItalic(wxSTC_MARKDOWN_EM1, true);
	m_transcript->StyleSetItalic(wxSTC_MARKDOWN_EM2, true);
	// A HEADING IS ALSO A SIZE. Colour alone at eight points is a difference a
	// reader has to look for; the point of a heading is to be found without
	// looking.
	m_transcript->StyleSetSize(wxSTC_MARKDOWN_HEADER1, font.IsOk() ? font.GetPointSize() + 3 : 13);
	m_transcript->StyleSetSize(wxSTC_MARKDOWN_HEADER2, font.IsOk() ? font.GetPointSize() + 2 : 12);

	m_transcript->StyleSetForeground(wxSTC_MARKDOWN_CODE, wxColour(128, 0, 0));
	m_transcript->StyleSetForeground(wxSTC_MARKDOWN_CODE2, wxColour(128, 0, 0));

	// BACKGROUNDS, not just letters. A fenced block should read as a block — and
	// a band of colour also answers the question "is the lexer alive at all"
	// without anyone having to squint at a hue.
	m_transcript->StyleSetBackground(wxSTC_MARKDOWN_CODEBK, wxColour(242, 242, 242));
	m_transcript->StyleSetForeground(wxSTC_MARKDOWN_CODEBK, wxColour(64, 64, 64));
	m_transcript->StyleSetBackground(wxSTC_MARKDOWN_BLOCKQUOTE, wxColour(240, 246, 240));
	m_transcript->StyleSetForeground(wxSTC_MARKDOWN_BLOCKQUOTE, wxColour(0, 96, 0));

	// A transcript is read, not edited. Margins off: there is no line to jump to
	// here, and a number column would only take width from the text.
	m_transcript->SetReadOnly(true);
	m_transcript->SetMarginWidth(0, 0);
	m_transcript->SetMarginWidth(1, 0);
	m_transcript->SetWrapMode(wxSTC_WRAP_WORD);

	// --- the lower pane: what a person would write ------------------------
	wxPanel* bottom = new wxPanel(splitter, wxID_ANY);
	wxBoxSizer* bottomSizer = new wxBoxSizer(wxVERTICAL);

	// 🛑 IT SAID WRITING FROM HERE DID NOT WORK, and by the time anyone read it that was false.
	// The sentence was written when nothing could collect a message; a connected assistant now
	// takes it and answers, which is what the rest of this window shows. A banner that describes
	// a state the program has left is worse than none — it is read as current.
	//
	// ⚠ AND IT WAS THREE SENTENCES, CLIPPED. The waiting strip below took the height it was
	// silently relying on, and a wrapped label in a shrinking pane loses its tail without saying
	// so. One line survives a resize; three explain themselves to nobody.
	wxStaticText* pending = new wxStaticText(bottom, wxID_ANY,
		_("What you write goes to the connected assistant. The platform calls no model itself."));
	bottomSizer->Add(pending, 0, wxALL, 4);

	// EMPTY UNTIL THERE IS SOMETHING TO WAIT FOR. A strip that always says something becomes
	// furniture and stops being read; this one appears when it has news and goes away again.
	m_waiting = new wxStaticText(bottom, wxID_ANY, wxEmptyString);
	bottomSizer->Add(m_waiting, 0, wxLEFT | wxRIGHT | wxBOTTOM, 4);

	wxBoxSizer* inputSizer = new wxBoxSizer(wxHORIZONTAL);

	// WRITABLE, even though nothing carries it away yet. A question can be
	// drafted now and sent when the other direction lands; a field locked
	// against typing loses the thought instead of keeping it. The BUTTON is the
	// half that is missing, and it is the half that stays disabled.
	m_input = new wxTextCtrl(bottom, wxID_ANY, wxEmptyString,
		wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE);
	if (font.IsOk())
		m_input->SetFont(font);

	// ⭐ ENTER SENDS — the same verb as the button, reached the way a person actually types. Shift
	// holds it back for a second line, which is the convention every chat field shares, so the
	// field stays multi-line without costing a reach for the mouse on every message.
	//
	// wxEVT_CHAR_HOOK rather than wxEVT_KEY_DOWN: a multi-line control consumes Enter itself, and
	// the hook is asked BEFORE it does. Skipped in every other case so typing is untouched.
	m_input->Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& event) {

		const int key = event.GetKeyCode();

		if ((key == WXK_RETURN || key == WXK_NUMPAD_ENTER)
			&& !event.ShiftDown() && !event.ControlDown() && !event.AltDown()) {

			// Asked of the BUTTON, not of the text: whatever disables sending — nothing typed, no
			// listener connected — disables it here too, and there is one rule rather than two
			// that drift apart.
			if (m_send != nullptr && m_send->IsEnabled()) {
				wxCommandEvent send(wxEVT_BUTTON, m_send->GetId());
				OnSend(send);
			}
			return;   // swallowed either way — Enter never leaves a stray newline behind
		}

		event.Skip();
	});

	inputSizer->Add(m_input, 1, wxEXPAND | wxALL, 4);

	m_send = new wxButton(bottom, wxID_ANY, _("Send"));
	m_send->SetToolTip(_("Leaves your message for the connected assistant to collect."));
	m_send->Bind(wxEVT_BUTTON, &ibAssistantView::OnSend, this);
	inputSizer->Add(m_send, 0, wxALL | wxALIGN_BOTTOM, 4);

	bottomSizer->Add(inputSizer, 1, wxEXPAND, 0);
	bottom->SetSizer(bottomSizer);

	splitter->SplitHorizontally(m_transcript, bottom);

	// ⚠ THE SASH IS PLACED AFTER THE WINDOW HAS A SIZE. Asked for here, the
	// position is measured against a height that is still nearly zero — a
	// negative offset then lands at the top and the conversation pane starts
	// collapsed, which is exactly how this arrived. CallAfter runs once the
	// layout has happened and the height is real.
	wxWeakRef<wxSplitterWindow> laterSplitter = splitter;
	splitter->CallAfter([laterSplitter]() {
		if (!laterSplitter)
			return;
		const int height = laterSplitter->GetClientSize().y;
		if (height > 240)
			laterSplitter->SetSashPosition(height - 160);
	});

	mainSizer->Add(splitter, 1, wxEXPAND | wxALL, 4);

	parent->SetSizer(mainSizer);
	parent->Layout();
}

void ibAssistantView::OnUpdate(ibView* WXUNUSED(sender), wxObject* WXUNUSED(hint))
{
}

void ibAssistantView::OnDraw(wxDC* WXUNUSED(dc))
{
}

void ibAssistantView::OnSend(wxCommandEvent& WXUNUSED(event))
{
	if (m_input == nullptr)
		return;

	const wxString text = m_input->GetValue();
	if (text.IsEmpty())
		return;

	ibMcpServer* server = ibApplicationData::GetMcpServer();
	if (server == nullptr)
		return;

	// ⭐ ASKED IF IT CAN BE ASKED, LEFT WHERE IT CAN BE FOUND IF NOT.
	//
	// When the connected client offered `sampling` at its handshake, the server puts the question
	// TO it and the answer comes back on its own — which is the whole difference between typing
	// here and being answered, and typing here and waiting until somebody goes and prods the
	// assistant from somewhere else.
	//
	// When it did not, the old road still works and is not a lesser one for this window: the
	// message is left in the queue, shown in the transcript at once, and collected with a tool
	// when the assistant next looks. The person sees the same thing either way; only the waiting
	// differs.
	wxString refusal;

	if (!server->CanAskModel() || !server->AskModel(text, refusal))
		server->Say(text);

	// Cleared only after it has been taken in, so a failure above leaves the
	// words where their author can still see them.
	m_input->Clear();
	m_input->SetFocus();

	// ⭐ AND THE WAITING BECOMES VISIBLE. Until now the window went perfectly quiet the moment
	// something was sent — the same picture as nothing happening — so a person had no way to tell
	// "it is being worked on" from "nobody is listening", and the second is the one they need.
	m_waitingDots = 0;

	if (!m_waitingTick.IsRunning())
		m_waitingTick.Start(500);

	RefreshWaiting();   // said at once rather than half a second later
}

void ibAssistantView::OnWaitingTick(wxTimerEvent& WXUNUSED(event))
{
	RefreshWaiting();
}

// ⭐ THE ANSWER IS WHAT ENDS THE WAIT, so this is called from where the answer arrives — not from
// a timeout. A strip that gave up after N seconds would tell a person the assistant had gone at
// exactly the moment it was thinking hardest.
void ibAssistantView::StopWaiting()
{
	m_waitingTick.Stop();

	if (m_waiting != nullptr) {
		m_waiting->SetLabel(wxEmptyString);
		m_waiting->GetParent()->Layout();
	}
}

void ibAssistantView::RefreshWaiting()
{
	if (m_waiting == nullptr)
		return;

	ibMcpServer* server = ibApplicationData::GetMcpServer();

	// ⚠ TWO SILENCES, TOLD APART BY ASKING. A message still sitting in the queue has been
	// collected by nobody — calling that "processing" would be the window inventing an assistant
	// that is not there. Once it HAS been taken, something really is working on it.
	const bool collected = server == nullptr || server->PendingCount() == 0;

	m_waitingDots = (m_waitingDots + 1) % 16;

	m_waiting->SetLabel((collected
		? _("Processing the request")
		: _("Waiting for an assistant to collect it")) + wxString(wxT('.'), m_waitingDots));

	m_waiting->GetParent()->Layout();
}

bool ibAssistantView::OnClose(bool deleteWindow)
{
	// BEFORE THE WIDGETS GO. Removal is by pointer identity, and a notifier left
	// registered would be written to after this view is gone.
	if (m_notifier) {
		if (ibMcpServer* server = ibApplicationData::GetMcpServer())
			server->RemoveNotifier(m_notifier.get());
		m_notifier.reset();
	}

	// ⚠ FOR THE SAME REASON, ONE LINE LATER. A running timer is a call already scheduled into a
	// window that is going away — the notifier's twin, and it would find a destroyed label.
	m_waitingTick.Stop();

	return ibView::OnClose(deleteWindow);
}
