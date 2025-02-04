////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : autoComplete window 
////////////////////////////////////////////////////////////////////////////

#include "codeEditor.h"
#include "frontend/mainFrame/mainFrame.h"
#include "backend/debugger/debugClient.h"
#include "backend/moduleManager/moduleManager.h"
#include "backend/metaData.h"
#include "frontend/docView/docView.h" 
#include "res/bitmaps_res.h"

#define DEF_LINENUMBER_ID 0
#define DEF_BREAKPOINT_ID 1
#define DEF_FOLDING_ID 2

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CCodeEditor::CCodeEditor()
	: wxStyledTextCtrl(), m_document(nullptr), ac(this), ct(this), m_precompileModule(nullptr), m_bInitialized(false) {
}

CCodeEditor::CCodeEditor(CMetaDocument* document, wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style, const wxString& name)
	: wxStyledTextCtrl(parent, id, pos, size, style, name), m_document(document), ac(this), ct(this), m_precompileModule(nullptr), m_bInitialized(false)
{
	// initialize styles
	StyleClearAll();

	//set Lexer to LEX_CONTAINER: This will trigger the styleneeded event so you can do your own highlighting
	SetLexer(wxSTC_LEX_CONTAINER);

	//Set margin cursor
	for (int margin = 0; margin < GetMarginCount(); margin++)
		SetMarginCursor(margin, wxSTC_CURSORARROW);

	//register event
	Connect(wxEVT_STC_MARGINCLICK, wxStyledTextEventHandler(CCodeEditor::OnMarginClick), nullptr, this);
	Connect(wxEVT_STC_STYLENEEDED, wxStyledTextEventHandler(CCodeEditor::OnStyleNeeded), nullptr, this);
	Connect(wxEVT_STC_MODIFIED, wxStyledTextEventHandler(CCodeEditor::OnTextChange), nullptr, this);

	Connect(wxEVT_KEY_DOWN, wxKeyEventHandler(CCodeEditor::OnKeyDown), nullptr, this);
	Connect(wxEVT_MOTION, wxMouseEventHandler(CCodeEditor::OnMouseMove), nullptr, this);

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

	// Setup the dwell time before a tooltip is displayed.
	SetMouseDwellTime(200);

	// Setup caret line
	SetCaretLineVisible(true);

	// miscellaneous
	SetLayoutCache(wxSTC_CACHE_PAGE);

	//Turn the fold markers red when the caret is a line in the group (optional)
	MarkerEnableHighlight(true);
}

CCodeEditor::~CCodeEditor()
{
	wxDELETE(m_precompileModule);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CCodeEditor::EditDebugPoint(int line_to_edit)
{
	//Обновляем список точек останова
	const int dwFlags = MarkerGet(line_to_edit);
	if ((dwFlags & (1 << CCodeEditor::Breakpoint))) {
		debugClient->RemoveBreakpoint(m_document->GetFilename(), line_to_edit);
	}
	else {
		debugClient->ToggleBreakpoint(m_document->GetFilename(), line_to_edit);
	}
}

void CCodeEditor::RefreshBreakpoint(bool deleteCurrentBreakline)
{
	MarkerDeleteAll(CCodeEditor::Breakpoint);
	//Обновляем список точек останова
	for (auto& line_to_edit : debugClient->GetDebugList(m_document->GetFilename())) {
		const int dwFlags = MarkerGet(line_to_edit);
		if (!(dwFlags & (1 << CCodeEditor::Breakpoint))) {
			MarkerAdd(line_to_edit, CCodeEditor::Breakpoint);
		}
	}
}

void CCodeEditor::SetCurrentLine(int lineBreakpoint, bool setBreakLine)
{
	const int firstVisibleLine = GetFirstVisibleLine(),
		linesOnScreen = LinesOnScreen();

	if (!CCodeEditor::GetSTCFocus())
		CCodeEditor::SetSTCFocus(true);

	MarkerDeleteAll(CCodeEditor::BreakLine);

	if (setBreakLine) MarkerAdd(lineBreakpoint - 1, CCodeEditor::BreakLine);

	if (firstVisibleLine > (lineBreakpoint - 1))
		ScrollToLine(lineBreakpoint - 1);
	else if (firstVisibleLine + linesOnScreen < (lineBreakpoint - 1))
		ScrollToLine(lineBreakpoint - 1);

	if (!setBreakLine) GotoLine(lineBreakpoint - 1);
}

void CCodeEditor::SetEditorSettings(const EditorSettings& settings)
{
	m_bIndentationSize = settings.GetIndentSize();

	SetIndent(m_bIndentationSize);
	SetTabWidth(m_bIndentationSize);

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
		// the most number of linqes we'd reasonbly have.
		int marginSize = TextWidth(wxSTC_STYLE_LINENUMBER, "_999999");
		SetMarginWidth(DEF_LINENUMBER_ID, marginSize);
	}

	// set margin as unused
	SetMarginType(DEF_BREAKPOINT_ID, wxSTC_MARGIN_SYMBOL);
	SetMarginMask(DEF_BREAKPOINT_ID, ~(1024 | 256 | 512 | 128 | 64 | wxSTC_MASK_FOLDERS));
	StyleSetBackground(DEF_BREAKPOINT_ID, *wxWHITE);

	SetMarginWidth(DEF_BREAKPOINT_ID, 0);
	SetMarginSensitive(DEF_BREAKPOINT_ID, false);

	if (true) {
		int foldingMargin = FromDIP(16);
		SetMarginWidth(DEF_BREAKPOINT_ID, foldingMargin);
		SetMarginSensitive(DEF_BREAKPOINT_ID, true);
	}

	// folding
	SetMarginType(DEF_FOLDING_ID, wxSTC_MARGIN_SYMBOL);
	SetMarginMask(DEF_FOLDING_ID, wxSTC_MASK_FOLDERS);

	SetMarginWidth(DEF_FOLDING_ID, 0);
	SetMarginSensitive(DEF_FOLDING_ID, false);

	if (true) {
		int foldingMargin = FromDIP(16);
		SetMarginWidth(DEF_FOLDING_ID, foldingMargin);
		SetMarginSensitive(DEF_FOLDING_ID, true);
	}

	m_bEnableAutoComplete = settings.GetEnableAutoComplete();
}

inline wxColour GetInverse(const wxColour& color)
{
	unsigned char r = color.Red();
	unsigned char g = color.Green();
	unsigned char b = color.Blue();

	return wxColour(r ^ 0xFF, g ^ 0xFF, b ^ 0xFF);
}

void CCodeEditor::SetFontColorSettings(const FontColorSettings& settings)
{
	// For some reason StyleSetFont takes a (non-const) reference, so we need to make
	// a copy before passing it in.
	wxFont font = settings.GetFont();

	StyleClearAll();
	StyleSetFont(wxSTC_STYLE_DEFAULT, font);

	SetSelForeground(true, settings.GetColors(FontColorSettings::DisplayItem_Selection).foreColor);
	SetSelBackground(true, settings.GetColors(FontColorSettings::DisplayItem_Selection).backColor);

	font = settings.GetFont(FontColorSettings::DisplayItem_Default);

	StyleSetFont(wxSTC_C_DEFAULT, font);
	StyleSetFont(wxSTC_C_IDENTIFIER, font);

	StyleSetForeground(wxSTC_C_DEFAULT, settings.GetColors(FontColorSettings::DisplayItem_Default).foreColor);
	StyleSetBackground(wxSTC_C_DEFAULT, settings.GetColors(FontColorSettings::DisplayItem_Default).backColor);

	StyleSetForeground(wxSTC_STYLE_DEFAULT, settings.GetColors(FontColorSettings::DisplayItem_Default).foreColor);
	StyleSetBackground(wxSTC_STYLE_DEFAULT, settings.GetColors(FontColorSettings::DisplayItem_Default).backColor);

	StyleSetForeground(wxSTC_C_IDENTIFIER, settings.GetColors(FontColorSettings::DisplayItem_Default).foreColor);
	StyleSetBackground(wxSTC_C_IDENTIFIER, settings.GetColors(FontColorSettings::DisplayItem_Default).backColor);

	font = settings.GetFont(FontColorSettings::DisplayItem_Comment);

	StyleSetFont(wxSTC_C_COMMENT, font);
	StyleSetFont(wxSTC_C_COMMENTLINE, font);
	StyleSetFont(wxSTC_C_COMMENTDOC, font);

	StyleSetForeground(wxSTC_C_COMMENT, settings.GetColors(FontColorSettings::DisplayItem_Comment).foreColor);
	StyleSetBackground(wxSTC_C_COMMENT, settings.GetColors(FontColorSettings::DisplayItem_Comment).backColor);

	StyleSetForeground(wxSTC_C_COMMENTLINE, settings.GetColors(FontColorSettings::DisplayItem_Comment).foreColor);
	StyleSetBackground(wxSTC_C_COMMENTLINE, settings.GetColors(FontColorSettings::DisplayItem_Comment).backColor);

	StyleSetForeground(wxSTC_C_COMMENTDOC, settings.GetColors(FontColorSettings::DisplayItem_Comment).foreColor);
	StyleSetBackground(wxSTC_C_COMMENTDOC, settings.GetColors(FontColorSettings::DisplayItem_Comment).backColor);

	font = settings.GetFont(FontColorSettings::DisplayItem_Keyword);

	StyleSetFont(wxSTC_C_WORD, font);
	StyleSetForeground(wxSTC_C_WORD, settings.GetColors(FontColorSettings::DisplayItem_Keyword).foreColor);
	StyleSetBackground(wxSTC_C_WORD, settings.GetColors(FontColorSettings::DisplayItem_Keyword).backColor);

	font = settings.GetFont(FontColorSettings::DisplayItem_Operator);
	StyleSetFont(wxSTC_C_OPERATOR, font);
	StyleSetForeground(wxSTC_C_OPERATOR, settings.GetColors(FontColorSettings::DisplayItem_Operator).foreColor);
	StyleSetBackground(wxSTC_C_OPERATOR, settings.GetColors(FontColorSettings::DisplayItem_Operator).backColor);

	font = settings.GetFont(FontColorSettings::DisplayItem_String);

	StyleSetFont(wxSTC_C_STRING, font);
	StyleSetForeground(wxSTC_C_STRING, settings.GetColors(FontColorSettings::DisplayItem_String).foreColor);
	StyleSetBackground(wxSTC_C_STRING, settings.GetColors(FontColorSettings::DisplayItem_String).backColor);

	StyleSetFont(wxSTC_C_STRINGEOL, font);
	StyleSetForeground(wxSTC_C_STRINGEOL, settings.GetColors(FontColorSettings::DisplayItem_String).foreColor);
	StyleSetBackground(wxSTC_C_STRINGEOL, settings.GetColors(FontColorSettings::DisplayItem_String).backColor);

	StyleSetFont(wxSTC_C_CHARACTER, font);
	StyleSetForeground(wxSTC_C_CHARACTER, settings.GetColors(FontColorSettings::DisplayItem_String).foreColor);
	StyleSetBackground(wxSTC_C_CHARACTER, settings.GetColors(FontColorSettings::DisplayItem_String).backColor);

	StyleSetFont(wxSTC_C_CHARACTER, font);
	StyleSetForeground(wxSTC_C_CHARACTER, settings.GetColors(FontColorSettings::DisplayItem_Selection).foreColor);
	StyleSetBackground(wxSTC_C_CHARACTER, settings.GetColors(FontColorSettings::DisplayItem_Selection).backColor);

	font = settings.GetFont(FontColorSettings::DisplayItem_Number);

	StyleSetFont(wxSTC_C_NUMBER, font);
	StyleSetForeground(wxSTC_C_NUMBER, settings.GetColors(FontColorSettings::DisplayItem_Number).foreColor);
	StyleSetBackground(wxSTC_C_NUMBER, settings.GetColors(FontColorSettings::DisplayItem_Number).backColor);

	StyleSetSize(wxSTC_STYLE_LINENUMBER, font.GetPointSize());

	// Set the caret color as the inverse of the background color so it's always visible.
	SetCaretForeground(GetInverse(settings.GetColors(FontColorSettings::DisplayItem_Default).backColor));
}

void CCodeEditor::AppendText(const wxString& text)
{
	int lastLine = wxStyledTextCtrl::GetLineCount() - 1;

	m_bInitialized = false;
	wxStyledTextCtrl::AppendText(text);
	m_bInitialized = true;

	CMetaObjectModule* moduleObject = m_document->ConvertMetaObjectToType<CMetaObjectModule>();

	if (moduleObject != nullptr) {

		try {
			m_precompileModule->Load(GetText());
			m_precompileModule->PrepareLexem();
		}
		catch (...) {
		}

		SaveModule();

		debugClient->PatchBreakpoints(moduleObject->GetDocPath(),
			lastLine, wxStyledTextCtrl::GetLineCount() - lastLine - 1
		);

		m_document->Modify(true);
	}
}

void CCodeEditor::Replace(long from, long to, const wxString& text)
{
	int lineStart = wxStyledTextCtrl::LineFromPosition(from);
	int lineEnd = wxStyledTextCtrl::LineFromPosition(to);

	int patchLine = 0;

	const std::string strBuffer = text.utf8_str();

	for (auto c : strBuffer) {
		if (c == '\n') {
			patchLine++;
		}
	}

	m_bInitialized = false;
	wxStyledTextCtrl::Replace(from, to, text);
	m_bInitialized = true;

	CMetaObjectModule* moduleObject = m_document->ConvertMetaObjectToType<CMetaObjectModule>();

	if (moduleObject != nullptr) {
		try {
			m_precompileModule->Load(GetText());
			m_precompileModule->PrepareLexem();
		}
		catch (...) {
		}
		SaveModule();
		if (moduleObject != nullptr) {
			if (patchLine > (lineEnd - lineStart)) {
				debugClient->PatchBreakpoints(moduleObject->GetDocPath(),
					lineStart, patchLine
				);
			}
		}
		m_document->Modify(true);
	}
}

bool CCodeEditor::LoadModule()
{
	ClearAll();
	wxDELETE(m_precompileModule);

	RefreshEditor();

	if (m_document != nullptr) {
		CMetaObjectModule* moduleObject = m_document->ConvertMetaObjectToType<CMetaObjectModule>();
		if (moduleObject != nullptr) {
			m_precompileModule = new CPrecompileModule(moduleObject);

			if (IsEditable()) {
				SetText(moduleObject->GetModuleText()); m_bInitialized = true;
			}
			else {
				SetReadOnly(false);
				SetText(moduleObject->GetModuleText()); m_bInitialized = true;
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
		return moduleObject != nullptr;
	}
	return m_document != nullptr;
}

bool CCodeEditor::SaveModule()
{
	if (m_document != nullptr) {
		CMetaObjectModule* moduleObject = m_document->ConvertMetaObjectToType<CMetaObjectModule>();
		if (moduleObject != nullptr) {
			moduleObject->SetModuleText(GetText());
			return true;
		}
	}

	return m_document != nullptr;
}

int CCodeEditor::GetRealPosition()
{
	const wxString& codeText = GetTextRange(0, GetCurrentPos());
	return codeText.Length();
}

int CCodeEditor::GetRealPositionFromPoint(const wxPoint& pt)
{
	const wxString& codeText = GetTextRange(0, PositionFromPoint(pt));
	return codeText.Length();
}

#include "win/dlg/lineInput/lineInput.h"
#include "win/dlg/functionSearcher/functionSearcher.h"

void CCodeEditor::RefreshEditor()
{
	CCodeEditor::SetEditorSettings(mainFrame->GetEditorSettings());
	CCodeEditor::SetFontColorSettings(mainFrame->GetFontColorSettings());

	CCodeEditor::RefreshBreakpoint();
}

#include <wx/fdrepdlg.h>

void CCodeEditor::FindText(const wxString& findString, int wxflags)
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
		CCodeEditor::SetSelectionStart(GetSelectionEnd());
		CCodeEditor::SearchAnchor();
		result = CCodeEditor::SearchNext(sciflags, findString);
	}
	else {
		CCodeEditor::SetSelectionEnd(GetSelectionStart());
		CCodeEditor::SearchAnchor();
		result = CCodeEditor::SearchPrev(sciflags, findString);
	}
	if (wxSTC_INVALID_POSITION == result) {
		wxMessageBox(wxString::Format(_("\"%s\" not found!"), findString.c_str()),
			_("Not Found!"), wxICON_ERROR, (wxWindow*)this);
	}
	else {
		CCodeEditor::EnsureCaretVisible();
		CCodeEditor::SetSTCFocus(true);
	}
}

void CCodeEditor::ShowGotoLine()
{
	CDialogLineInput *lineInput = new CDialogLineInput(this);
	const int ret = lineInput->ShowModal();
	if (ret != wxNOT_FOUND) {
		CCodeEditor::SetFocus();
		CCodeEditor::GotoLine(ret - 1);
	}
	lineInput->Destroy();
}

void CCodeEditor::ShowMethods()
{
	CFunctionList *funcList = new CFunctionList(m_document, this);
	const int ret = funcList->ShowModal();
	funcList->Destroy();
}

#include "backend/systemManager/systemManager.h"

bool CCodeEditor::SyntaxControl(bool throwMessage) const
{
	IMetaObject* metaObject = m_document->GetMetaObject();
	wxASSERT(metaObject);
	IMetaData* metaData = metaObject->GetMetaData();
	wxASSERT(metaData);
	IModuleManager* moduleManager = metaData->GetModuleManager();
	wxASSERT(moduleManager);

	IModuleInfo* dataRef = nullptr;
	if (moduleManager->FindCompileModule(metaObject, dataRef)) {
		CCompileModule* compileModule = dataRef->GetCompileModule();
		try {
			if (compileModule->Compile()) {
				if (throwMessage)
					CSystemFunction::Message(_("No syntax errors detected!"));
				return true;
			}
			wxASSERT("CCompileCode::Compile return false");
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

#define appendStyle(style) \
CCodeEditor::StartStyling(currPos); \
CCodeEditor::SetStyling(fromPos + translate.GetCurrentPos() - currPos, style);

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                                          Styling                                                                       //
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CCodeEditor::HighlightSyntaxAndCalculateFoldLevel(
	const int fromLine, const int toLine,
	const int fromPos, const int toPos,
	const wxString& strCode)
{
	class CFoldParser {
		wxStyledTextCtrl* m_stc;
	public:
		CFoldParser(wxStyledTextCtrl* stc, const int fromLine, const int toLine) :
			m_stc(stc), m_fromLine(fromLine), m_toLine(toLine), m_foldLevel(0) {
			m_foldLevel = m_stc->GetFoldLevel(fromLine);
		}

		void OpenFold(const int currentLine) {
			m_folding_vector.emplace_back(m_fromLine + currentLine, +1);
		}

		void MarkFold(const int currentLine, const int mask = wxSTC_FOLDLEVELELSE_FLAG) {
			m_mask_folding_vector.emplace_back(m_fromLine + currentLine, mask);
		}

		void CloseFold(const int currentLine) {
			m_folding_vector.emplace_back(m_fromLine + currentLine, -1);
		}

		void PatchFold() {
			int level = m_foldLevel ^ wxSTC_FOLDLEVELBASE_FLAG;
			if ((m_foldLevel & wxSTC_FOLDLEVELHEADER_FLAG) != 0) {
				level = level ^ wxSTC_FOLDLEVELHEADER_FLAG;
			}
			else if ((m_foldLevel & wxSTC_FOLDLEVELELSE_FLAG) != 0) {
				level = level ^ wxSTC_FOLDLEVELELSE_FLAG;
			}
			else if ((m_foldLevel & wxSTC_FOLDLEVELWHITE_FLAG) != 0) {
				level = level ^ wxSTC_FOLDLEVELWHITE_FLAG;
			}
			for (int l = m_fromLine; l <= m_toLine; l++) {
				const short curr_level = GetFoldLevel(l);
				const int curr_mask = wxSTC_FOLDLEVELBASE_FLAG + std::max(level, 0);
				if (curr_level > 0)
					m_stc->SetFoldLevel(l, curr_mask | wxSTC_FOLDLEVELHEADER_FLAG);
				else if (curr_level < 0)
					m_stc->SetFoldLevel(l, curr_mask | wxSTC_FOLDLEVELWHITE_FLAG);
				else
					m_stc->SetFoldLevel(l, curr_mask | GetFoldMask(l));
				level += curr_level;
			}
		}

	private:
		short GetFoldLevel(const int l) {
			short foldLevel = 0;
			for (auto& v : m_folding_vector) {
				if (v.first == l) {
					foldLevel += v.second;
				}
			}
			return foldLevel;
		}

		short GetFoldMask(const int l) {
			short foldMask = 0;
			for (auto& v : m_mask_folding_vector) {
				if (v.first == l) {
					foldMask |= v.second;
				}
			}
			return foldMask;
		}
	private:
		//build a vector to include line and folding level
		std::vector<std::pair<size_t, int>> m_folding_vector;
		std::vector<std::pair<size_t, int>> m_mask_folding_vector;

		int m_fromLine, m_toLine;
		int m_foldLevel;
	};

	CFoldParser foldParser(this, fromLine, toLine);

	CTranslateCode translate;
	translate.Load(strCode);

	//вдруг строка начинается с комментария:
	int wasLeftPoint = wxNOT_FOUND;

	//remove old styling
	CCodeEditor::StartStyling(fromPos); //from here
	CCodeEditor::SetStyling(toPos - fromPos, wxSTC_C_COMMENT); //with that length and style -> cleared

	wxString strWord;
	unsigned int currPos = fromPos;
	while (!translate.IsEnd()) {
		currPos = fromPos + translate.GetCurrentPos();
		if (translate.IsWord()) {
			try {
				strWord = translate.GetWord();
			}
			catch (...) {
			}
			const short keyWord = CTranslateCode::IsKeyWord(strWord);
			if (keyWord != wxNOT_FOUND && wasLeftPoint != translate.GetCurrentLine()) {

				if (keyWord == KEY_PROCEDURE
					|| keyWord == KEY_FUNCTION
					|| keyWord == KEY_IF
					|| keyWord == KEY_FOR
					|| keyWord == KEY_FOREACH
					|| keyWord == KEY_WHILE
					|| keyWord == KEY_TRY
					) {
					foldParser.OpenFold(translate.GetCurrentLine());
				}
				else if (
					keyWord == KEY_ELSE
					|| keyWord == KEY_ELSEIF
					|| keyWord == KEY_EXCEPT
					)
				{
					foldParser.MarkFold(translate.GetCurrentLine());
				}
				else if (
					keyWord == KEY_ENDPROCEDURE
					|| keyWord == KEY_ENDFUNCTION
					|| keyWord == KEY_ENDIF
					|| keyWord == KEY_ENDDO
					|| keyWord == KEY_ENDTRY
					) {
					foldParser.CloseFold(translate.GetCurrentLine());
				}

				if (strWord.Left(1) == '#') {
					appendStyle(wxSTC_C_PREPROCESSOR);
				}
				else {
					appendStyle(wxSTC_C_STRING);
				}
			}
			else {
				appendStyle(wxSTC_C_WORD);
			}
			wasLeftPoint = wxNOT_FOUND;
		}
		else if (translate.IsNumber() || translate.IsString() || translate.IsDate()) {
			if (translate.IsNumber()) {
				try {
					(void)translate.GetNumber();
				}
				catch (...) {
				}
				appendStyle(wxSTC_C_NUMBER);
			}
			else if (translate.IsString()) {
				try {
					(void)translate.GetString();
				}
				catch (...) {
				}
				appendStyle(wxSTC_C_OPERATOR);
			}
			else if (translate.IsDate()) {
				try {
					(void)translate.GetDate();
				}
				catch (...) {
				}
				appendStyle(wxSTC_C_OPERATOR);
			}
			wasLeftPoint = wxNOT_FOUND;
		}
		else {
			const wxUniChar& c = translate.GetByte();
			appendStyle(wxSTC_C_IDENTIFIER);
			if (c == '.')
				wasLeftPoint = translate.GetCurrentLine();
			else
				wasLeftPoint = wxNOT_FOUND;
		}
	}
	foldParser.PatchFold();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                                          EVENT                                                                         //
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CCodeEditor::OnStyleNeeded(wxStyledTextEvent& event)
{
	/*this is called every time the styler detects a line that needs style, so we style that range.
	This will save a lot of performance since we only style text when needed instead of parsing the whole file every time.*/
	int line_start = CCodeEditor::LineFromPosition(CCodeEditor::GetEndStyled());
	int line_end = CCodeEditor::GetFirstVisibleLine() + CCodeEditor::LinesOnScreen();

	if (line_end > CCodeEditor::GetLineCount()) {
		line_end = CCodeEditor::GetLineCount() - 1;
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
	if (CCodeEditor::GetLineCount() == CCodeEditor::LinesOnScreen()) {
		line_start = 0;
		line_end = CCodeEditor::GetLineCount() - 1;
	}

	if (line_end < line_start) {
		//that happens when you select parts that are in front of the styled area
		size_t temp = line_end;
		line_end = line_start;
		line_start = temp;
	}

	//style the line following the style area too (if present) in case fold level decreases in that one
	if (line_end < CCodeEditor::GetLineCount() - 1) {
		line_end++;
	}

	//get exact start positions
	size_t pos_start = CCodeEditor::PositionFromLine(line_start);
	size_t pos_end = CCodeEditor::GetLineEndPosition(line_end);

	wxString strCode; int space_char = 0;
	strCode.reserve(pos_end - pos_start);

	// get str code 
	const std::string strBuffer(CCodeEditor::GetTextRangeRaw(pos_start, pos_end));
	for (unsigned int i = 0; i < strBuffer.size(); i++) {
		const unsigned char& c = strBuffer[i];
		if (c == ' ' || stringUtils::IsSymbol(c) || !stringUtils::IsWord(c)) {
			(void)strCode.append(space_char, '_'); space_char = 0;
		}
		if ((c & 0x80) == 0) {
			wchar_t wc = c;
			(void)strCode.append(wc);
		}
		else if ((c & 0xE0) == 0xC0) {
			space_char += 1;
			wchar_t wc = (c & 0x1F) << 6;
			wc |= (strBuffer[i + 1] & 0x3F);
			(void)strCode.append(wc);
			i += 1;
		}
		else if ((c & 0xF0) == 0xE0) {
			space_char += 2;
			wchar_t wc = (c & 0xF) << 12;
			wc |= (strBuffer[i + 1] & 0x3F) << 6;
			wc |= (strBuffer[i + 2] & 0x3F);
			(void)strCode.append(wc);
			i += 2;
		}
		else if ((c & 0xF8) == 0xF0) {
			space_char += 3;
			wchar_t wc = (c & 0x7) << 18;
			wc |= (strBuffer[i + 1] & 0x3F) << 12;
			wc |= (strBuffer[i + 2] & 0x3F) << 6;
			wc |= (strBuffer[i + 3] & 0x3F);
			(void)strCode.append(wc);
			i += 3;
		}
		else if ((c & 0xFC) == 0xF8) {
			space_char += 4;
			wchar_t wc = (c & 0x3) << 24;
			wc |= (strBuffer[i + 1] & 0x3F) << 18;
			wc |= (strBuffer[i + 2] & 0x3F) << 12;
			wc |= (strBuffer[i + 3] & 0x3F) << 6;
			wc |= (strBuffer[i + 4] & 0x3F);
			(void)strCode.append(wc);
			i += 4;
		}
		else if ((c & 0xFE) == 0xFC) {
			space_char += 5;
			wchar_t wc = (c & 0x1) << 30;
			wc |= (strBuffer[i + 1] & 0x3F) << 24;
			wc |= (strBuffer[i + 2] & 0x3F) << 18;
			wc |= (strBuffer[i + 3] & 0x3F) << 12;
			wc |= (strBuffer[i + 4] & 0x3F) << 6;
			wc |= (strBuffer[i + 5] & 0x3F);
			(void)strCode.append(wc);
			i += 5;
		}
	}

	(void)strCode.append(space_char, '_'); space_char = 0;

	HighlightSyntaxAndCalculateFoldLevel(
		line_start, line_end,
		pos_start, pos_end, strCode
	);

	event.Skip();
}

void CCodeEditor::OnMarginClick(wxStyledTextEvent& event)
{
	const int line_from_pos = LineFromPosition(event.GetPosition());

	switch (event.GetMargin())
	{
	case DEF_BREAKPOINT_ID: {
		const int dwFlags = CCodeEditor::MarkerGet(line_from_pos);
		if (IsEditable()) {
			//Обновляем список точек останова
			const wxString& strModuleName = m_document->GetFilename();
			if ((dwFlags & (1 << CCodeEditor::Breakpoint))) {
				if (debugClient->RemoveBreakpoint(strModuleName, line_from_pos)) {
					MarkerDelete(line_from_pos, CCodeEditor::Breakpoint);
				}
			}
			else if (debugClient->ToggleBreakpoint(strModuleName, line_from_pos)) {
				MarkerAdd(line_from_pos, CCodeEditor::Breakpoint);
			}
		}
		break;
	}
	case DEF_FOLDING_ID:
		ToggleFold(line_from_pos);
		break;
	}

	event.Skip();
}

void CCodeEditor::OnTextChange(wxStyledTextEvent& event)
{
	int modFlags = event.GetModificationType();

	if ((modFlags & (wxSTC_MOD_BEFOREINSERT)) == 0 &&
		(modFlags & (wxSTC_MOD_BEFOREDELETE)) == 0)
		return;

	if (m_bInitialized) {
		CMetaObjectModule* moduleObject = m_document->ConvertMetaObjectToType<CMetaObjectModule>();
		if (moduleObject != nullptr) {

			IMetaData* metaData = moduleObject->GetMetaData();
			wxASSERT(metaData);

			std::string codeRaw = GetTextRaw();
			unsigned int length = 0; int patchLine = 0; bool hasChanged = false;

			const wxString& insertText = event.GetString();

			if ((modFlags & wxSTC_MOD_BEFOREINSERT) != 0)
			{
				std::string strBuffer = insertText.utf8_str();
				for (auto c : strBuffer)
				{
					if (c != ' ' && c != '\t' && c != '\n' && c != '\r') hasChanged = true;
					if (c == '\n') patchLine++;
				}
			}
			else {
				std::string strBuffer = codeRaw.substr(event.GetPosition(), event.GetLength());
				for (auto c : strBuffer) {
					if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
						hasChanged = true;
					if (c == '\n') patchLine--;
				}
			}

			if (hasChanged) {
				IModuleManager* moduleManager = metaData->GetModuleManager();
				wxASSERT(moduleManager);
				IModuleInfo* pRefData = nullptr;
				if (moduleManager->FindCompileModule(m_document->GetMetaObject(), pRefData)) {
					CCompileCode* compileModule = pRefData->GetCompileModule();
					wxASSERT(compileModule);
					if (!compileModule->m_changeCode)
						compileModule->m_changeCode = true;
				}
			}

			m_document->Modify(true);

			if ((modFlags & wxSTC_MOD_BEFOREINSERT) != 0)
				length = insertText.Length();
			else
				length = wxString::FromUTF8(codeRaw.substr(event.GetPosition(), event.GetLength())).Length();

			if ((modFlags & wxSTC_MOD_BEFOREINSERT) != 0)
				codeRaw.insert(event.GetPosition(), insertText.utf8_str());
			else
				codeRaw.erase(event.GetPosition(), event.GetLength());

			const wxString& codeText = wxString::FromUTF8(codeRaw);

			unsigned int startPos = 0;
			unsigned int endPos = event.GetPosition();

			std::string strBuffer = codeRaw.substr(0, endPos);

			unsigned int currLine = 0; bool needChangePos = false;

			for (unsigned int i = 0; i < strBuffer.size(); i++)
			{
				if (strBuffer[i] == '\n') {
					currLine++; startPos = i - 1;
				}
			}

			if (patchLine != 0) {
				debugClient->PatchBreakpoints(moduleObject->GetDocPath(), needChangePos ? currLine + 1 : currLine, patchLine);
			}

#ifndef _USE_OLD_TEXT_PARSER_IN_CODE_EDITOR 	
			m_precompileModule->Load(codeText);
			m_precompileModule->m_nCurLine = currLine > 0 ? currLine - 1 : 0;
			m_precompileModule->m_nCurPos = wxString::FromUTF8(strBuffer.substr(0, startPos)).Length();
			try {
				m_precompileModule->PatchLexem(currLine, patchLine, length, modFlags);
			}
			catch (...) {
			}
#else 
			m_precompileModule->Load(wxString::FromUTF8(codeRaw));

			try {
				m_precompileModule->PrepareLexem();
			}
			catch (...) {
			}
#endif
			moduleObject->SetModuleText(codeText);
		}
	}
}

void CCodeEditor::OnKeyDown(wxKeyEvent& event)
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
	case WXK_UP:
	{
		int currentPos = GetCurrentPos();
		int line = LineFromPosition(currentPos);

		int startPos = PositionFromLine(line);
		int endPos = GetLineEndPosition(line);

		int length = currentPos - startPos;

		int startNewPos = PositionFromLine(line - 1);
		int endNewPos = GetLineEndPosition(line - 1);

		if (endNewPos - startNewPos < length) {
			std::string m_stringBuffer;
			for (int c = 0; c < (length - (endNewPos - startNewPos)); c++) {
				m_stringBuffer.push_back(' ');
			}

			InsertText(endNewPos, m_stringBuffer);
		}

		SetEmptySelection(startNewPos + length); break;
	}
	case WXK_DOWN:
	{
		int currentPos = GetCurrentPos();
		int line = LineFromPosition(currentPos);

		int startPos = PositionFromLine(line);
		int endPos = GetLineEndPosition(line);

		int length = currentPos - startPos;

		int startNewPos = PositionFromLine(line + 1);
		int endNewPos = GetLineEndPosition(line + 1);

		if (endNewPos - startNewPos < length)
		{
			std::string m_stringBuffer;

			for (int c = 0; c < (length - (endNewPos - startNewPos)); c++)
			{
				m_stringBuffer.push_back(' ');
			}

			InsertText(endNewPos, m_stringBuffer);
		}

		SetEmptySelection(startNewPos + length); break;
	}
	case WXK_NUMPAD_ENTER:
	case WXK_RETURN: PrepareTABs(); break;
		//case '.': if (m_bEnableAutoComplete) LoadAutoComplete(); event.Skip(); break;
	case ' ': if (m_bEnableAutoComplete && event.ControlDown()) LoadAutoComplete(); event.Skip(); break;
	case '9': if (m_bEnableAutoComplete && event.ShiftDown()) LoadCallTip(); event.Skip(); break;
	case '0': if (m_bEnableAutoComplete && event.ShiftDown()) ct.Cancel(); event.Skip(); break;
	default: event.Skip(); break;
	}
}

void CCodeEditor::OnMouseMove(wxMouseEvent& event)
{
	LoadToolTip(event.GetPosition()); event.Skip();
}
