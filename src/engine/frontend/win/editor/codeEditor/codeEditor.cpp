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
	// shows that provider's displayName (e.g. "Pugi", "Copilot",
	// "Claude") — host stays vendor-neutral. With nothing installed
	// it falls back to a generic "AI Assistant (no plugin installed)"
	// and all items grey out.
	{
		wxMenu* aiSub = new wxMenu();
		auto* miExplain = aiSub->Append(wxID_HIGHEST + 4030,
		                                  _("Explain code\tAlt+I,E"));
		auto* miReview  = aiSub->Append(wxID_HIGHEST + 4031,
		                                  _("Review code\tAlt+I,R"));
		auto* miFix     = aiSub->Append(wxID_HIGHEST + 4032,
		                                  _("Fix code\tAlt+I,C"));
		auto* miDoc     = aiSub->Append(wxID_HIGHEST + 4033,
		                                  _("Generate doc comment\tAlt+I,G"));
		auto* miSend    = aiSub->Append(wxID_HIGHEST + 4034,
		                                  _("Send selection to chat\tAlt+I,S"));

		auto* pm = appData ? appData->GetPluginManager() : nullptr;
		const bool hasAI = pm && pm->HasAIProviderFor("chat");
		const bool hasSelection = GetSelectionStart() != GetSelectionEnd();
		miExplain->Enable(hasAI && hasSelection);
		miReview ->Enable(hasAI && hasSelection);
		miFix    ->Enable(hasAI && hasSelection);
		miSend   ->Enable(hasAI && hasSelection);
		miDoc    ->Enable(hasAI);

		// Always "AI Assistant" — host-neutral; specific provider name
		// is intentionally not exposed in the submenu label, matching
		// the way Cursor / Copilot / Cody never put the LLM vendor in
		// menu UI.
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
			const wxString sel = GetSelectedText();
			// Doc-gen falls back to the current line when nothing is
			// selected — matches the "Generate doc comment for cursor
			// procedure" UX in modern AI IDE assistants.
			wxString code = sel;
			if (code.IsEmpty() && op == wxT("doc")) {
				const int line = LineFromPosition(GetCurrentPos());
				code = GetLine(line);
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
			wxString json = wxT("{\"kind\":\"editor.skill\",\"op\":\"");
			json += op;
			json += wxT("\",\"language\":\"ces\",\"code\":\"");
			json += esc(code);
			json += wxT("\"}");

			// Pick the first available pane; if none, the AI Chat fallback
			// pane registered by the Tools menu still works because the
			// menu opens the demo bundle and CallWebPaneSend lands there.
			// We delegate to the main frame's "open + show" path by firing
			// the AI Chat command; the handler creates the pane on demand.
			wxCommandEvent openEvt(wxEVT_MENU, wxID_FRONTEND_PLUGIN_WEB_PANE);
			openEvt.SetEventObject(this);
			for (wxWindow* p = GetParent(); p != nullptr; p = p->GetParent()) {
				if (p->ProcessWindowEvent(openEvt)) break;
			}
			// Send the skill envelope to whichever pane is now the default.
			// The plugin manager keeps the default-pane id alive across
			// the whole session.
			pm->CallWebPaneSend(wxT("designer.demo.chat"), json);
			for (const auto& prov : pm->AIProviders()) {
				(void)prov;
				// Phase 6.1 routes via WebPaneSend only; future Phase 6.2
				// adds direct provider.Query routing for /slash-commands.
				break;
			}
		};
	};
	Bind(wxEVT_MENU, sendSkill(wxT("explain")), wxID_HIGHEST + 4030);
	Bind(wxEVT_MENU, sendSkill(wxT("review")),  wxID_HIGHEST + 4031);
	Bind(wxEVT_MENU, sendSkill(wxT("fix")),     wxID_HIGHEST + 4032);
	Bind(wxEVT_MENU, sendSkill(wxT("doc")),     wxID_HIGHEST + 4033);
	Bind(wxEVT_MENU, sendSkill(wxT("send")),    wxID_HIGHEST + 4034);

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
	//   Tab inside pending suggestion  — accept + insert
	//   Esc inside pending suggestion  — dismiss
	// Tab/Esc only intercepted when a suggestion is pending; otherwise
	// they fall through to default Scintilla behaviour.
	if ((event.RawControlDown() || event.ControlDown()) &&
	    !event.ShiftDown() && !event.AltDown() &&
	    event.GetKeyCode() == 'I') {
		TriggerSigmaCompletion();
		return;
	}
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
}

// ===========================================================================
// Sigma AI inline completion (Phase 6.2)
// ===========================================================================

void ibCodeEditor::TriggerSigmaCompletion()
{
	// Dismiss any pending suggestion first — a fresh trigger replaces.
	DismissSigmaCompletion();

	auto* pm = appData ? appData->GetPluginManager() : nullptr;
	if (pm == nullptr || !pm->HasAIProviderFor("helper")) {
		AnnotationSetText(LineFromPosition(GetCurrentPos()),
		    wxT("Sigma: no AI provider with \"helper\" mode installed."));
		AnnotationSetStyle(LineFromPosition(GetCurrentPos()), wxSTC_STYLE_INDENTGUIDE);
		AnnotationSetVisible(wxSTC_ANNOTATION_BOXED);
		// Auto-clear after a few seconds via a timer would be nicer; for
		// Phase 6.2.a the message stays until the next ContextMenu /
		// TriggerSigmaCompletion call.
		return;
	}

	// Build editor.complete envelope. Real provider dispatch lands in
	// Phase 6.2.b once Host_EditorCompletionRespond ABI is added — for
	// now we show a stub annotation so the UI affordance is testable
	// without a v4 plugin that supports inline completion.
	const int line = LineFromPosition(GetCurrentPos());
	const wxString prefix = GetLine(line);
	wxLogDebug(wxT("[sigma] TriggerSigmaCompletion line=%d prefix=%s"),
	           line, prefix);

	// Stub completion — replace with real round-trip in Phase 6.2.b.
	ShowSigmaCompletion(wxT("// AI suggestion: implement TODO above\n"
	                          "// Tab to accept   Esc to dismiss"));
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
