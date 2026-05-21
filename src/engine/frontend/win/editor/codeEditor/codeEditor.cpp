////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : autoComplete window 
////////////////////////////////////////////////////////////////////////////

#include "codeEditor.h"
#include "frontend/mainFrame/mainFrame.h"
#include "backend/moduleManager/moduleManager.h"
#include "backend/metaData.h"
#include "frontend/docView/docView.h"
#include "res/bitmaps_res.h"

#include <wx/tokenzr.h>
#include <wx/timer.h>
#include <wx/log.h>
#include <wx/utils.h>   // wxGetenv

#define DEF_LINENUMBER_ID 0
#define DEF_BREAKPOINT_ID 1
#define DEF_FOLDING_ID 2

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

wxString ibCodeEditor::MakeProcedureTemplate(const wxString& name, const wxString& args)
{
	const bool isCES = (ibCompileCode::GetCodeStyle() == CODE_CES);
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

ibCodeEditor::ibCodeEditor()
	: wxStyledTextCtrl(), m_ac(this), m_ct(this), m_fp(this)
{
}

ibCodeEditor::ibCodeEditor(ibMetaDocument* document, wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style, const wxString& name)
	: wxStyledTextCtrl(parent, id, pos, size, style, name),
	  m_document(document), m_ac(this), m_ct(this), m_fp(this)
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

	// AUTO-TRIGGER: idle-detect timer for Workmate-style inline completion.
	// Owner + Bind route wxEVT_TIMER through OnSigmaIdleTimer; ArmSigmaIdleTimer
	// (called from OnCharAdded / OnKeyDown) restarts it on every keystroke.
	m_sigmaIdleTimer.SetOwner(this);
	Bind(wxEVT_TIMER, &ibCodeEditor::OnSigmaIdleTimer, this, m_sigmaIdleTimer.GetId());

	// Operating mode is sourced from process env. Plugins inject env keys
	// only during their init_fn scope (security: SEC-CR-P1-1), so this
	// value comes from the user's shell or from a manual /etc setup until
	// we surface a session-level setting. wxAtoi returns 0 on empty / bad
	// input; clamp to the valid 0..3 range and fall back to 2 (auto-moderate)
	// when the env var is absent OR contains garbage.
	{
		const wxString envMode = wxGetenv(wxT("OES_AI_AUTOCOMPLETE_MODE"));
		long parsed = -1;
		if (!envMode.IsEmpty() && envMode.ToLong(&parsed) && parsed >= 0 && parsed <= 3) {
			m_sigmaAutoMode = static_cast<int>(parsed);
		} else {
			m_sigmaAutoMode = 2;
		}
	}

	// Free Ctrl+Alt+Space so Scintilla doesn't swallow the hotkey before
	// our OnKeyDown sees it. Used as the manual-trigger shortcut for the
	// inline AI completion (Workmate parity — code.1c.ai bind it to the
	// same chord).
	CmdKeyClear(' ', wxSTC_KEYMOD_CTRL | wxSTC_KEYMOD_ALT);

	// Replace Scintilla's built-in right-click menu with a custom one
	// so the Designer can inject the syntax-helper lookup item
	// (Ctrl+F1) above the standard Cut/Copy/Paste. UsePopUp(0)
	// disables Scintilla's auto popup; wxEVT_CONTEXT_MENU drives our
	// builder.
	UsePopUp(0);
	Bind(wxEVT_CONTEXT_MENU, &ibCodeEditor::OnContextMenu, this);

	// Free key combinations Scintilla would otherwise consume before they
	// reach the host frame's accelerator table. Without these CmdKeyClear
	// calls Scintilla's editor handler eats the key (no-op default action)
	// and the menu shortcut never fires on Windows / Linux.
	//   Ctrl+F1                — syntax-helper lookup
	//   Ctrl+Alt+F1            — toggle the syntax-helper pane
	//   F9 / Ctrl+Shift+F9     — toggle / enable-disable breakpoint
	//   F10 / F11              — step over / step into
	//   F12 / Alt+F12          — go to definition / find usages
	//   F3 / Shift+F3          — find next / previous
	CmdKeyClear(WXK_F1, wxSTC_KEYMOD_CTRL);
	CmdKeyClear(WXK_F1, wxSTC_KEYMOD_CTRL | wxSTC_KEYMOD_ALT);
	CmdKeyClear(WXK_F9, wxSTC_KEYMOD_NORM);
	CmdKeyClear(WXK_F9, wxSTC_KEYMOD_CTRL | wxSTC_KEYMOD_SHIFT);
	CmdKeyClear(WXK_F10, wxSTC_KEYMOD_NORM);
	CmdKeyClear(WXK_F11, wxSTC_KEYMOD_NORM);
	CmdKeyClear(WXK_F12, wxSTC_KEYMOD_NORM);
	CmdKeyClear(WXK_F12, wxSTC_KEYMOD_ALT);
	CmdKeyClear(WXK_F3, wxSTC_KEYMOD_NORM);
	CmdKeyClear(WXK_F3, wxSTC_KEYMOD_SHIFT);

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

	// Setup the dwell time before a tooltip is displayed.
	SetMouseDwellTime(200);

	// Setup caret line
	//SetCaretLineVisible(true);

	// miscellaneous
	SetLayoutCache(wxSTC_CACHE_PAGE);

	//Turn the fold markers red when the caret is a line in the group (optional)
	MarkerEnableHighlight(true);

	// Construct the precompiler upfront — sessionless hosts (codeRunner)
	// pass nullptr for the document, so the precompiler initialises with
	// an empty module name and no metadata. Local variable / function
	// names parsed from the live editor text still feed autocomplete;
	// metadata-driven props are simply absent.
	ibValueMetaObjectModuleBase* moduleObject = m_document != nullptr
		? m_document->ConvertMetaObjectToType<ibValueMetaObjectModuleBase>()
		: nullptr;
	m_precompileModule = new ibPrecompileCode(moduleObject);

	// For document-less hosts there is no LoadModule() to flip the
	// "initial text loaded" gate — start initialised so OnTextChange
	// runs the precompile pass right away.
	if (m_document == nullptr)
		m_initialized = true;
}

ibCodeEditor::~ibCodeEditor()
{
	if (m_document != nullptr) {
		const ibValueMetaObject* metaObject = m_document->GetMetaObject();
		wxASSERT(metaObject);
		const ibMetaData* metaData = metaObject->GetMetaData();
		wxASSERT(metaData);
		ibRuntimeModuleDataObject* dataRef = nullptr;
		auto* cc = metaData->GetCompileCache();
		if (cc && cc->FindCompileModule(metaObject, dataRef)) {
			ibCompileModule* compileModule = dataRef->GetCompileModule();
			if (compileModule != nullptr) compileModule->ClearLexem();
		}
	}

	wxDELETE(m_precompileModule);
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
	RefreshBreakpointMarkers();
}

void ibCodeEditor::SetCurrentLine(int lineBreakpoint, bool setBreakLine)
{
	const int firstVisibleLine = GetFirstVisibleLine(),
		linesOnScreen = LinesOnScreen();

	//Incorrect position when editing button title
	//if (!ibCodeEditor::GetSTCFocus()) 
	// CodeEditor::SetSTCFocus(true);

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

	SetIndent(m_indentationSize);
	SetTabWidth(m_indentationSize);

	bool useTabs = settings.GetUseTabs();
	bool showWhiteSpace = settings.GetShowWhiteSpace();

	SetUseTabs(useTabs);
	SetTabIndents(useTabs);
	SetBackSpaceUnIndents(useTabs);
	SetViewWhiteSpace(showWhiteSpace);

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

inline wxColour GetInverse(const wxColour& color)
{
	unsigned char r = color.Red();
	unsigned char g = color.Green();
	unsigned char b = color.Blue();

	return wxColour(r ^ 0xFF, g ^ 0xFF, b ^ 0xFF);
}

void ibCodeEditor::SetFontColorSettings(const ibFontColorSettings& settings)
{
	// For some reason StyleSetFont takes a (non-const) reference, so we need to make
	// a copy before passing it in.
	wxFont font = settings.GetFont();

	// Set STYLE_DEFAULT font + colors BEFORE StyleClearAll so the cascade
	// propagates the user's font (and point size) to every style index —
	// otherwise styles we don't touch later (annotations, indent guides,
	// callTip, etc.) keep the OS-default font and zoom adds +1pt to a
	// different base, looking out of proportion with the rest of the text.
	StyleSetFont(wxSTC_STYLE_DEFAULT, font);
	StyleSetForeground(wxSTC_STYLE_DEFAULT, settings.GetColors(ibFontColorSettings::DisplayItem_Default).foreColor);
	StyleSetBackground(wxSTC_STYLE_DEFAULT, settings.GetColors(ibFontColorSettings::DisplayItem_Default).backColor);
	StyleClearAll();

	SetSelForeground(true, settings.GetColors(ibFontColorSettings::DisplayItem_Selection).foreColor);
	SetSelBackground(true, settings.GetColors(ibFontColorSettings::DisplayItem_Selection).backColor);

	font = settings.GetFont(ibFontColorSettings::DisplayItem_Default);

	StyleSetFont(wxSTC_C_DEFAULT, font);
	StyleSetFont(wxSTC_C_IDENTIFIER, font);

	StyleSetForeground(wxSTC_C_DEFAULT, settings.GetColors(ibFontColorSettings::DisplayItem_Default).foreColor);
	StyleSetBackground(wxSTC_C_DEFAULT, settings.GetColors(ibFontColorSettings::DisplayItem_Default).backColor);

	StyleSetForeground(wxSTC_STYLE_DEFAULT, settings.GetColors(ibFontColorSettings::DisplayItem_Default).foreColor);
	StyleSetBackground(wxSTC_STYLE_DEFAULT, settings.GetColors(ibFontColorSettings::DisplayItem_Default).backColor);

	StyleSetForeground(wxSTC_C_IDENTIFIER, settings.GetColors(ibFontColorSettings::DisplayItem_Default).foreColor);
	StyleSetBackground(wxSTC_C_IDENTIFIER, settings.GetColors(ibFontColorSettings::DisplayItem_Default).backColor);

	font = settings.GetFont(ibFontColorSettings::DisplayItem_Comment);

	StyleSetFont(wxSTC_C_COMMENT, font);
	StyleSetFont(wxSTC_C_COMMENTLINE, font);
	StyleSetFont(wxSTC_C_COMMENTDOC, font);

	StyleSetForeground(wxSTC_C_COMMENT, settings.GetColors(ibFontColorSettings::DisplayItem_Comment).foreColor);
	StyleSetBackground(wxSTC_C_COMMENT, settings.GetColors(ibFontColorSettings::DisplayItem_Comment).backColor);

	StyleSetForeground(wxSTC_C_COMMENTLINE, settings.GetColors(ibFontColorSettings::DisplayItem_Comment).foreColor);
	StyleSetBackground(wxSTC_C_COMMENTLINE, settings.GetColors(ibFontColorSettings::DisplayItem_Comment).backColor);

	StyleSetForeground(wxSTC_C_COMMENTDOC, settings.GetColors(ibFontColorSettings::DisplayItem_Comment).foreColor);
	StyleSetBackground(wxSTC_C_COMMENTDOC, settings.GetColors(ibFontColorSettings::DisplayItem_Comment).backColor);

	font = settings.GetFont(ibFontColorSettings::DisplayItem_Preprocessor);

	StyleSetFont(wxSTC_C_PREPROCESSOR, font);
	StyleSetForeground(wxSTC_C_PREPROCESSOR, settings.GetColors(ibFontColorSettings::DisplayItem_Preprocessor).foreColor);
	StyleSetBackground(wxSTC_C_PREPROCESSOR, settings.GetColors(ibFontColorSettings::DisplayItem_Preprocessor).backColor);

	font = settings.GetFont(ibFontColorSettings::DisplayItem_Keyword);

	StyleSetFont(wxSTC_C_WORD, font);
	StyleSetForeground(wxSTC_C_WORD, settings.GetColors(ibFontColorSettings::DisplayItem_Keyword).foreColor);
	StyleSetBackground(wxSTC_C_WORD, settings.GetColors(ibFontColorSettings::DisplayItem_Keyword).backColor);

	font = settings.GetFont(ibFontColorSettings::DisplayItem_Operator);
	StyleSetFont(wxSTC_C_OPERATOR, font);
	StyleSetForeground(wxSTC_C_OPERATOR, settings.GetColors(ibFontColorSettings::DisplayItem_Operator).foreColor);
	StyleSetBackground(wxSTC_C_OPERATOR, settings.GetColors(ibFontColorSettings::DisplayItem_Operator).backColor);

	font = settings.GetFont(ibFontColorSettings::DisplayItem_String);

	StyleSetFont(wxSTC_C_STRING, font);
	StyleSetForeground(wxSTC_C_STRING, settings.GetColors(ibFontColorSettings::DisplayItem_String).foreColor);
	StyleSetBackground(wxSTC_C_STRING, settings.GetColors(ibFontColorSettings::DisplayItem_String).backColor);

	StyleSetFont(wxSTC_C_STRINGEOL, font);
	StyleSetForeground(wxSTC_C_STRINGEOL, settings.GetColors(ibFontColorSettings::DisplayItem_String).foreColor);
	StyleSetBackground(wxSTC_C_STRINGEOL, settings.GetColors(ibFontColorSettings::DisplayItem_String).backColor);

	StyleSetFont(wxSTC_C_CHARACTER, font);
	StyleSetForeground(wxSTC_C_CHARACTER, settings.GetColors(ibFontColorSettings::DisplayItem_String).foreColor);
	StyleSetBackground(wxSTC_C_CHARACTER, settings.GetColors(ibFontColorSettings::DisplayItem_String).backColor);

	StyleSetFont(wxSTC_C_CHARACTER, font);
	StyleSetForeground(wxSTC_C_CHARACTER, settings.GetColors(ibFontColorSettings::DisplayItem_Selection).foreColor);
	StyleSetBackground(wxSTC_C_CHARACTER, settings.GetColors(ibFontColorSettings::DisplayItem_Selection).backColor);

	font = settings.GetFont(ibFontColorSettings::DisplayItem_Number);

	StyleSetFont(wxSTC_C_NUMBER, font);
	StyleSetForeground(wxSTC_C_NUMBER, settings.GetColors(ibFontColorSettings::DisplayItem_Number).foreColor);
	StyleSetBackground(wxSTC_C_NUMBER, settings.GetColors(ibFontColorSettings::DisplayItem_Number).backColor);

	// Apply the full font to the line-m_number margin, not just its size — otherwise
	// the margin keeps the default monospace face until a style cascade refresh
	// (e.g. user opens Settings and saves) happens to pull it through.
	StyleSetFont(wxSTC_STYLE_LINENUMBER, font);
	StyleSetSize(wxSTC_STYLE_LINENUMBER, font.GetPointSize());
	// Soften the gutter — muted gray digits on the darkest gradient step
	// (line-number margin is leftmost; breakpoint + fold margins step
	// progressively lighter, see the SetMarginBackground calls in
	// SetEditorSettings).
	StyleSetForeground(wxSTC_STYLE_LINENUMBER, wxColour(0xA0, 0xA0, 0xA0));
	StyleSetBackground(wxSTC_STYLE_LINENUMBER, wxColour(0xE8, 0xE8, 0xE8));

	// Set the caret color as the inverse of the background color so it's always visible.
	SetCaretForeground(GetInverse(settings.GetColors(ibFontColorSettings::DisplayItem_Default).backColor));

	// Reapply brace-highlight styles — StyleClearAll above resets every
	// style index (incl. BRACELIGHT/BRACEBAD) to the OS default. Without
	// also pushing the user's font here the brace would visibly shift
	// (different glyph metrics) when the highlight kicks in.
	StyleSetFont(wxSTC_STYLE_BRACELIGHT, font);
	StyleSetForeground(wxSTC_STYLE_BRACELIGHT, *wxRED);
	StyleSetFont(wxSTC_STYLE_BRACEBAD, font);
	StyleSetForeground(wxSTC_STYLE_BRACEBAD, *wxRED);
}

bool ibCodeEditor::LoadModule()
{
	ClearAll();
	wxDELETE(m_precompileModule);

	if (m_document != nullptr) {
		ibValueMetaObjectModuleBase* moduleObject = m_document->ConvertMetaObjectToType<ibValueMetaObjectModuleBase>();
		if (moduleObject != nullptr) {
			m_precompileModule = new ibPrecompileCode(moduleObject);

			if (IsEditable()) {
				SetText(moduleObject->GetModuleText()); m_initialized = true;
			}
			else {
				SetReadOnly(false);
				SetText(moduleObject->GetModuleText()); m_initialized = true;
				SetReadOnly(true);
			}

			m_precompileModule->Load(moduleObject->GetModuleText());

			try {
				m_precompileModule->PrepareLexem();
			}
			catch (...) {
			}

			EmptyUndoBuffer();
		}

		m_fp.RecalcFoldLevel();
		RefreshEditor();
		return moduleObject != nullptr;
	}

	return m_document != nullptr;
}

bool ibCodeEditor::SaveModule()
{
	if (m_document != nullptr) {

		ibValueMetaObjectModuleBase* moduleObject = m_document->ConvertMetaObjectToType<ibValueMetaObjectModuleBase>();

		if (moduleObject != nullptr) {
			moduleObject->SetModuleText(GetText());
			return true;
		}
	}

	return m_document != nullptr;
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

#include "frontend/win/dlgs/lineInput/lineInput.h"
#include "frontend/win/dlgs/functionSearcher/functionSearcher.h"

void ibCodeEditor::RefreshEditor()
{
	ibCodeEditor::SetEditorSettings(mainFrame->GetEditorSettings());
	ibCodeEditor::SetFontColorSettings(mainFrame->GetFontColorSettings());

	ibCodeEditor::RefreshBreakpoint();
}

void ibCodeEditor::ActivateEditor()
{
	if (m_document != nullptr) {
	
		ibValueMetaObjectModuleBase* moduleObject = m_document->ConvertMetaObjectToType<ibValueMetaObjectModuleBase>();	
		if (moduleObject != nullptr && (moduleObject->GetClassType() == g_metaModuleCLSID || moduleObject->GetClassType() == g_metaManagerCLSID))
			objectInspector->SelectObject(moduleObject->GetParent());
		else
			objectInspector->SelectObject(moduleObject);
	}
	
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

#include "frontend/window_ptr.h"

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

#include "frontend/mainFrame/mainFrame.h" // editor / host command ids
#include "backend/plugin/pluginManager.h" // appData->GetPluginManager() Sigma gate
#include "backend/appData.h"

void ibCodeEditor::OnContextMenu(wxContextMenuEvent& event)
{
	// Composed-from-commands context menu. Each entry has a stable
	// menu-event id; events propagate up to the host frame, so a single
	// editor instance can be embedded under different hosts (Designer
	// vs codeRunner) without rewiring per host. Items without a host
	// binding render as disabled stubs — keeps the menu shape stable
	// while signalling which features are not available in the current
	// configuration.
	wxMenu menu;

	// --- Clipboard primitives (wxStyledTextCtrl built-ins) -------------
	auto* miCut   = menu.Append(wxID_CUT,    _("Cut\tCtrl+X"));
	auto* miCopy  = menu.Append(wxID_COPY,   _("Copy\tCtrl+C"));
	auto* miPaste = menu.Append(wxID_PASTE,  _("Paste\tCtrl+V"));
	miCut  ->Enable(GetSelectionStart() != GetSelectionEnd() && !GetReadOnly());
	miCopy ->Enable(GetSelectionStart() != GetSelectionEnd());
	miPaste->Enable(CanPaste());

	// --- Syntax helper lookup — Ctrl+F1 (RawCtrl on macOS for parity) --
	const wxString identifier = GetIdentifierUnderCursor();
	auto* miLookup = menu.Append(
	    wxID_FRONTEND_SYNTAX_HELPER_LOOKUP,
	    _("Look up in Syntax Helper\tRawCtrl+F1"));
	miLookup->Enable(!identifier.IsEmpty());

	menu.Append(wxID_SELECTALL, _("Select All\tCtrl+A"));
	menu.AppendSeparator();

	// --- Navigation -----------------------------------------------------
	// Phase 3 wires Go to Line locally (it's a public method on this
	// editor). Find Next / Find Previous / Go to Definition / Find
	// Usages are command-registry stubs — the menu shape matches the
	// target IDE layout, but the actions are disabled until the
	// underlying handlers ship in Phase 4.
	auto* miFindNext = menu.Append(wxID_HIGHEST + 4001, _("Find Next\tF3"));
	auto* miFindPrev = menu.Append(wxID_HIGHEST + 4002, _("Find Previous\tShift+F3"));
	auto* miGotoLine = menu.Append(wxID_HIGHEST + 4003, _("Go to Line...\tCtrl+G"));
	miFindNext->Enable(false);
	miFindPrev->Enable(false);

	menu.AppendSeparator();

	auto* miGotoDef    = menu.Append(wxID_HIGHEST + 4010, _("Go to Definition\tF12"));
	auto* miFindUsages = menu.Append(wxID_HIGHEST + 4011, _("Find Usages\tAlt+F12"));
	miGotoDef   ->Enable(false);
	miFindUsages->Enable(false);

	menu.AppendSeparator();

	// --- Breakpoints ----------------------------------------------------
	// Toggle hits OnEditDebugPoint(line) (codeEditor.h:484), which the
	// Designer override (codeEditorDesigner.cpp:16) routes through
	// debugClient. The frontend stub here drives the same path via the
	// margin-click route — the menu commits the current line and
	// triggers the existing handler indirectly via menu id, so the
	// codeRunner / standalone case does not need its own handler.
	auto* miBpToggle = menu.Append(wxID_HIGHEST + 4020, _("Toggle Breakpoint\tF9"));
	auto* miBpCond   = menu.Append(wxID_HIGHEST + 4021, _("Conditional Breakpoint..."));
	auto* miBpEnable = menu.Append(wxID_HIGHEST + 4022, _("Enable / Disable Breakpoint\tCtrl+Shift+F9"));
	menu.Append(wxID_FRONTEND_DEBUG_REMOVE_ALL_BREAKPOINTS,
	            _("Remove All Breakpoints"));
	miBpCond  ->Enable(false);
	miBpEnable->Enable(false);

	menu.AppendSeparator();

	// --- AI Assistant skills — Phase 6.1 -------------------------------
	// Right-click skill submenu modern AI IDE assistants ship (Explain
	// / Review / Fix / Doc-gen / Send to chat). Submenu label is
	// dynamic: when a plugin has registered an AI provider, the label
	// shows that provider's displayName — host stays vendor-neutral.
	// With nothing installed
	// it falls back to a generic "AI Assistant (no plugin installed)"
	// and all items grey out.
	{
		wxMenu* aiSub = new wxMenu();
		auto* miExplain = aiSub->Append(wxID_HIGHEST + 4030,
		                                  _("Объяснить код\tAlt+I,E"));
		auto* miReview  = aiSub->Append(wxID_HIGHEST + 4031,
		                                  _("Проверить код\tAlt+I,R"));
		auto* miFix     = aiSub->Append(wxID_HIGHEST + 4032,
		                                  _("Исправить код\tAlt+I,C"));
		auto* miDoc     = aiSub->Append(wxID_HIGHEST + 4033,
		                                  _("Сгенерировать документирующий комментарий\tAlt+I,G"));
		auto* miSend    = aiSub->Append(wxID_HIGHEST + 4034,
		                                  _("Отправить выделенное в чат\tAlt+I,S"));
		// Triple-review operates on the whole module (no selection
		// requirement) — aiBridge fans the text out to multiple LLMs
		// for a consensus verdict.
		auto* miTriple  = aiSub->Append(wxID_HIGHEST + 4035,
		                                  _("Triple-review модуля\tAlt+I,T"));
		// AGENT-MODE: 7th skill — drive the oes_agent MCP tool to create
		// real metadata objects. Always enabled when AI is present (no
		// selection / text required — the agent works from a prompt).
		auto* miAgent   = aiSub->Append(wxID_HIGHEST + 4036,
		                                  _("Создать объект через агента\tAlt+I,A"));
		// COMMIT-MSG: 8th skill — mirrors 1С:Workmate "Generate Commit
		// Message". Editor buffer is irrelevant here; inputs come from
		// `git diff` invoked at click time. Lives on the AI Assistant
		// submenu so VCS-adjacent AI flows stay in one place.
		auto* miCommit  = aiSub->Append(wxID_HIGHEST + 4037,
		                                  _("Сгенерировать сообщение коммита\tAlt+I,M"));

		auto* pm = appData ? appData->GetPluginManager() : nullptr;
		const bool hasAI = pm && pm->HasAIProviderFor("chat");
		const bool hasSelection = GetSelectionStart() != GetSelectionEnd();
		miExplain->Enable(hasAI && hasSelection);
		miReview ->Enable(hasAI && hasSelection);
		miFix    ->Enable(hasAI && hasSelection);
		miSend   ->Enable(hasAI && hasSelection);
		miDoc    ->Enable(hasAI);
		// Triple-review reads the whole module — gated only on provider
		// availability, not on a non-empty selection.
		miTriple ->Enable(hasAI && GetTextLength() > 0);
		miAgent  ->Enable(hasAI);
		// Commit-message is editor-independent — gate only on AI
		// availability. The git-repo / staged-changes check happens
		// inside the click handler so the menu doesn't spawn a child
		// process on every right-click.
		miCommit ->Enable(hasAI);

		// Always "AI Assistant" — host-neutral; specific provider name
		// is intentionally not exposed in the submenu label.
		menu.AppendSubMenu(aiSub, hasAI
		    ? _("AI Assistant")
		    : _("AI Assistant (no plugin installed)"));
	}

	menu.AppendSeparator();

	// --- Debug-session commands ----------------------------------------
	menu.Append(wxID_FRONTEND_DEBUG_STEP_INTO,  _("Step Into\tF11"));
	menu.Append(wxID_FRONTEND_DEBUG_STEP_OVER,  _("Step Over\tF10"));

	// --- Local handlers for self-contained items -----------------------
	Bind(wxEVT_MENU,
	     [this](wxCommandEvent&) { Cut();        }, wxID_CUT);
	Bind(wxEVT_MENU,
	     [this](wxCommandEvent&) { Copy();       }, wxID_COPY);
	Bind(wxEVT_MENU,
	     [this](wxCommandEvent&) { Paste();      }, wxID_PASTE);
	Bind(wxEVT_MENU,
	     [this](wxCommandEvent&) { SelectAll();  }, wxID_SELECTALL);
	Bind(wxEVT_MENU,
	     [this](wxCommandEvent&) { ShowGotoLine(); }, wxID_HIGHEST + 4003);
	Bind(wxEVT_MENU,
	     [this](wxCommandEvent&) {
		     const int line = LineFromPosition(GetCurrentPos()) + 1;
		     OnEditDebugPoint(line);
	     },
	     wxID_HIGHEST + 4020);

	// --- Route host-frame commands explicitly to the host MDI parent.
	//
	// wxStyledTextCtrl's PopupMenu does not always propagate
	// wxEVT_MENU through wxAUI / wxAuiDocMDIFrame parents to the host
	// frame — wxAuiDocMDIChildFrame is itself a wxFrame on Windows,
	// so wxGetTopLevelParent stops there instead of climbing to the
	// outer MDI parent where the host's Bind() lives. Walk the
	// parent chain manually, firing the event at every frame on the
	// way up until one of them handles it. ProcessEvent returns true
	// only when a Bind() matches; on false we keep walking.
	auto routeUp = [this](int id) {
		return [this, id](wxCommandEvent&) {
			wxLogMessage(wxT("[help-router] click id=%d this=%p"),
			             id, static_cast<void*>(this));
			wxCommandEvent up(wxEVT_MENU, id);
			up.SetEventObject(this);
			for (wxWindow* p = GetParent(); p != nullptr; p = p->GetParent()) {
				const wxString cls = p->GetClassInfo()
				                       ? p->GetClassInfo()->GetClassName()
				                       : wxT("?");
				const bool handled = p->ProcessWindowEvent(up);
				wxLogMessage(wxT("[help-router] parent=%p class=%s handled=%d"),
				             static_cast<void*>(p), cls, handled ? 1 : 0);
				if (handled) return;
			}
			if (wxTheApp) {
				if (wxWindow* top = wxTheApp->GetTopWindow()) {
					const bool handled = top->ProcessWindowEvent(up);
					wxLogMessage(wxT("[help-router] top=%p class=%s handled=%d"),
					             static_cast<void*>(top),
					             top->GetClassInfo()
					                 ? top->GetClassInfo()->GetClassName()
					                 : wxT("?"),
					             handled ? 1 : 0);
				}
			}
		};
	};
	// Sigma skill clicks — build an editor.skill JSON envelope and ship
	// it to the default AI pane via WebPaneSend. Plugin's onMessage
	// handler picks it up, runs the corresponding prompt template, and
	// streams the reply back through the chat.delta envelope. The pane
	// will auto-open via CallWebPaneShow before the envelope arrives.
	auto sendSkill = [this](const wxString& op) {
		return [this, op](wxCommandEvent&) {
			auto* pm = appData ? appData->GetPluginManager() : nullptr;
			if (pm == nullptr) return;
			// Triple-review reads the whole module text and tags the
			// language so aiBridge / the LLMs know which syntax they're
			// looking at. Every other skill keeps the legacy
			// "selection-or-current-line" behaviour.
			// AGENT-MODE: the agent skill ships the whole module as
			// context — the user's prompt becomes the natural-language
			// instruction, the editor body grounds it.
			const bool wholeModule = (op == wxT("triple-review") ||
			                            op == wxT("agent"));
			wxString code;
			wxString language = wxT("ces");
			if (wholeModule) {
				code = GetText();
				language = (ibCompileCode::GetCodeStyle() == CODE_VES)
				    ? wxString(wxT("VES"))
				    : wxString(wxT("CES"));
			} else {
				const wxString sel = GetSelectedText();
				// Doc-gen falls back to the current line when nothing is
				// selected — matches the "Generate doc comment for cursor
				// procedure" UX in modern AI IDE assistants.
				code = sel;
				if (code.IsEmpty() && op == wxT("doc")) {
					const int line = LineFromPosition(GetCurrentPos());
					code = GetLine(line);
				}
			}
			// Build envelope by hand — bringing nlohmann into the
			// frontend editor.cpp would widen the include surface; the
			// JSON is small and field set is fixed.
			auto esc = [](const wxString& s) {
				wxString out; out.reserve(s.size() + 8);
				for (wxUniChar c : s) {
					const auto v = static_cast<unsigned>(c.GetValue());
					if (c == wxT('\\')) out += wxT("\\\\");
					else if (c == wxT('"')) out += wxT("\\\"");
					else if (c == wxT('\n')) out += wxT("\\n");
					else if (c == wxT('\r')) out += wxT("\\r");
					else if (c == wxT('\t')) out += wxT("\\t");
					else if (v < 0x20) out += wxString::Format(wxT("\\u%04x"), v);
					else out += c;
				}
				return out;
			};
			const wxString rid = wxString::Format(wxT("skill-%lld"),
			                                       static_cast<long long>(wxGetUTCTime()));
			wxString json = wxT("{\"kind\":\"editor.skill\",\"op\":\"");
			json += op;
			json += wxT("\",\"language\":\"");
			json += language;
			json += wxT("\",\"code\":\"");
			json += esc(code);
			json += wxT("\",\"requestId\":\"");
			json += rid;
			json += wxT("\"}");

			// Open / focus the AI Assistant pane via the main-frame handler
			// (creates it lazily if no plugin registered one), then route
			// the skill envelope to whichever pane the plugin manager
			// considers default. The default is the first SUCCESSFUL
			// RegisterWebPane call; if the user has installed an AI plugin
			// that pane id matches. Falling back to the menu's demo bundle
			// keeps the path alive even when no real provider exists, but
			// in that case the envelope dies silently — which is fine
			// because HasAIProviderFor() greyed the menu out anyway.
			wxCommandEvent openEvt(wxEVT_MENU, wxID_FRONTEND_PLUGIN_WEB_PANE);
			openEvt.SetEventObject(this);
			for (wxWindow* p = GetParent(); p != nullptr; p = p->GetParent()) {
				if (p->ProcessWindowEvent(openEvt)) break;
			}
			wxString target = pm->GetDefaultAIPaneId();
			if (target.IsEmpty()) target = wxT("designer.demo.chat");
			pm->CallWebPaneSend(target, json);
		};
	};
	Bind(wxEVT_MENU, sendSkill(wxT("explain")),       wxID_HIGHEST + 4030);
	Bind(wxEVT_MENU, sendSkill(wxT("review")),        wxID_HIGHEST + 4031);
	Bind(wxEVT_MENU, sendSkill(wxT("fix")),           wxID_HIGHEST + 4032);
	Bind(wxEVT_MENU, sendSkill(wxT("doc")),           wxID_HIGHEST + 4033);
	Bind(wxEVT_MENU, sendSkill(wxT("send")),          wxID_HIGHEST + 4034);
	Bind(wxEVT_MENU, sendSkill(wxT("triple-review")), wxID_HIGHEST + 4035);
	// AGENT-MODE: agent skill reuses sendSkill so the editor.skill envelope
	// shape stays uniform; aiBridge dispatches on op="agent" to RunOesAgent.
	Bind(wxEVT_MENU, sendSkill(wxT("agent")),         wxID_HIGHEST + 4036);

	// COMMIT-MSG: dedicated handler — editor buffer is irrelevant; the
	// LLM input is `git diff`. Capture staged + unstaged diff under
	// section headers, cap the total at 50 KB,
	// and ship through the same editor.skill / CallWebPaneSend pipeline
	// the rest of the skills use. aiBridge's opLabels carries the
	// Russian Conventional-Commits prompt for op="commit".
	Bind(wxEVT_MENU, [this](wxCommandEvent&) {
		auto* pm = appData ? appData->GetPluginManager() : nullptr;
		if (pm == nullptr) return;

		// `git` not in PATH → log + dialog and bail; the menu does
		// not pre-check because spawning a child on every right-click
		// is wasteful.
		wxArrayString verOut, verErr;
		const long verRc = wxExecute(wxT("git --version"), verOut, verErr,
		                              wxEXEC_SYNC | wxEXEC_NODISABLE);
		if (verRc != 0) {
			wxLogMessage(wxT("[commit] git not found in PATH, skipping commit message generation"));
			wxMessageBox(_("Утилита git не найдена в PATH. Установите git и повторите."),
			              _("Сгенерировать сообщение коммита"),
			              wxOK | wxICON_INFORMATION);
			return;
		}

		// Verify the CWD is inside a git repo. `git rev-parse
		// --show-toplevel` exits 0 inside one; outside a repo it
		// exits 128.
		wxArrayString topOut, topErr;
		const long topRc = wxExecute(wxT("git rev-parse --show-toplevel"),
		                              topOut, topErr,
		                              wxEXEC_SYNC | wxEXEC_NODISABLE);
		if (topRc != 0 || topOut.IsEmpty()) {
			wxMessageBox(_("Текущий каталог не является git-репозиторием."),
			              _("Сгенерировать сообщение коммита"),
			              wxOK | wxICON_INFORMATION);
			return;
		}

		// Capture staged + unstaged diffs separately. `--no-pager` so
		// git doesn't pipe through less; `--no-color` so the LLM
		// doesn't see ANSI escapes.
		wxArrayString stagedOut, stagedErr;
		wxExecute(wxT("git --no-pager diff --cached --no-color"),
		           stagedOut, stagedErr,
		           wxEXEC_SYNC | wxEXEC_NODISABLE);
		wxArrayString unstagedOut, unstagedErr;
		wxExecute(wxT("git --no-pager diff --no-color"),
		           unstagedOut, unstagedErr,
		           wxEXEC_SYNC | wxEXEC_NODISABLE);

		// Concatenate with section headers. Empty sections drop so
		// the LLM doesn't waste context on "(empty)" labels.
		wxString diff;
		auto append = [&diff](const wxString& header, const wxArrayString& lines) {
			if (lines.IsEmpty()) return;
			diff += header;
			diff += wxT("\n");
			for (const wxString& ln : lines) {
				diff += ln;
				diff += wxT("\n");
			}
			diff += wxT("\n");
		};
		append(wxT("=== staged (git diff --cached) ==="), stagedOut);
		append(wxT("=== unstaged (git diff) ==="),       unstagedOut);

		if (diff.IsEmpty()) {
			wxMessageBox(_("Нет изменений для коммита (ни staged, ни unstaged)."),
			              _("Сгенерировать сообщение коммита"),
			              wxOK | wxICON_INFORMATION);
			return;
		}

		// Cap payload at 50 KB. Past this the prompt budget pushes
		// context out at the LLM side regardless.
		constexpr size_t kMaxDiffBytes = 50 * 1024;
		if (diff.utf8_str().length() > kMaxDiffBytes) {
			diff = diff.Mid(0, kMaxDiffBytes);
			diff += wxT("\n\n[diff truncated]\n");
		}

		// JSON-escape mirror of the sendSkill helper, inlined here
		// to keep the handler self-contained.
		auto esc = [](const wxString& s) {
			wxString out; out.reserve(s.size() + 8);
			for (wxUniChar c : s) {
				const auto v = static_cast<unsigned>(c.GetValue());
				if (c == wxT('\\')) out += wxT("\\\\");
				else if (c == wxT('"')) out += wxT("\\\"");
				else if (c == wxT('\n')) out += wxT("\\n");
				else if (c == wxT('\r')) out += wxT("\\r");
				else if (c == wxT('\t')) out += wxT("\\t");
				else if (v < 0x20) out += wxString::Format(wxT("\\u%04x"), v);
				else out += c;
			}
			return out;
		};
		const wxString rid = wxString::Format(wxT("skill-%lld"),
		                                       static_cast<long long>(wxGetUTCTime()));
		wxString json = wxT("{\"kind\":\"editor.skill\",\"op\":\"commit\","
		                   "\"language\":\"diff\",\"code\":\"");
		json += esc(diff);
		json += wxT("\",\"requestId\":\"");
		json += rid;
		json += wxT("\"}");

		// Open / focus the AI pane (lazy-created when no plugin has
		// registered one) and ship the envelope to the default chat
		// pane. Same path as the other skill clicks.
		wxCommandEvent openEvt(wxEVT_MENU, wxID_FRONTEND_PLUGIN_WEB_PANE);
		openEvt.SetEventObject(this);
		for (wxWindow* p = GetParent(); p != nullptr; p = p->GetParent()) {
			if (p->ProcessWindowEvent(openEvt)) break;
		}
		wxString target = pm->GetDefaultAIPaneId();
		if (target.IsEmpty()) target = wxT("designer.demo.chat");
		pm->CallWebPaneSend(target, json);
	}, wxID_HIGHEST + 4037);

	Bind(wxEVT_MENU,
	     routeUp(wxID_FRONTEND_SYNTAX_HELPER_LOOKUP),
	     wxID_FRONTEND_SYNTAX_HELPER_LOOKUP);
	Bind(wxEVT_MENU,
	     routeUp(wxID_FRONTEND_DEBUG_STEP_INTO),
	     wxID_FRONTEND_DEBUG_STEP_INTO);
	Bind(wxEVT_MENU,
	     routeUp(wxID_FRONTEND_DEBUG_STEP_OVER),
	     wxID_FRONTEND_DEBUG_STEP_OVER);
	Bind(wxEVT_MENU,
	     routeUp(wxID_FRONTEND_DEBUG_REMOVE_ALL_BREAKPOINTS),
	     wxID_FRONTEND_DEBUG_REMOVE_ALL_BREAKPOINTS);

	PopupMenu(&menu);
	event.Skip(false);
}

#include "backend/system/systemManager.h"

bool ibCodeEditor::SyntaxControl(bool throwMessage) const
{
	const ibValueMetaObject* metaObject = m_document->GetMetaObject();
	wxASSERT(metaObject);
	const ibMetaData* metaData = metaObject->GetMetaData();
	wxASSERT(metaData);
	ibRuntimeModuleDataObject* dataRef = nullptr;
	auto* cc = metaData->GetCompileCache();
	if (cc && cc->FindCompileModule(metaObject, dataRef)) {
		ibCompileModule* compileModule = dataRef->GetCompileModule();
		try {
			if (compileModule->Compile()) {
				if (throwMessage)
					ibValueSystemFunction::Message(_("No syntax errors detected!"));
				return true;
			}
			wxASSERT("ibCompileCode::Compile return false");
			return false;

		}
		catch (...) {

			if (!throwMessage) {
				int answer = wxMessageBox(
					_("Errors were found while checking module. Do you want to continue ?"), compileModule->GetModuleName(),
					wxYES_NO | wxCENTRE);

				if (answer == wxNO)
					return false;
			}
		}
	}

	return true;
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

	while (!m_tc.IsEnd()) {
#ifdef UTF8_LEXEM_TRANSLATE
		currPos = fromPos + m_tc.GetCurrentUtf8Pos();
#else 
		currPos = fromPos + m_tc.GetCurrentPos();
#endif 
		if (m_tc.IsWord()) {
			(void)m_tc.GetWord(word, false, true);
			const short keyWord = ibTranslateCode::IsKeyWord(word);
			if (keyWord != wxNOT_FOUND) {
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
		}
		else {
			(void)m_tc.GetByte();
			appendStyle(wxSTC_C_IDENTIFIER);
		}
	}

	m_fp.UpdateFoldLevel();
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

	if (!m_initialized || m_precompileModule == nullptr)
		return;

	const wxString& codeText = GetText();
	const int line = LineFromPosition(event.GetPosition());

	// Precompile pass — fires for every host (codeRunner included).
	// Tracks local declarations + functions parsed out of the live
	// editor text; metadata-driven props come in only when a backing
	// document exists (PrepareModuleData early-returns otherwise).
	m_precompileModule->Load(codeText);

	if (event.m_linesAdded != 0) {

		OnPatchModule(line, event.m_linesAdded);

		if (m_lineBreakpoint != wxNOT_FOUND) {
			MarkerDeleteAll(ibCodeEditor::BreakLine);
			if (line < m_lineBreakpoint)
				m_lineBreakpoint += event.m_linesAdded;
			MarkerAdd(m_lineBreakpoint, ibCodeEditor::BreakLine);
		}

		RefreshBreakpoint();
	}

	try {
#if _USE_OLD_TEXT_PARSER_IN_CODE_EDITOR == 0
		// First-time-empty buffer (codeRunner initial SetText, or any
		// host that hasn't called LoadModule) — incremental PrepareLexem
		// early-returns when m_listLexem is empty, so the fold parser
		// gets no KEYWORD lexems and folding doesn't kick in. Bootstrap
		// with a full pass so subsequent edits have a baseline to patch.
		if (m_precompileModule->GetLexems().empty()) {
			m_precompileModule->PrepareLexem();
		}
		else {
			const wxString& patchText = event.GetString();
			const int str_length = patchText.Length();
			const int str_utf8_length = event.GetLength();
			if ((modFlags & (wxSTC_MOD_INSERTTEXT)) != 0) {
				m_precompileModule->PrepareLexem(line,
#ifdef UTF8_LEXEM_TRANSLATE
					event.m_linesAdded, str_length, str_utf8_length);
#else
					event.m_linesAdded, str_length);
#endif
			}
			else if ((modFlags & (wxSTC_MOD_DELETETEXT)) != 0) {
				m_precompileModule->PrepareLexem(line,
#ifdef UTF8_LEXEM_TRANSLATE
					event.m_linesAdded, -str_length, str_utf8_length);
#else
					event.m_linesAdded, -str_length);
#endif
			}
		}
#else
		m_precompileModule->PrepareLexem();
#endif
	}
	catch (...)
	{
	}

	// Document / metadata-side update — only when a backing document
	// is present. CodeRunner edits live editor text in memory only;
	// nothing to push back to a moduleObject / mark dirty / invalidate
	// in the compile cache.
	if (m_document != nullptr) {
		ibValueMetaObjectModuleBase* moduleObject =
			m_document->ConvertMetaObjectToType<ibValueMetaObjectModuleBase>();
		if (moduleObject != nullptr) {
			ibMetaData* metaData = moduleObject->GetMetaData();
			wxASSERT(metaData);

			ibRuntimeModuleDataObject* pRefData = nullptr;
			auto* cc = metaData->GetCompileCache();
			if (cc && cc->FindCompileModule(m_document->GetMetaObject(), pRefData)) {
				ibCompileCode* compileModule = pRefData->GetCompileModule();
				wxASSERT(compileModule);
				if (!compileModule->m_changedCode) compileModule->m_changedCode = true;
			}

			m_document->Modify(true);
			moduleObject->SetModuleText(codeText);
		}
	}

	m_fp.RecalcFoldLevel();
}

void ibCodeEditor::OnKeyDown(wxKeyEvent& event)
{
	if (!IsEditable()) {
		event.Skip(); return;
	}

	// Sigma AI inline completion hotkeys:
	//   Ctrl+I (RawCtrl+I on macOS)    — request completion at caret
	//   Ctrl+Alt+Space                 — Workmate-parity manual trigger
	//   Tab inside pending suggestion  — accept + insert
	//   Esc inside pending suggestion  — dismiss
	// Tab/Esc only intercepted when a suggestion is pending; otherwise
	// they fall through to default Scintilla behaviour. Accept MUST run
	// before the idle-timer rearm path below so the existing pending
	// suggestion gets committed instead of redundantly re-firing the LLM.
	if (HasPendingSigmaCompletion()) {
		if (event.GetKeyCode() == WXK_TAB) {
			AcceptSigmaCompletion();
			return;
		}
		if (event.GetKeyCode() == WXK_ESCAPE) {
			DismissSigmaCompletion();
			return;
		}
		// Any other key dismisses the suggestion + falls through so the
		// user's typing actually lands in the buffer instead of being
		// interpreted as part of the AI affordance.
		DismissSigmaCompletion();
	}
	if ((event.RawControlDown() || event.ControlDown()) &&
	    !event.ShiftDown() && !event.AltDown() &&
	    event.GetKeyCode() == 'I') {
		TriggerSigmaCompletion();
		return;
	}
	// Ctrl+Alt+Space — manual trigger. Off-mode disables the hotkey
	// (m_sigmaAutoMode == 0); manual-only mode (1) and both auto modes
	// (2/3) still respond. We consume the event so Scintilla doesn't
	// insert a literal space.
	if (event.ControlDown() && event.AltDown() && !event.ShiftDown() &&
	    event.GetKeyCode() == WXK_SPACE) {
		if (m_sigmaAutoMode != 0) {
			TriggerSigmaCompletion();
		}
		return;
	}
	if (!event.ControlDown() && !event.RawControlDown() &&
	    !event.AltDown() && !event.ShiftDown() &&
	    event.GetKeyCode() == WXK_F1) {
		TriggerDocCommentSkill();
		return;
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
			int endPos = GetLineEndPosition(line);

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
			int endPos = GetLineEndPosition(line);

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

	// Restart the idle timer for the Workmate-style auto-trigger. The
	// hotkey paths above all return early, so we only land here for
	// regular typing / navigation — exactly the events that should
	// restart the debounce window.
	ArmSigmaIdleTimer();
}

// ===========================================================================
// Sigma AI inline completion (Phase 6.2)
// ===========================================================================

wxString ibCodeEditor::FindNearestRoutineSignature() const
{
	int line = LineFromPosition(GetCurrentPos());
	const int minLine = line > 80 ? line - 80 : 0;
	for (; line >= minLine; --line) {
		wxString text = const_cast<ibCodeEditor*>(this)->GetLine(line);
		text.Trim(false).Trim(true);
		if (text.IsEmpty()) continue;
		const wxString lower = text.Lower();
		if (lower.StartsWith(wxT("procedure ")) ||
		    lower.StartsWith(wxT("function ")) ||
		    lower.StartsWith(wxT("процедура ")) ||
		    lower.StartsWith(wxT("функция "))) {
			wxString signature = text;
			for (int next = line + 1;
			     next < GetLineCount() && next < line + 8;
			     ++next) {
				wxString extra = const_cast<ibCodeEditor*>(this)->GetLine(next);
				extra.Trim(false).Trim(true);
				if (extra.IsEmpty()) break;
				signature += wxT("\n") + extra;
				if (extra.Find(wxT("{")) != wxNOT_FOUND ||
				    extra.Find(wxT(")")) != wxNOT_FOUND) {
					break;
				}
			}
			return signature;
		}
	}
	return const_cast<ibCodeEditor*>(this)->GetLine(LineFromPosition(GetCurrentPos()));
}

void ibCodeEditor::TriggerDocCommentSkill()
{
	auto* pm = appData ? appData->GetPluginManager() : nullptr;
	if (pm == nullptr || !pm->HasAIProviderFor("chat")) {
		wxBell();
		return;
	}

	wxString code = GetSelectedText();
	if (code.IsEmpty()) code = FindNearestRoutineSignature();
	code.Trim(false).Trim(true);
	if (code.IsEmpty()) {
		wxBell();
		return;
	}

	auto esc = [](const wxString& s) {
		wxString out; out.reserve(s.size() + 8);
		for (wxUniChar c : s) {
			const auto v = static_cast<unsigned>(c.GetValue());
			if (c == wxT('\\')) out += wxT("\\\\");
			else if (c == wxT('"')) out += wxT("\\\"");
			else if (c == wxT('\n')) out += wxT("\\n");
			else if (c == wxT('\r')) out += wxT("\\r");
			else if (c == wxT('\t')) out += wxT("\\t");
			else if (v < 0x20) out += wxString::Format(wxT("\\u%04x"), v);
			else out += c;
		}
		return out;
	};

	const wxString rid = wxString::Format(wxT("skill-%lld"),
	                                      static_cast<long long>(wxGetUTCTime()));
	wxString json = wxT("{\"kind\":\"editor.skill\",\"op\":\"doc\","
	                    "\"language\":\"ces\",\"code\":\"");
	json += esc(code);
	json += wxT("\",\"requestId\":\"");
	json += rid;
	json += wxT("\"}");

	wxCommandEvent openEvt(wxEVT_MENU, wxID_FRONTEND_PLUGIN_WEB_PANE);
	openEvt.SetEventObject(this);
	for (wxWindow* p = GetParent(); p != nullptr; p = p->GetParent()) {
		if (p->ProcessWindowEvent(openEvt)) break;
	}
	wxString target = pm->GetDefaultAIPaneId();
	if (target.IsEmpty()) target = wxT("designer.demo.chat");
	pm->CallWebPaneSend(target, json);
}

void ibCodeEditor::TriggerSigmaCompletion()
{
	// Dismiss any pending suggestion first — a fresh trigger replaces.
	DismissSigmaCompletion();

	auto* pm = appData ? appData->GetPluginManager() : nullptr;
	// HasAIProviderFor("chat") instead of "helper": the legacy-LLM-shim
	// registers under chat+agent modes (no separate "helper" mode in
	// v3 plugins), and aiBridge synthesises the same set. Until a real
	// completion-only provider mode lands, "chat" is the reliable gate.
	if (pm == nullptr ||
	    (!pm->HasAIProviderFor("chat") && !pm->HasAIProviderFor("helper"))) {
		const int line = LineFromPosition(GetCurrentPos());
		AnnotationSetText(line,
		    wxT("AI Assistant: provider not configured. Tools → Plugins → enable aiBridge."));
		AnnotationSetStyle(line, wxSTC_STYLE_INDENTGUIDE);
		AnnotationSetVisible(wxSTC_ANNOTATION_BOXED);
		return;
	}

	const int caretLine = LineFromPosition(GetCurrentPos());

	// Build prompt with surrounding context. Cursor's autocomplete UX
	// shows that ~50 preceding lines + the active comment / partial line
	// gives the model enough context to produce a useful continuation
	// without burning tokens on the whole module. The instruction line
	// at the top tells the LLM to output ONLY code, no prose, no
	// markdown fences, matching the indent of the line where the
	// completion will land.
	const int contextStart = std::max(0, caretLine - 50);
	wxString context;
	for (int i = contextStart; i <= caretLine; ++i) {
		context += GetLine(i);
	}

	wxString promptText;
	promptText += wxT("You are an OES Designer inline code completion engine.\n");
	promptText += wxT("Language: CES (Open Enterprise Solutions, C-flavoured ");
	promptText += wxT("variant of 1C BSL — keywords are English: Procedure, ");
	promptText += wxT("Function, If, For, While, Var, New, etc.).\n");
	promptText += wxT("Task: continue the code below directly after the cursor. ");
	promptText += wxT("If the last line is a comment describing what to do, ");
	promptText += wxT("write the matching implementation. Output ONLY raw code ");
	promptText += wxT("with proper indentation. Do not include markdown fences, ");
	promptText += wxT("explanations, or repeat the existing lines.\n\n");
	promptText += wxT("=== existing code ===\n");
	promptText += context;
	promptText += wxT("\n=== continue ===\n");

	// Indicator annotation while we wait so the user knows the request
	// fired (Cursor's "loading" pulse equivalent). Replaced when the
	// callback fires.
	AnnotationSetText(caretLine, wxT("…"));
	AnnotationSetStyle(caretLine, wxSTC_STYLE_INDENTGUIDE);
	AnnotationSetVisible(wxSTC_ANNOTATION_BOXED);

	// Status bar phase ping — designer's bottom bar shows AI activity so
	// the user always knows whether the platform is talking to a remote
	// model. wxLogStatus routes to the active top-level wxFrame (Designer
	// main frame). Workmate parity for the visible spinner.
	wxLogStatus(_("AI: запрос отправлен…"));

	const long pendingLineSnapshot = caretLine;
	const long pendingPosSnapshot  = GetCurrentPos();

	// Lambda captures `this` — codeEditor must outlive the inflight
	// request. wxStyledTextCtrl widgets are owned by the document view;
	// closing the tab destroys this object. The CompleteCodeAsync
	// callback runs on the UI thread via wxApp::CallAfter, but the
	// editor may be gone by then. Use a generation token to ignore
	// stale callbacks.
	++m_sigmaCompletionGeneration;
	const unsigned gen = m_sigmaCompletionGeneration;

	pm->CompleteCodeAsync(promptText, wxT("uk-UA"),
	    [this, gen, pendingLineSnapshot, pendingPosSnapshot]
	    (bool ok, const wxString& text, const wxString& err) {
		if (gen != m_sigmaCompletionGeneration) return;  // stale — superseded
		if (!ok || text.IsEmpty()) {
			AnnotationSetText(pendingLineSnapshot,
			    err.IsEmpty() ? _("AI: empty completion") : err);
			AnnotationSetStyle(pendingLineSnapshot, wxSTC_STYLE_INDENTGUIDE);
			AnnotationSetVisible(wxSTC_ANNOTATION_BOXED);
			wxLogStatus(_("AI: ошибка"));
			return;
		}
		wxString cleaned = text;
		// Strip stray markdown fences if the model couldn't help itself.
		cleaned.Replace(wxT("```ces"), wxString(), false);
		cleaned.Replace(wxT("```bsl"), wxString(), false);
		cleaned.Replace(wxT("```"),    wxString(), false);
		cleaned.Trim(/*fromRight=*/false);
		cleaned.Trim(/*fromRight=*/true);
		// Move caret back to the original position so the accepted text
		// lands in the right spot even if the user touched the buffer
		// during the request.
		if (pendingPosSnapshot >= 0 && pendingPosSnapshot <= GetLastPosition()) {
			SetEmptySelection(pendingPosSnapshot);
		}
		ShowSigmaCompletion(cleaned);
		wxLogStatus(_("AI: готово"));
	});
}

void ibCodeEditor::ShowSigmaCompletion(const wxString& text)
{
	const int line = LineFromPosition(GetCurrentPos());
	m_sigmaPending     = text;
	m_sigmaPendingLine = line;
	AnnotationSetText(line, text);
	AnnotationSetStyle(line, wxSTC_STYLE_INDENTGUIDE);
	AnnotationSetVisible(wxSTC_ANNOTATION_BOXED);
}

void ibCodeEditor::AcceptSigmaCompletion()
{
	if (m_sigmaPending.IsEmpty()) return;
	// Strip the keybinding hint footer lines from the inserted body —
	// callers pass user-facing text that includes "Tab/Esc" instructions;
	// only the code portion (line(s) NOT prefixed with "//") goes into
	// the buffer. For Phase 6.2.a the stub embeds both; real Phase 6.2.b
	// suggestions will be code-only and bypass this filter.
	wxString insertText;
	wxStringTokenizer tok(m_sigmaPending, wxT("\n"));
	while (tok.HasMoreTokens()) {
		const wxString l = tok.GetNextToken();
		wxString trimmed = l;
		trimmed.Trim(false);
		if (trimmed.StartsWith(wxT("// AI suggestion")) ||
		    trimmed.StartsWith(wxT("// Tab"))) {
			continue;
		}
		if (!insertText.IsEmpty()) insertText += wxT("\n");
		insertText += l;
	}
	if (insertText.IsEmpty()) insertText = m_sigmaPending;

	const int pos = GetCurrentPos();
	InsertText(pos, insertText);
	SetEmptySelection(pos + insertText.length());
	DismissSigmaCompletion();
}

void ibCodeEditor::DismissSigmaCompletion()
{
	if (m_sigmaPendingLine >= 0) {
		AnnotationSetText(m_sigmaPendingLine, wxEmptyString);
	}
	m_sigmaPending.Clear();
	m_sigmaPendingLine = -1;
}

// ===========================================================================
// Workmate-style auto-trigger — Phase 6.2.c
// ===========================================================================

bool ibCodeEditor::CursorIsCommentLine() const
{
	const int line = LineFromPosition(GetCurrentPos());
	if (line < 0) return false;

	// wxStyledTextCtrl::GetLine is non-const in wx 3.x (it returns by value
	// but isn't marked const). Cast away constness — the read is logically
	// const and there's no side-effecting state mutation in STC's GetLine.
	auto* self = const_cast<ibCodeEditor*>(this);
	wxString current = self->GetLine(line);
	current.Trim(true);   // trailing CR / LF / WS
	current.Trim(false);  // leading WS

	if (current.StartsWith(wxT("//"))) return true;

	// Workmate parity: user writes `// describe what` + Enter ⇒ cursor
	// lands on a blank line directly below the comment. We want the
	// trigger to fire there, not only while the user is still on the
	// comment line itself.
	if (current.IsEmpty() && line > 0) {
		wxString prior = self->GetLine(line - 1);
		prior.Trim(true);
		prior.Trim(false);
		if (prior.StartsWith(wxT("//"))) return true;
	}
	return false;
}

void ibCodeEditor::ArmSigmaIdleTimer()
{
	// Mode gate. 0 (off) and 1 (manual-only — hotkey path) skip the
	// debounce entirely so we don't spin a wxTimer the user can never
	// trip. Auto modes set per-mode delay matching Workmate / Cursor
	// defaults: moderate = 700ms post-pause, intensive = 350ms.
	if (m_sigmaIdleTimer.IsRunning()) m_sigmaIdleTimer.Stop();
	if (m_sigmaAutoMode == 0 || m_sigmaAutoMode == 1) return;
	const int delayMs = (m_sigmaAutoMode == 3) ? 350 : 700;
	m_sigmaIdleTimer.StartOnce(delayMs);
}

void ibCodeEditor::OnSigmaIdleTimer(wxTimerEvent& /*event*/)
{
	// Comment-line gate: blind auto-trigger only on lines that look like
	// task descriptions. Anything else costs the user tokens for no UX
	// win. m_sigmaPending guard prevents stacking a second request on
	// top of an in-flight one — the in-flight callback will resolve and
	// the user can re-arm on the next keystroke.
	if (HasPendingSigmaCompletion()) return;
	if (!CursorIsCommentLine())      return;
	TriggerSigmaCompletion();
}
