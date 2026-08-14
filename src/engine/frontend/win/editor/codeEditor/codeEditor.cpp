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

#include <wx/artprov.h>

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
	bool prevWasDot = false;   // last significant token was a member-access '.' (kept across whitespace)

	while (!m_tc.IsEnd()) {
#ifdef UTF8_LEXEM_TRANSLATE
		currPos = fromPos + m_tc.GetCurrentUtf8Pos();
#else 
		currPos = fromPos + m_tc.GetCurrentPos();
#endif 
		if (m_tc.IsWord()) {
			(void)m_tc.GetWord(word, false, true);
			const short keyWord = ibTranslateCode::IsKeyWord(word);
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

	// SCAN FROM THE TOP OF THE DOCUMENT, tracking whether we are inside a string. A literal cannot
	// be recognised by looking around the caret: `""` inside a string is an escaped quote, not a
	// close followed by an open, and only a scan that started outside can tell the two apart.
	const wxString text = GetText();
	const int caret = GetCurrentPos();

	int start = -1;
	for (int i = 0; i < static_cast<int>(text.length()); ++i) {
		if (text[i] != wxT('"'))
			continue;

		if (start < 0) {
			start = i;                       // opening quote
			continue;
		}
		if (i + 1 < static_cast<int>(text.length()) && text[i + 1] == wxT('"')) {
			++i;                             // "" — an escaped quote, still inside
			continue;
		}

		// A closing quote: this literal spans [start, i]. The caret counts as inside when it sits
		// anywhere from the opening quote to just past the closing one, so a click at either edge
		// finds the string a person is plainly pointing at.
		if (caret >= start && caret <= i + 1) {
			span.m_start = start;
			span.m_end   = i + 1;
			break;
		}
		start = -1;
	}

	if (!span.Found())
		return span;

	// Take the VALUE out of the spelling: drop the quotes, unescape "", and strip the continuation
	// markers so what comes out is the query language and not the script's rendering of it.
	for (int i = span.m_start + 1; i < span.m_end - 1; ++i) {
		if (text[i] == wxT('"') && i + 1 < span.m_end - 1 && text[i + 1] == wxT('"')) {
			span.m_text += wxT('"');
			++i;
			continue;
		}
		if (text[i] == wxT('\n')) {
			span.m_text += wxT('\n');
			// Everything up to and including the next `|` is the continuation marker and its indent.
			int j = i + 1;
			while (j < span.m_end - 1 && (text[j] == wxT(' ') || text[j] == wxT('\t') || text[j] == wxT('\r')))
				++j;
			if (j < span.m_end - 1 && text[j] == wxT('|'))
				i = j;
			continue;
		}
		span.m_text += text[i];
	}
	return span;
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
	const int line = LineFromPosition(span.m_start);
	const int lineStart = PositionFromLine(line);
	wxString indent;
	for (int i = lineStart; i < span.m_start; ++i)
		indent += (GetCharAt(i) == wxT('\t')) ? wxT('\t') : wxT(' ');

	SetTargetStart(span.m_start);
	SetTargetEnd(span.m_end);
	ReplaceTarget(SpellStringLiteral(text, indent));
}

void ibCodeEditor::InsertStringLiteral(int position, const wxString& text)
{
	if (position < 0)
		position = GetCurrentPos();

	// Indented to WHERE THE CARET IS, so a query written into the middle of a procedure lines up
	// with the code around it instead of starting at column zero.
	const int line = LineFromPosition(position);
	const int lineStart = PositionFromLine(line);
	wxString indent;
	for (int i = lineStart; i < position; ++i)
		indent += (GetCharAt(i) == wxT('\t')) ? wxT('\t') : wxT(' ');

	InsertText(position, SpellStringLiteral(text, indent));
}

#include "frontend/mainFrame/mainFrame.h"  // wxID_FRONTEND_SYNTAX_HELPER_LOOKUP
#include "frontend/win/dlgs/queryConstructor/queryConstructor.h"   // the constructor, opened on the literal
#include "frontend/artProvider/artProvider.h"                      // wxART_QUERY_CONSTRUCTOR — the icon, registered not embedded
#include "backend/metadataConfiguration.h"                         // activeMetaData — the config this module belongs to

void ibCodeEditor::OnContextMenu(wxContextMenuEvent& event)
{
	wxMenu menu;

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
	menu.Bind(wxEVT_MENU, [this, literal, caret](wxCommandEvent&) {
		wxString text = literal.m_text;
		if (!ibShowQueryConstructor(this, text, activeMetaData, !IsEditable()))
			return;
		if (literal.Found()) ReplaceStringLiteral(literal, text);
		else                 InsertStringLiteral(caret, text);
	}, miConstruct->GetId());

	menu.AppendSeparator();

	// Standard clipboard primitives.
	auto* miCut       = menu.Append(wxID_CUT,       _("Cut")        + wxT("\tCtrl+X"));
	auto* miCopy      = menu.Append(wxID_COPY,      _("Copy")       + wxT("\tCtrl+C"));
	auto* miPaste     = menu.Append(wxID_PASTE,     _("Paste")      + wxT("\tCtrl+V"));
	auto* miSelectAll = menu.Append(wxID_SELECTALL, _("Select all") + wxT("\tCtrl+A"));

	miCut  ->SetBitmap(wxArtProvider::GetBitmap(wxART_CUT,   wxART_MENU));
	miCopy ->SetBitmap(wxArtProvider::GetBitmap(wxART_COPY,  wxART_MENU));
	miPaste->SetBitmap(wxArtProvider::GetBitmap(wxART_PASTE, wxART_MENU));
	// Select All — no canonical wxArt id; left unset so the row aligns
	// with the icon column without a placeholder.
	(void)miSelectAll;

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
