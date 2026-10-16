#include "docViewAssistant.h"

#include "frmclient/mainFrame/mainFrame.h"   // the shared font/colour settings live on the frame

#include <wx/button.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/stattext.h>
#include <wx/stc/stc.h>
#include <wx/textctrl.h>
#include <wx/weakref.h>

namespace {

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

} // namespace

// ----------------------------------------------------------------------------
// ibAssistantDocument implementation
// ----------------------------------------------------------------------------

void ibAssistantDocument::DrawView(const ibProtocolNode& view, const ibProtocolNode& patch)
{
	if (ibAssistantView* const assistantView = dynamic_cast<ibAssistantView*>(GetFirstView()))
		assistantView->Draw(view, patch);
}

ibFrontendView* ibAssistantDocument::DoCreateView()
{
	return new ibAssistantView();
}

// ----------------------------------------------------------------------------
// ibAssistantView implementation
// ----------------------------------------------------------------------------

bool ibAssistantView::OnCreate(ibFrontendDocument* doc, long flags)
{
	BuildLayout(m_viewFrame);

	// The strip's heartbeat. Owned by the view so the tick arrives here, and stopped in OnClose —
	// a timer outliving its label writes into a destroyed control.
	m_waitingTick.SetOwner(this);
	Bind(wxEVT_TIMER, &ibAssistantView::OnWaitingTick, this);

	// What was already said and the server's greeting come with the first drawing — the server's view holds them.
	return ibFrontendView::OnCreate(doc, flags);
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

	// One line survives a resize; three explain themselves to nobody.
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
	// against typing loses the thought instead of keeping it.
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

			// Asked of the BUTTON, not of the text: whatever disables sending disables it here too, and there is one
			// rule rather than two that drift apart.
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

void ibAssistantView::OnDraw(wxDC* WXUNUSED(dc))
{
}

void ibAssistantView::Draw(const ibProtocolNode& view, const ibProtocolNode& patch)
{
	if (patch.IsNode() && !patch.Has(ibProtocolName::Rows) && !patch.Has(ibProtocolName::State))
		return;

	// The transcript only grows: the entries not shown yet go at its end. Fewer than shown — a transcript begun anew —
	// is drawn from its first.
	const std::vector<ibProtocolNode> entries = view.FindChild(ibProtocolName::Rows).Children();
	if (entries.size() < m_shownEntries && m_transcript != nullptr) {
		m_transcript->SetReadOnly(false);
		m_transcript->ClearAll();
		m_transcript->SetReadOnly(true);
		m_shownEntries = 0;
	}
	for (size_t i = m_shownEntries; i < entries.size(); ++i)
		AppendTranscript(m_transcript, entries[i].GetString(ibProtocolName::Text));
	m_shownEntries = entries.size();

	// ⭐ AND THE WAITING, AS THE SERVER SAYS IT — begun by what was sent, ended by the answer, never by a timeout.
	const ibProtocolNode state = view.FindChild(ibProtocolName::State);
	m_pendingCount = state.GetInt(ibProtocolName::Pending);
	if (state.GetBool(ibProtocolName::Waiting)) {
		if (!m_waitingTick.IsRunning()) {
			m_waitingDots = 0;
			m_waitingTick.Start(500);
		}
		RefreshWaiting();   // said at once rather than half a second later
	}
	else if (m_waitingTick.IsRunning()) {
		StopWaiting();
	}
}

void ibAssistantView::OnSend(wxCommandEvent& WXUNUSED(event))
{
	if (m_input == nullptr)
		return;

	const wxString text = m_input->GetValue();
	if (text.IsEmpty())
		return;

	const ibTabDocument* const document = dynamic_cast<const ibTabDocument*>(GetDocument());
	if (document == nullptr)
		return;

	// ⭐ ASKED IF IT CAN BE ASKED, LEFT WHERE IT CAN BE FOUND IF NOT — the server's (ibAssistantTranscriptView): the
	// person sees the same thing either way; only the waiting differs.
	ibProtocolNode args;
	args.SetValue(ibProtocolName::Text, text);
	document->Post(ibProtocolEvent::Change, args);

	// Cleared once it is handed over.
	m_input->Clear();
	m_input->SetFocus();
}

void ibAssistantView::OnWaitingTick(wxTimerEvent& WXUNUSED(event))
{
	RefreshWaiting();
}

// ⭐ THE ANSWER IS WHAT ENDS THE WAIT, so this is called when the server says it landed — not from
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

	// ⚠ TWO SILENCES, TOLD APART BY ASKING — the server. A message still sitting in the queue has been
	// collected by nobody — calling that "processing" would be the window inventing an assistant
	// that is not there. Once it HAS been taken, something really is working on it.
	const bool collected = m_pendingCount == 0;

	m_waitingDots = (m_waitingDots + 1) % 16;

	m_waiting->SetLabel((collected
		? _("Processing the request")
		: _("Waiting for an assistant to collect it")) + wxString(wxT('.'), m_waitingDots));

	m_waiting->GetParent()->Layout();
}

bool ibAssistantView::OnClose(bool deleteWindow)
{
	// ⚠ BEFORE THE WIDGETS GO. A running timer is a call already scheduled into a window that is going away, and it
	// would find a destroyed label.
	m_waitingTick.Stop();

	return ibFrontendView::OnClose(deleteWindow);
}
