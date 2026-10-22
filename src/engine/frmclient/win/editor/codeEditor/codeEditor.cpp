////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : autoComplete window 
////////////////////////////////////////////////////////////////////////////

#include "codeEditor.h"
#include "frmclient/mainFrame/mainFrame.h"
#include "frmclient/docView/tabDocument.h"   // the module's tab — what the text is fetched from and handed back to
#include "frmclient/mainFrame/objinspect/objinspect.h"   // the inspector — what it shows as it is made, and activated
#include "res/bitmaps_res.h"

#include <wx/artprov.h>

#define DEF_LINENUMBER_ID 0
#define DEF_BREAKPOINT_ID 1
#define DEF_FOLDING_ID 2

wxIMPLEMENT_CLASS(ibCodeEditor, wxStyledTextCtrl);

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

wxString ibCodeEditor::MakeProcedureTemplate(const wxString& name, const wxString& args) const
{
	const bool isCES = (m_tc.GetCodeStyle() == CODE_CES);
	if (isCES) {
		return wxT("Procedure ") + name + wxT("(") + args + wxT(")\r\n")
		       wxT("{\r\n")
		       wxT("\t\r\n")
		       wxT("}");
	}
	return wxT("Procedure ") + name + wxT("(") + args + wxT(")\r\n")
	       wxT("\t\r\n")
	       wxT("EndProcedure");
}

// ⚠ THE SYNTAX IS THE TAB'S, and it comes with the tab's view (SetCodeStyle): until then — and for a text with no tab — the
// stream reads the default a session's compile state starts with (ibCompileState: CES).
ibCodeEditor::ibCodeEditor()
	: wxStyledTextCtrl(), m_ac(this), m_ct(this), m_tc(CODE_CES)
{
}

ibCodeEditor::ibCodeEditor(ibTabDocument* document, wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style, const wxString& name)
	: wxStyledTextCtrl(parent, id, pos, size, style, name),
	  m_document(document), m_ac(this), m_ct(this), m_tc(CODE_CES)
{
	// initialize styles
	StyleClearAll();

	//set Lexer to LEX_CONTAINER: This will trigger the styleneeded event so you can do your own highlighting
	SetLexer(wxSTC_LEX_CONTAINER);

	//Set margin cursor
	for (int margin = 0; margin < GetMarginCount(); margin++)
		SetMarginCursor(margin, wxSTC_CURSORARROW);

	//register event
	Connect(wxEVT_STC_MARGINCLICK, wxStyledTextEventHandler(ibCodeEditor::OnMarginClick), nullptr, this);
	Connect(wxEVT_STC_STYLENEEDED, wxStyledTextEventHandler(ibCodeEditor::OnStyleNeeded), nullptr, this);
	Connect(wxEVT_STC_MODIFIED, wxStyledTextEventHandler(ibCodeEditor::OnTextChange), nullptr, this);

	Connect(wxEVT_KEY_DOWN, wxKeyEventHandler(ibCodeEditor::OnKeyDown), nullptr, this);
	Connect(wxEVT_STC_CHARADDED, wxStyledTextEventHandler(ibCodeEditor::OnCharAdded), nullptr, this);
	Connect(wxEVT_MOTION, wxMouseEventHandler(ibCodeEditor::OnMouseMove), nullptr, this);

	// On zoom step the line height jumps immediately while STC's per-page
	// width cache repaints column widths only on the next scroll. Re-fit
	// the margins (line-number / breakpoint / fold) against the now-
	// zoomed text metrics and force a single repaint — keeps vertical
	// and horizontal scaling visually in sync without re-running the
	// lexer over the whole document on every wheel tick.
	Bind(wxEVT_STC_ZOOM, [this](wxStyledTextEvent& evt) {
		if (GetMarginWidth(DEF_LINENUMBER_ID) > 0)
			SetMarginWidth(DEF_LINENUMBER_ID, TextWidth(wxSTC_STYLE_LINENUMBER, "_9999999"));

		const int symbolMargin = std::max(FromDIP(16), TextHeight(0));
		if (GetMarginWidth(DEF_BREAKPOINT_ID) > 0)
			SetMarginWidth(DEF_BREAKPOINT_ID, symbolMargin);
		if (GetMarginWidth(DEF_FOLDING_ID) > 0)
			SetMarginWidth(DEF_FOLDING_ID, symbolMargin);

		Refresh();
		Update();
		evt.Skip();
	});

	//set edge mode
	SetEdgeMode(wxSTC_EDGE_MULTILINE);

	// set visibility
	SetVisiblePolicy(wxSTC_VISIBLE_STRICT | wxSTC_VISIBLE_SLOP, 1);
	SetXCaretPolicy(wxSTC_CARET_EVEN | wxSTC_VISIBLE_STRICT | wxSTC_CARET_SLOP, 1);
	SetYCaretPolicy(wxSTC_CARET_EVEN | wxSTC_VISIBLE_STRICT | wxSTC_CARET_SLOP, 1);

	// Set the marker bitmaps.
	MarkerDefineBitmap(Breakpoint, wxMEMORY_BITMAP(Breakpoint_png));
	MarkerDefineBitmap(CurrentLine, wxMEMORY_BITMAP(Currentline_png));
	MarkerDefineBitmap(BreakLine, wxMEMORY_BITMAP(Breakline_png));

	// THE SAME DOT, ANOTHER COLOUR, for a breakpoint with a condition: the shape still reads as "a breakpoint",
	// the colour says "not every time". Turned from the red one rather than drawn anew, so the two never drift.
	// BLUE, not amber: amber read as a darker red beside the red dot, and the yellow run arrow drawn on top of
	// it disappeared (Max, 2026-09-11).
	wxImage conditionalBreakpoint = wxMEMORY_IMAGE(Breakpoint_png);
	conditionalBreakpoint.RotateHue(0.62);   // red -> blue
	MarkerDefineBitmap(ConditionalBreakpoint, wxBitmap(conditionalBreakpoint));

	//markers
	MarkerDefine(wxSTC_MARKNUM_FOLDER, wxSTC_MARK_BOXPLUS, *wxWHITE, *wxBLACK);
	MarkerDefine(wxSTC_MARKNUM_FOLDEROPEN, wxSTC_MARK_BOXMINUS, *wxWHITE, *wxBLACK);
	MarkerDefine(wxSTC_MARKNUM_FOLDERSUB, wxSTC_MARK_VLINE, *wxWHITE, *wxBLACK);
	MarkerDefine(wxSTC_MARKNUM_FOLDEREND, wxSTC_MARK_BOXPLUSCONNECTED, *wxWHITE, *wxBLACK);
	MarkerDefine(wxSTC_MARKNUM_FOLDEROPENMID, wxSTC_MARK_BOXMINUSCONNECTED, *wxWHITE, *wxBLACK);
	MarkerDefine(wxSTC_MARKNUM_FOLDERMIDTAIL, wxSTC_MARK_TCORNER, *wxWHITE, *wxBLACK);
	MarkerDefine(wxSTC_MARKNUM_FOLDERTAIL, wxSTC_MARK_LCORNER, *wxWHITE, *wxBLACK);

	// annotations
	AnnotationSetVisible(wxSTC_ANNOTATION_BOXED);

	// Set fold flags
	SetFoldFlags(wxSTC_FOLDFLAG_LINEBEFORE_CONTRACTED | wxSTC_FOLDFLAG_LINEAFTER_CONTRACTED);

	// Brace highlight: foreground only, no bold or background, so the
	// surrounding font metrics stay stable while the caret moves over
	// braces. Triggered from wxEVT_STC_UPDATEUI via our manual matcher
	// (wxSTC's BraceMatch needs the lexer to style brace characters
	// and our custom styler doesn't, so it returns -1).
	StyleSetForeground(wxSTC_STYLE_BRACELIGHT, *wxRED);
	StyleSetForeground(wxSTC_STYLE_BRACEBAD, *wxRED);

	Bind(wxEVT_STC_UPDATEUI, &ibCodeEditor::OnUpdateUI, this);

	// Custom context menu — replaces wxSTC's built-in popup with one
	// that adds the Syntax Helper Look-Up item alongside the standard
	// edit actions. The Look-Up item posts wxID_FRONTEND_SYNTAX_HELPER_LOOKUP
	// upward through the parent chain so the host frame
	// (mainFrameDesigner's OpenHelpForCursor binding) handles it without
	// the editor depending on the downstream designer header. See
	// subphase 1.3 — codeEditor knows nothing about the help corpus.
	UsePopUp(wxSTC_POPUP_NEVER);
	Bind(wxEVT_CONTEXT_MENU, &ibCodeEditor::OnContextMenu, this);
	Bind(wxEVT_MENU, [this](wxCommandEvent&) { Cut();        }, wxID_CUT);
	Bind(wxEVT_MENU, [this](wxCommandEvent&) { Copy();       }, wxID_COPY);
	Bind(wxEVT_MENU, [this](wxCommandEvent&) { Paste();      }, wxID_PASTE);
	Bind(wxEVT_MENU, [this](wxCommandEvent&) { SelectAll();  }, wxID_SELECTALL);
	Bind(wxEVT_MENU, [this](wxCommandEvent& ev) {
		// Walk parent chain firing wxEVT_MENU at every wxWindow until
		// one handles. wxStyledTextCtrl's PopupMenu does not always
		// propagate through wxAUI / wxAuiDocMDIFrame parents to the
		// outermost host where the host Bind() lives.
		wxCommandEvent up(wxEVT_MENU, wxID_FRONTEND_SYNTAX_HELPER_LOOKUP);
		up.SetEventObject(this);
		for (wxWindow* p = GetParent(); p != nullptr; p = p->GetParent()) {
			if (p->ProcessWindowEvent(up)) return;
		}
		if (wxTheApp) {
			if (wxWindow* top = wxTheApp->GetTopWindow())
				top->ProcessWindowEvent(up);
		}
	}, wxID_FRONTEND_SYNTAX_HELPER_LOOKUP);

	// Setup the dwell time before a tooltip is displayed.
	SetMouseDwellTime(200);

	// Setup caret line
	//SetCaretLineVisible(true);

	// miscellaneous
	SetLayoutCache(wxSTC_CACHE_PAGE);

	//Turn the fold markers red when the caret is a line in the group (optional)
	MarkerEnableHighlight(true);

	// The stream needs no module and no metadata: what the text MEANS is asked of the compiler, and
	// the module is passed at the moment of asking. So a sessionless host (codeRunner, which has no
	// document) gets the same stream as any other — folding, brace matching and the caret question
	// all work; only the answers that need a configuration are absent, and they are absent there
	// anyway.
	m_tc.SetLexemMode(ibLexemMode::Editing);

	// For document-less hosts there is no LoadModule() to flip the
	// "initial text loaded" gate — start initialised so OnTextChange
	// re-lexes right away.
	if (m_document == nullptr)
		m_initialized = true;

	// ⭐ WHAT IT SHOWS IN THE INSPECTOR AS IT IS ACTIVATED — the desktop's showed its document's metaobject, which the thin
	// one does not have: the property the inspector holds as it is made, shown or hidden (the row of the tree it was opened
	// from), held with the tree — the tree's next row makes the next one, this stays.
	ibObjectInspector* const inspector = objectInspector;
	if (m_document != nullptr && inspector != nullptr && inspector->GetSelectedObject() != nullptr)
		m_property = inspector->GetSelectedObject()->weak_from_this().lock();
}

// The desktop's cleared its module's lexems in the compile cache here — the cache is the server's.
ibCodeEditor::~ibCodeEditor()
{
}

bool ibCodeEditor::TryBefore(wxEvent& event)
{
	wxEventType type = event.GetEventType();
	if (type == wxEVT_PAINT ||
		type == wxEVT_NC_PAINT ||
		type == wxEVT_ERASE_BACKGROUND) {
		return wxStyledTextCtrl::TryBefore(event);
	}
	if (m_ct.Active()) {
		if (type == wxEVT_KEY_DOWN) {
			m_ct.CallEvent(event); return wxStyledTextCtrl::TryBefore(event);
		}
	}

	if (m_ac.CallEvent(event))
		return true;
	if (m_ct.CallEvent(event))
		return true;
	else return wxStyledTextCtrl::TryBefore(event);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ibCodeEditor::EditDebugPoint(int line_to_edit)
{
	// Forward to the (designer-side) debugger override; sessionless
	// hosts (codeRunner) get the default no-op.
	OnEditDebugPoint(line_to_edit);
}

void ibCodeEditor::RefreshBreakpoint(bool deleteCurrentBreakline)
{
	MarkerDeleteAll(ibCodeEditor::Breakpoint);
	MarkerDeleteAll(ibCodeEditor::ConditionalBreakpoint);
	RefreshBreakpointMarkers();
}

void ibCodeEditor::SetCurrentLine(int lineBreakpoint, bool setBreakLine)
{
	const int firstVisibleLine = GetFirstVisibleLine(),
		linesOnScreen = LinesOnScreen();

	//Incorrect position when editing button title
	//if (!ibCodeEditor::GetSTCFocus()) 
	// CodeEditor::SetSTCFocus(true);

	// THE RUN LINE MOVED, so every value that was asked about belongs to a state that no longer exists —
	// the question and its answer both go. This is the door a step comes through, and a step is exactly
	// what makes an answer stale.
	m_askedExpression.clear();
	m_askedValue.clear();

	MarkerDeleteAll(ibCodeEditor::BreakLine);

	if (setBreakLine) MarkerAdd(lineBreakpoint - 1, ibCodeEditor::BreakLine);

	if (lineBreakpoint > 0) {

		if (firstVisibleLine > (lineBreakpoint - 1))
			ScrollToLine(lineBreakpoint - 1);
		else if (firstVisibleLine + linesOnScreen < (lineBreakpoint - 1))
			ScrollToLine(lineBreakpoint - 1);
	}

	if (setBreakLine && lineBreakpoint > 0)
		m_lineBreakpoint = lineBreakpoint - 1;
	else
		m_lineBreakpoint = wxNOT_FOUND;

	//Set standart focus
	if (lineBreakpoint > 0) ibCodeEditor::SetFocus();

	//if (!setBreakLine) GotoLine(lineBreakpoint - 1);
}

void ibCodeEditor::SetEditorSettings(const ibEditorSettings& settings)
{
	m_indentationSize = settings.GetIndentSize();
	settings.Apply(*this);

	SetMarginType(DEF_LINENUMBER_ID, wxSTC_MARGIN_NUMBER);
	SetMarginWidth(DEF_LINENUMBER_ID, 0);

	if (settings.GetShowLineNumbers()) {
		// Figure out how wide the margin needs to be do display
		// the most m_number of linqes we'd reasonbly have.
		SetMarginWidth(DEF_LINENUMBER_ID, TextWidth(wxSTC_STYLE_LINENUMBER, "_9999999"));
	}

	// Symbol margins (breakpoint + fold) sized off the current line
	// height so they scale together with the editor zoom — using a
	// fixed FromDIP(16) baseline left the gutter too narrow when the
	// font was zoomed in, with icons floating in a tiny strip next to
	// large text. The wxEVT_STC_ZOOM handler refreshes the same widths
	// on every wheel tick.
	const int symbolMargin = std::max(FromDIP(16), TextHeight(0));

	SetMarginType(DEF_BREAKPOINT_ID, wxSTC_MARGIN_SYMBOL);
	SetMarginMask(DEF_BREAKPOINT_ID, ~(1024 | 256 | 512 | 128 | 64 | wxSTC_MASK_FOLDERS));

	SetMarginWidth(DEF_BREAKPOINT_ID, symbolMargin);
	SetMarginSensitive(DEF_BREAKPOINT_ID, true);

	// folding
	SetMarginType(DEF_FOLDING_ID, wxSTC_MARGIN_SYMBOL);
	SetMarginMask(DEF_FOLDING_ID, wxSTC_MASK_FOLDERS);

	SetMarginWidth(DEF_FOLDING_ID, symbolMargin);
	SetMarginSensitive(DEF_FOLDING_ID, true);

	// Three-step gradient across the gutter — leftmost (line numbers)
	// darkest, breakpoint medium, fold margin lightest — so the gutter
	// reads as a layered band that fades into the text area on the right.
	// LINENUMBER margin bg comes from wxSTC_STYLE_LINENUMBER style (set
	// in SetFontColorSettings); the symbol margins get explicit colors.
	SetMarginBackground(DEF_BREAKPOINT_ID, wxColour(0xF0, 0xF0, 0xF0));
	SetMarginBackground(DEF_FOLDING_ID,    wxColour(0xF6, 0xF6, 0xF6));
	SetFoldMarginColour(true,   wxColour(0xF6, 0xF6, 0xF6));
	SetFoldMarginHiColour(true, wxColour(0xF6, 0xF6, 0xF6));

	m_enableAutoComplete = settings.GetEnableAutoComplete();
}

void ibCodeEditor::SetFontColorSettings(const ibFontColorSettings& settings)
{
	settings.Apply(*this);

	// Soften the gutter — muted gray digits on the darkest gradient step
	// (line-number margin is leftmost; breakpoint + fold margins step
	// progressively lighter, see the SetMarginBackground calls in
	// SetEditorSettings).
	StyleSetForeground(wxSTC_STYLE_LINENUMBER, wxColour(0xA0, 0xA0, 0xA0));
	StyleSetBackground(wxSTC_STYLE_LINENUMBER, wxColour(0xE8, 0xE8, 0xE8));

	// Matching braces red; their font is the chosen one already, StyleClearAll copied it in.
	StyleSetForeground(wxSTC_STYLE_BRACELIGHT, *wxRED);
	StyleSetForeground(wxSTC_STYLE_BRACEBAD, *wxRED);
}

bool ibCodeEditor::LoadModule()
{
	if (m_document == nullptr) {
		ClearAll();
		return false;
	}

	// The text the tab's view holds (ibProtocol::ibView::ibModule::ibRequest::Text): what the desktop's read of the metaobject.
	ibProtocolNode answer;
	if (!m_document->Fetch(ibProtocolNode(), answer)) {
		ClearAll();
		m_fp.RecalcFoldLevel();
		RefreshEditor();
		return false;
	}
	return LoadModule(answer);
}

bool ibCodeEditor::LoadModule(const ibProtocolNode& answer)
{
	ClearAll();

	m_moduleText = answer.GetString(ibProtocol::ibView::ibModule::Text);

	if (IsEditable()) {
		SetText(m_moduleText); m_initialized = true;
	}
	else {
		SetReadOnly(false);
		SetText(m_moduleText); m_initialized = true;
		SetReadOnly(true);
	}

	m_tc.Load(m_moduleText);

	try {
		m_tc.Translate();
	}
	catch (...) {
	}

	EmptyUndoBuffer();

	m_fp.RecalcFoldLevel();
	RefreshEditor();
	return true;
}

bool ibCodeEditor::SaveModule()
{
	if (m_document != nullptr) {

		// Where the desktop's wrote the metaobject's text — the tab's, as a field hands its text back (Change {Text}); a
		// text the server already holds is not sent again. HELD, not a call of its own: what reads the text on the
		// server — Save, the syntax check, a close, the debugger — is a call, and carries it (Pending) ahead of its work;
		// the focus leaving the editor asks the server nothing.
		const wxString text = GetText();
		if (text != m_moduleText) {
			ibProtocolNode args;
			args.SetValue(ibProtocol::ibView::ibModule::Text, text);
			m_document->Hold(ibProtocol::ibEvent::ibKind::Change, args);
			m_moduleText = text;
		}
		return true;
	}

	return m_document != nullptr;
}

void ibCodeEditor::SetCodeStyle(short codeStyle)
{
	if (m_tc.GetCodeStyle() == codeStyle)
		return;

	// The text read again in the syntax it is written in: the stream the folds and the caret question read, and the
	// colouring from the top (OnStyleNeeded restyles from where the styled part ends).
	m_tc.SetCodeStyle(codeStyle);
	m_tc.Load(GetText());
	try {
		m_tc.Translate();
	}
	catch (...) {
	}

	m_fp.RecalcFoldLevel();
	StartStyling(0);   // nothing styled any more: the container is asked to style it all again
	Colourise(0, -1);
}

int ibCodeEditor::GetRealPosition()
{
	const wxString& codeText = GetTextRange(0, GetCurrentPos());
	return codeText.Length();
}

int ibCodeEditor::GetRealPositionFromPoint(const wxPoint& pt)
{
	const wxString& codeText = GetTextRange(0, PositionFromPoint(pt));
	return codeText.Length();
}

void ibCodeEditor::SetDebugValue(const wxString& value)
{
	// KEPT AS WELL AS SHOWN. The system decides on its own when to pop a tooltip and shows the text that
	// is in place at that moment, so the same answer is laid down again on every movement across the word
	// it belongs to (LoadToolTip) — and that is only possible if the editor still has it.
	m_askedValue = value;

	if (value.IsEmpty())
		UnsetToolTip();
	else
		SetToolTip(value);
}

#include "frmclient/win/dlgs/lineInput/lineInput.h"
#include "frmclient/win/dlgs/functionSearcher/functionSearcher.h"

void ibCodeEditor::RefreshEditor()
{
	ibCodeEditor::SetEditorSettings(mainFrame->GetEditorSettings());
	ibCodeEditor::SetFontColorSettings(mainFrame->GetFontColorSettings());

	ibCodeEditor::RefreshBreakpoint();
}

// The inspector is sent the property held, as the desktop's was its document's metaobject here (an object's or a
// manager's module: its owner) — and receives it anew.
void ibCodeEditor::ActivateEditor()
{
	if (m_document != nullptr && objectInspector != nullptr)
		objectInspector->SelectObject(m_property.get());

	ibCodeEditor::SetSTCFocus(true);
	ibCodeEditor::SetFocus();
}

#include <wx/fdrepdlg.h>

void ibCodeEditor::FindText(const wxString& findString, int wxflags)
{
	int sciflags = 0;
	if ((wxflags & wxFR_WHOLEWORD) != 0) {
		sciflags |= wxSTC_FIND_WHOLEWORD;
	}
	if ((wxflags & wxFR_MATCHCASE) != 0) {
		sciflags |= wxSTC_FIND_MATCHCASE;
	}
	int result = 0;
	if ((wxflags & wxFR_DOWN) != 0) {
		ibCodeEditor::SetSelectionStart(GetSelectionEnd());
		ibCodeEditor::SearchAnchor();
		result = ibCodeEditor::SearchNext(sciflags, findString);
	}
	else {
		ibCodeEditor::SetSelectionEnd(GetSelectionStart());
		ibCodeEditor::SearchAnchor();
		result = ibCodeEditor::SearchPrev(sciflags, findString);
	}
	if (wxSTC_INVALID_POSITION == result) {
		wxMessageBox(wxString::Format(_("\"%s\" not found!"), findString.c_str()),
			_("Not Found!"), wxICON_ERROR, (wxWindow*)this);
	}
	else {
		ibCodeEditor::EnsureCaretVisible();
		ibCodeEditor::SetSTCFocus(true);
	}
}

void ibCodeEditor::ShowGotoLine()
{
	ibDialogLineInput dlg(this);
	const int ret = dlg.ShowModal();

	if (ret != wxNOT_FOUND) {
		ibCodeEditor::SetFocus();
		ibCodeEditor::GotoLine(ret - 1);
	}
}

void ibCodeEditor::ShowMethods()
{
	ibFunctionList dlg(m_document, this);
	dlg.ShowModal();
}

void ibCodeEditor::SyntaxControl()
{
	if (m_document == nullptr)
		return;

	// The check compiles the module the server holds (ibModuleTextDocument::SyntaxControl) — so the text typed here goes
	// first; both are posted, in that order.
	SaveModule();

	ibProtocolNode args;
	args.SetValue(ibProtocol::ibView::ibModule::Id, static_cast<int>(ibProtocol::ibView::ibModule::ibCommand::SyntaxControl));
	m_document->Post(ibProtocol::ibEvent::ibKind::Command, args);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef UTF8_LEXEM_TRANSLATE
#define appendStyle(style) \
ibCodeEditor::StartStyling(currPos); \
ibCodeEditor::SetStyling(fromPos + m_tc.GetCurrentUtf8Pos() - currPos, style);
#else
#define appendStyle(style) \
ibCodeEditor::StartStyling(currPos); \
ibCodeEditor::SetStyling(fromPos + m_tc.GetCurrentPos() - currPos, style);
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                                          Styling                                                                       //
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ibCodeEditor::HighlightSyntaxAndCalculateFoldLevel(const int fromPos, const int toPos)
{
	m_tc.Load(ibCodeEditor::GetTextRange(fromPos, toPos));

	//remove old styling
	ibCodeEditor::StartStyling(fromPos); //from here
	ibCodeEditor::SetStyling(toPos - fromPos, wxSTC_C_COMMENT); //with that length and style -> cleared

	wxString word;
	unsigned int currPos = fromPos;
	bool prevWasDot = false;   // last significant token was a member-access '.' (kept across whitespace)

	while (!m_tc.IsEnd()) {
#ifdef UTF8_LEXEM_TRANSLATE
		currPos = fromPos + m_tc.GetCurrentUtf8Pos();
#else 
		currPos = fromPos + m_tc.GetCurrentPos();
#endif 
		if (m_tc.IsWord()) {
			(void)m_tc.GetWord(word, false, true);
			const short keyWord = m_tc.IsKeyWord(word);
			// A keyword right after a member-access `.` is a MEMBER NAME, not a keyword
			// (`q.Execute().Select()` — `Select` is the method): style it as a plain identifier.
			if (keyWord != wxNOT_FOUND && !prevWasDot) {
				if (word.Left(1) == '#') {
					appendStyle(wxSTC_C_PREPROCESSOR);
				}
				else {
					appendStyle(wxSTC_C_STRING);
				}
			}
			else {
				appendStyle(wxSTC_C_WORD);
			}
			prevWasDot = false;
		}
		else if (m_tc.IsNumber() || m_tc.IsString() || m_tc.IsDate()) {
			if (m_tc.IsNumber()) {
				(void)m_tc.GetNumber();
				appendStyle(wxSTC_C_NUMBER);
			}
			else if (m_tc.IsString()) {
				(void)m_tc.GetString();
				appendStyle(wxSTC_C_OPERATOR);
			}
			else if (m_tc.IsDate()) {
				(void)m_tc.GetDate();
				appendStyle(wxSTC_C_OPERATOR);
			}
			prevWasDot = false;
		}
		else {
			wxUniChar b;
			(void)m_tc.GetByte(b);
			appendStyle(wxSTC_C_IDENTIFIER);
			// keep "after dot" across intervening whitespace, so `obj . Select` is handled too
			if (b == '.')                                             prevWasDot = true;
			else if (b != ' ' && b != '\t' && b != '\r' && b != '\n') prevWasDot = false;
		}
	}

	UpdateFoldLevel();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                                          EVENT                                                                         //
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ibCodeEditor::OnStyleNeeded(wxStyledTextEvent& event)
{
	/*this is called every time the styler detects a line that needs style, so we style that range.
	This will save a lot of performance since we only style text when needed instead of parsing the whole file every time.*/
	int line_start = ibCodeEditor::LineFromPosition(ibCodeEditor::GetEndStyled());
	int line_end = ibCodeEditor::GetFirstVisibleLine() + ibCodeEditor::LinesOnScreen();

	if (line_end > ibCodeEditor::GetLineCount()) {
		line_end = ibCodeEditor::GetLineCount() - 1;
	}

	/*fold level: May need to include the two lines in front because of the fold level these lines have- the line above
	may be affected*/
	if (line_start > 1) {
		line_start -= 2;
	}
	else {
		line_start = 0;
	}

	//if it is so small that all lines are visible, style the whole document
	if (ibCodeEditor::GetLineCount() == ibCodeEditor::LinesOnScreen()) {
		line_start = 0;
		line_end = ibCodeEditor::GetLineCount() - 1;
	}

	if (line_end < line_start) {
		//that happens when you select parts that are in front of the styled area
		wxSwap(line_end, line_start);
	}

	//style the line following the style area too (if present) in case fold level decreases in that one
	if (line_end < ibCodeEditor::GetLineCount() - 1) {
		line_end++;
	}

	//get exact start positions
	HighlightSyntaxAndCalculateFoldLevel(
		ibCodeEditor::PositionFromLine(line_start),
		ibCodeEditor::GetLineEndPosition(line_end)
	);

	event.Skip();
}

void ibCodeEditor::OnMarginClick(wxStyledTextEvent& event)
{
	const int line_from_pos = LineFromPosition(event.GetPosition());

	switch (event.GetMargin())
	{
	case DEF_BREAKPOINT_ID:
		if (IsEditable())
			OnEditDebugPoint(line_from_pos);
		break;
	case DEF_FOLDING_ID:
		ToggleFold(line_from_pos);
		break;
	}

	event.Skip();
}

void ibCodeEditor::OnTextChange(wxStyledTextEvent& event)
{
	const int modFlags = event.GetModificationType();

	if ((modFlags & (wxSTC_MOD_INSERTTEXT)) == 0 &&
		(modFlags & (wxSTC_MOD_DELETETEXT)) == 0)
		return;

	if (!m_initialized)
		return;

	const wxString& codeText = GetText();
	const int line = LineFromPosition(event.GetPosition());

	// Re-lex — fires for every host (codeRunner included). The stream feeds folding, brace
	// matching and the caret question; nothing here compiles anything.
	m_tc.Load(codeText);

	if (event.m_linesAdded != 0) {

		// Was the edit at the very start (column 0) of `line`? Then the whole
		// line's content moved by linesAdded, so a marker sitting ON `line`
		// shifts too; an edit later in the line leaves `line` in place. Without
		// this the breakpoint/exec-line wouldn't follow an Enter pressed at the
		// line start (the original bug: BP on 10 + Enter → stays on 10).
		const bool atLineStart = (event.GetPosition() == PositionFromLine(line));

		OnPatchModule(line, event.m_linesAdded, atLineStart);

		if (m_lineBreakpoint != wxNOT_FOUND) {
			MarkerDeleteAll(ibCodeEditor::BreakLine);
			if (line < m_lineBreakpoint || (atLineStart && line == m_lineBreakpoint))
				m_lineBreakpoint += event.m_linesAdded;
			MarkerAdd(m_lineBreakpoint, ibCodeEditor::BreakLine);
		}

		RefreshBreakpoint();
	}

	try {
#if _USE_OLD_TEXT_PARSER_IN_CODE_EDITOR == 0
		// First-time-empty buffer (codeRunner initial SetText, or any host that hasn't called
		// LoadModule) — a patch has no baseline to work from, so bootstrap with a full pass and
		// let subsequent edits patch it.
		if (!m_tc.HasLexem()) {
			m_tc.Translate();
		}
		else if ((modFlags & (wxSTC_MOD_INSERTTEXT | wxSTC_MOD_DELETETEXT)) != 0) {

			const bool deleted = (modFlags & wxSTC_MOD_DELETETEXT) != 0;
			const int  sign = deleted ? -1 : 1;

			ibTranslateCode::ibTextEdit edit;
			edit.m_line = line;
			edit.m_lineOffset = event.m_linesAdded;
			edit.m_posOffset = sign * (int)event.GetString().Length();
#ifdef UTF8_LEXEM_TRANSLATE
			edit.m_posOffsetUtf8 = sign * event.GetLength();
#endif
			m_tc.Translate(edit);
		}
#else
		m_tc.Translate();
#endif
	}
	catch (...)
	{
	}

	// Document-side update — only when a backing document is present. A text of the window's own (a composer's
	// expression) lives in the editor only. The desktop's wrote the metaobject's text and marked the compile cache's
	// module changed on every key; here the tab is told it is modified (its document's Modify), and the text goes back
	// as the focus leaves, or before a save, a close, a check (SaveModule) — the server's module is then the text.
	if (m_document != nullptr)
		m_document->Modify(true);

	m_fp.RecalcFoldLevel();
}

void ibCodeEditor::OnKeyDown(wxKeyEvent& event)
{
	if (!IsEditable()) {
		event.Skip(); return;
	}

	switch (event.GetKeyCode())
	{
	case WXK_LEFT:
		SetEmptySelection(GetCurrentPos() - 1);
		break;
	case WXK_RIGHT:
		SetEmptySelection(GetCurrentPos() + 1);
		break;
	case WXK_UP: {
		if (!event.ShiftDown()) {
			int currentPos = GetCurrentPos();
			int line = LineFromPosition(currentPos);

			int startPos = PositionFromLine(line);

			int length = currentPos - startPos;

			int startNewPos = PositionFromLine(line - 1);
			int endNewPos = GetLineEndPosition(line - 1);

			if (endNewPos - startNewPos < length)
				InsertText(endNewPos, wxString(wxT(' '), length - (endNewPos - startNewPos)));
			SetEmptySelection(startNewPos + length);
		}
		else {
			event.Skip();
		}
		break;
	}
	case WXK_DOWN:
	{
		if (!event.ShiftDown()) {
			int currentPos = GetCurrentPos();
			int line = LineFromPosition(currentPos);

			int startPos = PositionFromLine(line);

			int length = currentPos - startPos;

			int startNewPos = PositionFromLine(line + 1);
			int endNewPos = GetLineEndPosition(line + 1);

			if (endNewPos - startNewPos < length)
				InsertText(endNewPos, wxString(wxT(' '), length - (endNewPos - startNewPos)));
			SetEmptySelection(startNewPos + length);
		}
		else {
			event.Skip();
		}
		break;
	}
	case WXK_NUMPAD_ENTER:
	case WXK_RETURN: PrepareTABs(); break;
	case ' ': if (m_enableAutoComplete && event.ControlDown()) LoadAutoComplete(); event.Skip(); break;
	case '9': if (m_enableAutoComplete && event.ShiftDown()) LoadCallTip(); event.Skip(); break;
	case '0': if (m_enableAutoComplete && event.ShiftDown()) m_ct.Cancel(); event.Skip(); break;

	case WXK_F8:
		if (IsEditable())
			OnEditDebugPoint(LineFromPosition(GetCurrentPos()));
		break;
	default: event.Skip(); break;
	}
}

wxString ibCodeEditor::GetIdentifierUnderCursor()
{
	// Explicit selection wins — user may have selected a multi-word
	// expression that the autocomplete word-boundary heuristic cannot
	// see. Callers that want strict identifier-only semantics should
	// validate the returned string themselves.
	const wxString sel = GetSelectedText();
	if (!sel.IsEmpty()) return sel;

	const int pos   = GetCurrentPos();
	const int start = WordStartPosition(pos, true);
	const int end   = WordEndPosition  (pos, true);
	if (end <= start) return wxEmptyString;
	return GetTextRange(start, end);
}

// ---------------------------------------------------------------------------
//  The string literal under the caret — the door a query in a module is reached through
// ---------------------------------------------------------------------------

ibCodeEditor::StringLiteralSpan ibCodeEditor::GetStringLiteralUnderCursor()
{
	StringLiteralSpan span;

	// ⭐⭐ THE LEXER SAYS WHERE A LITERAL IS AND WHAT IT HOLDS — not a quote count of our own.
	//
	// The editor already keeps the module's token stream (m_tc) for the colouring and the completion,
	// and a literal is one of its tokens. The scan this replaced counted quotes from the top of the
	// document by itself, and went wrong in two ways:
	//  - it compared the caret, a wxSTC position (a BYTE offset into UTF-8), with an index into
	//    GetText() (a CHARACTER count). Every non-ASCII character above the query - a Russian message
	//    in a Tstr() - put the caret further on than the scan believed: a click near the end of a
	//    query opened the constructor on the NEXT literal ("MonthEnd" - not a query), and OK wrote
	//    the result that many bytes too early, over the code in front of the literal;
	//  - it did not know comments, so one quote in a `//` line turned every literal below it inside out.
	// The stream's m_numUtf8String IS a wxSTC position, and a comment never becomes a token.
	//
	// ⚠ A STREAM OF ITS OWN, NOT THE EDITOR'S KEPT ONE. m_tc is patched edit by edit (OnTextChange), and
	// after the text is replaced WHOLE - a SetText on a live editor, a delete-all then an insert-all - the
	// patched stream is not the text any more: CI measured it (CodeEditorFix, four tests on 0dfc2f81), the
	// first literal of a fresh editor found and every literal after a second SetText lost. The question is
	// asked on a click, not a keystroke, so the text is simply read again by the same lexer in the same
	// mode; the patching is a question of its own, for the colouring and the folds that read it.
	const int caret = GetCurrentPos();
	ibTranslateCode stream(m_tc.GetCodeStyle());
	stream.SetLexemMode(ibLexemMode::Editing);
	stream.Load(GetText());
	try {
		stream.Translate();
	}
	catch (...) {
		return span;
	}
	// The last token that starts at or before the caret. A token's recorded position is where the token
	// itself starts - the lexer skips the whitespace and comments in front first - so a caret just past a
	// closing quote, or in the gap after it, still falls to the literal until the next token begins.
	size_t at = stream.GetLexCount();
	for (size_t i = 0; i < stream.GetLexCount(); ++i) {
		if (stream.GetLexAt(i).m_lexType == ENDPROGRAM || static_cast<int>(stream.GetLexAt(i).m_numUtf8String) > caret)
			break;
		at = i;
	}
	if (at == stream.GetLexCount())
		return span;

	// WHERE IT OPENS, WHERE IT CLOSES AND WHAT IT SAYS, read by the lexer from the token's start - so
	// the value is the one the running module would hand a Query: the same `""`, `|` and line breaks.
	const auto readLiteral = [this, caret, &span](const ibLexem& lex) -> bool {
		if (lex.m_lexType != CONSTANT || lex.m_valData.GetType() != ibValueTypes::TYPE_STRING)
			return false;
		const int from = static_cast<int>(lex.m_numUtf8String);
		ibTranslateCode reader(m_tc.GetCodeStyle());
		reader.Load(GetTextRange(from, GetLength()));
		try {
			if (!reader.IsString())
				return false;
			const int open = from + static_cast<int>(reader.GetCurrentUtf8Pos());
			if (GetCharAt(open) != '"')
				return false;   // a stray `|` the lexer also reads as a string - not a literal to open
			wxString value;
			if (!reader.GetString(value))
				return false;
			const int close = from + static_cast<int>(reader.GetCurrentUtf8Pos());
			// Inside from the opening quote to just past the closing one, so a click at either edge
			// finds the string a person is plainly pointing at.
			if (caret < open || caret > close)
				return false;
			span.m_start = open;
			span.m_end   = close;
			span.m_text  = value;
			return true;
		}
		catch (...) {
			return false;   // a literal still being typed has no closing quote yet
		}
	};

	// A caret just past a closing quote already stands in the NEXT token's stretch.
	if (!readLiteral(stream.GetLexAt(at)) && at > 0)
		readLiteral(stream.GetLexAt(at - 1));
	return span;
}

// The indent a literal's continuation lines take: the text in front of `position` on its line, a tab
// kept a tab and anything else a space - counted in CHARACTERS, since a wxSTC position counts bytes
// and a Cyrillic name in front of the quote is one column, not two.
static wxString IndentBefore(wxStyledTextCtrl& editor, int position)
{
	const wxString lead = editor.GetTextRange(editor.PositionFromLine(editor.LineFromPosition(position)), position);
	wxString indent;
	for (const wxUniChar c : lead)
		indent += (c == wxT('\t')) ? wxT('\t') : wxT(' ');
	return indent;
}

wxString ibCodeEditor::SpellStringLiteral(const wxString& text, const wxString& indent)
{
	// Quoted, inner quotes doubled, and every line after the first opened with `|` under the
	// opening quote — the script's own spelling of a multi-line string, and what makes a query
	// written into a module readable rather than one endless line.
	wxString spelled = wxT("\"");
	for (size_t i = 0; i < text.length(); ++i) {
		if (text[i] == wxT('"'))  { spelled += wxT("\"\""); continue; }
		if (text[i] == wxT('\r')) { continue; }
		if (text[i] == wxT('\n')) { spelled += wxT("\n") + indent + wxT("|"); continue; }
		spelled += text[i];
	}
	return spelled + wxT("\"");
}

void ibCodeEditor::ReplaceStringLiteral(const StringLiteralSpan& span, const wxString& text)
{
	if (!span.Found())
		return;

	// The indent of the opening quote — continuation lines line up under it.
	SetTargetStart(span.m_start);
	SetTargetEnd(span.m_end);
	ReplaceTarget(SpellStringLiteral(text, IndentBefore(*this, span.m_start)));
}

void ibCodeEditor::InsertStringLiteral(int position, const wxString& text)
{
	if (position < 0)
		position = GetCurrentPos();

	// Indented to WHERE THE CARET IS, so a query written into the middle of a procedure lines up
	// with the code around it instead of starting at column zero.
	InsertText(position, SpellStringLiteral(text, IndentBefore(*this, position)));
}

void ibCodeEditor::OnMouseMove(wxMouseEvent& event)
{
	// OVER THE BREAKPOINT MARGIN the hint is the margin's: what the breakpoint on that line stops on. The
	// margins stand side by side in their order, so this one starts where the line numbers end.
	const wxPoint at = event.GetPosition();
	const int marginLeft = GetMarginWidth(DEF_LINENUMBER_ID);
	if (at.x >= marginLeft && at.x < marginLeft + GetMarginWidth(DEF_BREAKPOINT_ID)) {
		wxString hint;
		if (!GetDebugPointHint(LineFromPosition(PositionFromPoint(at)), hint))
			hint.clear();
		if (hint != m_marginHint) {
			if (hint.IsEmpty()) UnsetToolTip();
			else                SetToolTip(hint);
			m_marginHint = hint;
		}
	}
	else {
		// Off the margin its hint comes down, before the text's own (a debugger's value) goes up.
		if (!m_marginHint.IsEmpty()) {
			UnsetToolTip();
			m_marginHint.clear();
		}
		LoadToolTip(at);
	}
	event.Skip();
}

#include "frmclient/win/dlgs/queryConstructor/queryConstructor.h"   // the constructor, opened on the literal
#include "frmclient/win/dlgs/translateConstructor/translateConstructor.h"   // …and its sibling for a translated text
#include "frmclient/win/dlgs/formatConstructor/formatConstructor.h"         // …and for a format string
#include "frmclient/win/dlgs/linqConstructor/linqConstructor.h"             // …and a LINQ block, at the caret
#include "frmclient/artProvider/artProvider.h"                      // wxART_QUERY_CONSTRUCTOR — the icon, registered not embedded

void ibCodeEditor::OnContextMenu(wxContextMenuEvent& event)
{
	wxMenu menu;

	// The line the menu is about: the one under the mouse, or the caret's when it came from the keyboard.
	const wxPoint clickAt = event.GetPosition();
	const int menuLine = clickAt == wxDefaultPosition
		? LineFromPosition(GetCurrentPos())
		: LineFromPosition(PositionFromPoint(ScreenToClient(clickAt)));

	// Syntax helper lookup goes first — primary action for an
	// identifier-aware editor. Disabled when the cursor isn't over
	// an identifier (whitespace, between tokens).
	const wxString identifier = GetIdentifierUnderCursor();
	auto* miLookup = menu.Append(wxID_FRONTEND_SYNTAX_HELPER_LOOKUP,
	                             _("Look up in Syntax Helper") + wxT("\tRawCtrl+F1"));
	miLookup->SetBitmap(wxArtProvider::GetBitmap(wxART_HELP_BOOK, wxART_MENU));
	miLookup->Enable(!identifier.IsEmpty());

	// ITS SIBLING: the query constructor, gated the same way and by the same kind of question —
	// "is the caret standing on something I can work with". For the lookup that is an identifier;
	// for the constructor it is a string literal, because a query in a module lives in one.
	const StringLiteralSpan literal = GetStringLiteralUnderCursor();
	const int caret = GetCurrentPos();
	// FENCED ON BOTH SIDES. It is neither a help lookup nor a clipboard verb — it opens a whole
	// window over the caret, and a menu reads by its groups.
	menu.AppendSeparator();
	wxMenuItem* miConstruct = menu.Append(wxID_ANY, _("Query constructor"));
	miConstruct->SetBitmap(wxArtProvider::GetBitmap(wxART_QUERY_CONSTRUCTOR, wxART_FRONTEND,
		FromDIP(wxSize(16, 16))));
	// ALWAYS AVAILABLE. Gating it on "the caret is inside a string" made the constructor a tool for
	// EDITING a query somebody had already typed, which is the wrong way round — the first query is
	// the one you most want help writing. Standing on a literal it opens on that query and writes
	// back into it; standing anywhere else it opens empty and INSERTS the result at the caret.
	// (A module that cannot be changed still opens it, read-only: a query one may not edit is still
	// one worth reading, and refusing would hide the only view of it there is.)
	//
	// THE CONFIGURATION IS THE SERVER'S: the constructor asks it of this editor's tab (ibView::ibQueryConstructor). An
	// editor on no tab (a window's own) has nobody to ask — the desktop's "no configuration open".
	menu.Bind(wxEVT_MENU, [this, literal, caret](wxCommandEvent&) {
		wxString text = literal.m_text;
		const ibMetaData metaData = m_document != nullptr ? m_document->GetMetaData() : ibMetaData();
		if (!ibShowQueryConstructor(this, text, m_document != nullptr ? &metaData : nullptr, !IsEditable()))
			return;
		if (literal.Found()) ReplaceStringLiteral(literal, text);
		else                 InsertStringLiteral(caret, text);
	}, miConstruct->GetId());

	// AND THE TRANSLATION CONSTRUCTOR, on the same literal and by the same rule: a module's messages
	// are texts a person reads, written once per language like any caption — and the window is the
	// caption's own (ibDialogTranslateConstructor), over the languages of the configuration THIS module belongs to —
	// of its document, the tab it is in (none, a query's text: the session's).
	wxMenuItem* miTranslate = menu.Append(wxID_ANY, _("Translation constructor"));
	miTranslate->SetBitmap(wxArtProvider::GetBitmap(wxART_TRANSLATION_CONSTRUCTOR, wxART_FRONTEND, FromDIP(wxSize(16, 16))));
	menu.Bind(wxEVT_MENU, [this, literal, caret](wxCommandEvent&) {
		const ibTranslateString before(literal.m_text);
		const ibMetaData metaData = m_document != nullptr ? m_document->GetMetaData() : ibMetaData();
		ibDialogTranslateConstructor dialog(this, _("Translation constructor"), before, &metaData, !IsEditable());
		if (dialog.ShowModal() != wxID_OK)
			return;
		const ibTranslateString after = dialog.GetTranslate();
		if (after == before)
			return;   // nothing was changed: the literal keeps the spelling its author gave it
		if (literal.Found()) ReplaceStringLiteral(literal, after.GetRawText());
		else                 InsertStringLiteral(caret, after.GetRawText());
	}, miTranslate->GetId());

	// …AND THE THIRD: the string Format(value, format) reads. Same literal, same rule — and a string that
	// comes back unchanged is not written, so it keeps its author's spelling.
	wxMenuItem* miFormat = menu.Append(wxID_ANY, _("Format string constructor"));
	miFormat->SetBitmap(wxArtProvider::GetBitmap(wxART_FORMAT_CONSTRUCTOR, wxART_FRONTEND, FromDIP(wxSize(16, 16))));
	menu.Bind(wxEVT_MENU, [this, literal, caret](wxCommandEvent&) {
		const ibFormatString before = ibFormatString::Parse(literal.m_text);
		ibDialogFormatConstructor dialog(this, _("Format string constructor"), before, !IsEditable());
		if (dialog.ShowModal() != wxID_OK)
			return;
		const ibFormatString after = dialog.GetFormat();
		if (after == before)
			return;
		if (literal.Found()) ReplaceStringLiteral(literal, after.Render());
		else                 InsertStringLiteral(caret, after.Render());
	}, miFormat->GetId());

	// ⭐ AND THE LINQ CONSTRUCTOR — which stands on no literal, because a LINQ block is not a string: it
	// is code, at a place, reading what that place can see. It is asked of IntelliSense at the caret
	// (the text, the CHARACTER position the compiler measures in, and the module). Standing in a query
	// it opens that query and replaces it; anywhere else it writes a new block at the caret.
	//
	// ⚠ THE MODULE IS THE SERVER'S: the client's constructor is given its tab, which it asks what the compiler says,
	// and the syntax it is written in (the tab's) — linqConstructor.h.
	const int caretChars = GetRealPosition();
	wxMenuItem* miLinq = menu.Append(wxID_ANY, _("LINQ query constructor"));
	miLinq->SetBitmap(wxArtProvider::GetBitmap(wxART_LINQ_CONSTRUCTOR, wxART_FRONTEND, FromDIP(wxSize(16, 16))));
	menu.Bind(wxEVT_MENU, [this, caret, caretChars](wxCommandEvent&) {
		const wxString text = GetText();
		ibDialogLinqConstructor dialog(this, text, (unsigned int)caretChars, m_document, GetCodeStyle(), !IsEditable());
		if (dialog.ShowModal() != wxID_OK || !IsEditable())
			return;
		wxString refusal;
		const wxString block = dialog.GetBlock(refusal);
		if (block.IsEmpty())
			return;
		unsigned int from = 0, to = 0;
		if (dialog.GetReplacedSpan(from, to)) {
			// Characters to wxSTC positions: the document counts BYTES of UTF-8.
			SetTargetStart((int)text.Left(from).ToUTF8().length());
			SetTargetEnd((int)text.Left(to).ToUTF8().length());
			ReplaceTarget(block);
		}
		else
			InsertText(caret, block);
	}, miLinq->GetId());

	AppendDebugMenu(menu, menuLine);

	menu.AppendSeparator();

	// Standard clipboard primitives.
	auto* miCut       = menu.Append(wxID_CUT,       _("Cut")        + wxT("\tCtrl+X"));
	auto* miCopy      = menu.Append(wxID_COPY,      _("Copy")       + wxT("\tCtrl+C"));
	auto* miPaste     = menu.Append(wxID_PASTE,     _("Paste")      + wxT("\tCtrl+V"));
	auto* miSelectAll = menu.Append(wxID_SELECTALL, _("Select all") + wxT("\tCtrl+A"));

	miCut  ->SetBitmap(wxArtProvider::GetBitmap(wxART_CUT,   wxART_MENU));
	miCopy ->SetBitmap(wxArtProvider::GetBitmap(wxART_COPY,  wxART_MENU));
	miPaste->SetBitmap(wxArtProvider::GetBitmap(wxART_PASTE, wxART_MENU));
	// Select All has no stock id; the provider draws it (a dashed selection round the lines).
	miSelectAll->SetBitmap(wxArtProvider::GetBitmap(wxART_SELECT_ALL, wxART_DOC_MODULE, FromDIP(wxSize(16, 16))));

	miCut  ->Enable(GetSelectionStart() != GetSelectionEnd() && IsEditable());
	miCopy ->Enable(GetSelectionStart() != GetSelectionEnd());
	miPaste->Enable(CanPaste());

	wxPoint pt = event.GetPosition();
	if (pt == wxDefaultPosition) {
		// Keyboard-triggered (Shift+F10 / Menu key) — anchor at caret.
		const int pos = GetCurrentPos();
		pt = ClientToScreen(wxPoint(PointFromPosition(pos).x, PointFromPosition(pos).y));
	}
	PopupMenu(&menu, ScreenToClient(pt));
}

void ibCodeEditor::UpdateFoldLevel()
{
	std::vector<int> levels;
	if (!m_fp.UpdateFoldLevel(m_tc, GetLineCount(), levels))
		return;

	for (int line = 1; line < static_cast<int>(levels.size()); line++)
		SetFoldLevel(line, levels[line]);
}
