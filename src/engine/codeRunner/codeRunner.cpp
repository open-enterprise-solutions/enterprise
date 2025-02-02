#include "codeRunner.h"
#include "backend/compiler/compileCode.h"
#include "backend/compiler/procUnit.h"

///////////////////////////////////////////////////////////////////////////

#define DEF_LINENUMBER_ID 0
#define DEF_BREAKPOINT_ID 1
#define DEF_FOLDING_ID 2

///////////////////////////////////////////////////////////////////////////

void CFrameCodeRunner::SyntaxCheckOnButtonClick(wxCommandEvent& event)
{
	try {
		m_compileCode->Compile(m_codeEditor->GetText());;
		CFrameCodeRunner::AppendOutput(_("No syntax errors detected!"));
	}
	catch (const CBackendException* err) {
		CFrameCodeRunner::AppendOutput(err->what());
	}

	event.Skip();
}

void CFrameCodeRunner::RunCodeOnButtonClick(wxCommandEvent& event)
{
	try {
		m_compileCode->Compile(m_codeEditor->GetText());
		m_procUnit->Execute(m_compileCode->m_cByteCode);
	}
	catch (const CBackendException* err) {
		CFrameCodeRunner::AppendOutput(err->what());
	}
	event.Skip();
}

void CFrameCodeRunner::ClearOutputOnButtonClick(wxCommandEvent& event)
{
	m_output->SetReadOnly(false);
	m_output->ClearAll();
	m_output->SetReadOnly(true);
	event.Skip();
}

void CFrameCodeRunner::OnKeyDown(wxKeyEvent& event)
{
	if (!m_codeEditor->IsEditable()) {
		event.Skip(); return;
	}

	switch (event.GetKeyCode())
	{
	case WXK_RETURN:
		PrepareTABs();
		break;
	default:event.Skip();
	};
}

void CFrameCodeRunner::OnStyleNeeded(wxStyledTextEvent& event)
{
	/*this is called every time the styler detects a line that needs style, so we style that range.
	This will save a lot of performance since we only style text when needed instead of parsing the whole file every time.*/
	int line_start = m_codeEditor->LineFromPosition(m_codeEditor->GetEndStyled());
	int line_end = m_codeEditor->GetFirstVisibleLine() + m_codeEditor->LinesOnScreen();

	if (line_end > m_codeEditor->GetLineCount()) {
		line_end = m_codeEditor->GetLineCount() - 1;
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
	if (m_codeEditor->GetLineCount() == m_codeEditor->LinesOnScreen()) {
		line_start = 0;
		line_end = m_codeEditor->GetLineCount() - 1;
	}

	if (line_end < line_start) {
		//that happens when you select parts that are in front of the styled area
		size_t temp = line_end;
		line_end = line_start;
		line_start = temp;
	}

	//style the line following the style area too (if present) in case fold level decreases in that one
	if (line_end < m_codeEditor->GetLineCount() - 1) {
		line_end++;
	}

	//get exact start positions
	size_t pos_start = m_codeEditor->PositionFromLine(line_start);
	size_t pos_end = (m_codeEditor->GetLineEndPosition(line_end));

	wxString strCode; int space_char = 0;
	strCode.reserve(pos_end - pos_start);

	// get str code 
	const std::string strBuffer(m_codeEditor->GetTextRangeRaw(pos_start, pos_end));
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

void CFrameCodeRunner::OnChagedText(wxStyledTextEvent& event)
{
	int modFlags = event.GetModificationType();
	if ((modFlags & (wxSTC_MOD_BEFOREINSERT)) == 0 &&
		(modFlags & (wxSTC_MOD_BEFOREDELETE)) == 0) {
		event.Skip();
		return;
	}

	event.Skip();
}

void CFrameCodeRunner::OnNeedShow(wxStyledTextEvent& event)
{
	event.Skip();
}

void CFrameCodeRunner::OnMarginClick(wxStyledTextEvent& event)
{
	const int currentLine =
		m_codeEditor->LineFromPosition(event.GetPosition());
	switch (event.GetMargin())
	{
	case DEF_FOLDING_ID:
		m_codeEditor->ToggleFold(currentLine);
		break;
	}
	event.Skip();
}

///////////////////////////////////////////////////////////////////////////
#define appendStyle(style) \
m_codeEditor->StartStyling(currPos); \
m_codeEditor->SetStyling(fromPos + translate.GetCurrentPos() - currPos, style);
///////////////////////////////////////////////////////////////////////////

#define wxSTC_FOLDLEVELBASE_FLAG 0x400

#define wxSTC_FOLDLEVELWHITE_FLAG 0x1000
#define wxSTC_FOLDLEVELHEADER_FLAG 0x2000

#define wxSTC_FOLDLEVELELSE_FLAG 0x4000

///////////////////////////////////////////////////////////////////////////

void CFrameCodeRunner::HighlightSyntaxAndCalculateFoldLevel(
	const int fromLine, const int toLine,
	const int fromPos, const int toPos,
	const wxString& strCode
)
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

	CFoldParser foldParser(m_codeEditor, fromLine, toLine);

	CTranslateCode translate;
	translate.Load(strCode);

	//вдруг строка начинается с комментария:
	int wasLeftPoint = wxNOT_FOUND;

	//remove old styling
	m_codeEditor->StartStyling(fromPos); //from here
	m_codeEditor->SetStyling(toPos - fromPos, wxSTC_C_COMMENT); //with that length and style -> cleared

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

void CFrameCodeRunner::PrepareTABs()
{
	const int curr_position = m_codeEditor->GetCurrentPos();
	const int curr_line =
		m_codeEditor->LineFromPosition(curr_position);

	const int level = m_codeEditor->GetFoldLevel(curr_line);
	int fold_level = level ^ wxSTC_FOLDLEVELBASE_FLAG;

	if ((level & wxSTC_FOLDLEVELHEADER_FLAG) != 0) {
		fold_level = (fold_level ^ wxSTC_FOLDLEVELHEADER_FLAG);
		const int start_line_pos = m_codeEditor->PositionFromLine(curr_line);		
		if (start_line_pos + fold_level != curr_position) {
			fold_level = fold_level + 1;
		}
	}
	else if ((level & wxSTC_FOLDLEVELELSE_FLAG) != 0) {
		fold_level = (fold_level ^ wxSTC_FOLDLEVELELSE_FLAG);
		if (fold_level >= 0) {
			const int start_line_pos = m_codeEditor->PositionFromLine(curr_line);
			int currFold = 0; unsigned int toPos = 0;
			std::string strBuffer(m_codeEditor->GetLineRaw(curr_line));
			const int length = curr_position - start_line_pos;
			for (unsigned int i = 0; i < length; i++) {
				if (strBuffer[i] == '\t' || strBuffer[i] == ' ') {
					currFold++; toPos = i + 1;
				}
				else break;
			};
			if (currFold != (fold_level - 1)) {
				(void)strBuffer.replace(0, toPos, fold_level - 1, '\t');
				(void)strBuffer.append(toPos > (fold_level - 1) ? toPos - (fold_level - 1) : 0, ' ');
				m_codeEditor->Replace(
					start_line_pos,
					start_line_pos + strBuffer.length(),
					strBuffer
				);
			}

			if (start_line_pos + fold_level - 1 == curr_position) {
				fold_level = fold_level - 1;
			}
		}
	}
	else if ((level & wxSTC_FOLDLEVELWHITE_FLAG) != 0) {
		fold_level = (fold_level ^ wxSTC_FOLDLEVELWHITE_FLAG) - 1;
		if (fold_level >= 0) {
			const int start_line_pos = m_codeEditor->PositionFromLine(curr_line);
			int currFold = 0; unsigned int toPos = 0;
			std::string strBuffer(m_codeEditor->GetLineRaw(curr_line));
			const int length = curr_position - start_line_pos;
			for (unsigned int i = 0; i < length; i++) {
				if (strBuffer[i] == '\t' || strBuffer[i] == ' ') {
					currFold++; toPos = i + 1;
				}
				else break;
			};
			if (currFold != fold_level) {
				(void)strBuffer.replace(0, toPos, fold_level, '\t');
				(void)strBuffer.append(toPos > fold_level ? toPos - fold_level : 0, ' ');
				m_codeEditor->Replace(
					start_line_pos,
					start_line_pos + strBuffer.length(),
					strBuffer
				);
			}
		}
	}

	std::string strTabs;
	strTabs.append("\r\n");

	if ((curr_line + 1) < m_codeEditor->GetLineCount()) {
		int currFold = 0; unsigned int toPos = 0;
		const int length = m_codeEditor->GetLineLength(curr_line + 1);
		if (length > 0) {
			std::string strBuffer(m_codeEditor->GetLineRaw(curr_line + 1));
			for (unsigned int i = 0; i < length; i++) {
				if (strBuffer[i] == '\t' || strBuffer[i] == ' ') {
					currFold++; toPos = i + 1;
				}
				else break;
			};
		}
	}

	for (int i = 0; i < fold_level; i++) strTabs.push_back('\t');
	m_codeEditor->InsertText(curr_position, strTabs);

	m_codeEditor->GotoLine(m_codeEditor->LineFromPosition(curr_position + strTabs.length()));
	m_codeEditor->SetEmptySelection(curr_position + strTabs.length());
}

void CFrameCodeRunner::SetFontColorSettings()
{
	// For some reason StyleSetFont takes a (non-const) reference, so we need to make
	// a copy before passing it in.
	wxFont font = wxFont(10, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);

	m_codeEditor->StyleClearAll();
	m_codeEditor->StyleSetFont(wxSTC_STYLE_DEFAULT, font);

	m_codeEditor->SetSelForeground(true, wxColor(0xFF, 0xFF, 0xFF));
	m_codeEditor->SetSelBackground(true, wxColor(0x0A, 0x24, 0x6A));

	m_codeEditor->StyleSetFont(wxSTC_C_DEFAULT, font);
	m_codeEditor->StyleSetFont(wxSTC_C_IDENTIFIER, font);

	m_codeEditor->StyleSetForeground(wxSTC_C_DEFAULT, wxColor(0x00, 0x00, 0x00));
	m_codeEditor->StyleSetBackground(wxSTC_C_DEFAULT, wxColor(0xFF, 0xFF, 0xFF));

	m_codeEditor->StyleSetForeground(wxSTC_STYLE_DEFAULT, wxColor(0x00, 0x00, 0x00));
	m_codeEditor->StyleSetBackground(wxSTC_STYLE_DEFAULT, wxColor(0xFF, 0xFF, 0xFF));

	m_codeEditor->StyleSetForeground(wxSTC_C_IDENTIFIER, wxColor(0x00, 0x00, 0x00));
	m_codeEditor->StyleSetBackground(wxSTC_C_IDENTIFIER, wxColor(0xFF, 0xFF, 0xFF));

	m_codeEditor->StyleSetFont(wxSTC_C_COMMENT, font);
	m_codeEditor->StyleSetFont(wxSTC_C_COMMENTLINE, font);
	m_codeEditor->StyleSetFont(wxSTC_C_COMMENTDOC, font);

	m_codeEditor->StyleSetForeground(wxSTC_C_COMMENT, wxColor(0x00, 0x80, 0x00));
	m_codeEditor->StyleSetBackground(wxSTC_C_COMMENT, wxColor(0xFF, 0xFF, 0xFF));
	m_codeEditor->StyleSetForeground(wxSTC_C_COMMENTLINE, wxColor(0x00, 0x80, 0x00));
	m_codeEditor->StyleSetBackground(wxSTC_C_COMMENTLINE, wxColor(0xFF, 0xFF, 0xFF));
	m_codeEditor->StyleSetForeground(wxSTC_C_COMMENTDOC, wxColor(0x00, 0x80, 0x00));
	m_codeEditor->StyleSetBackground(wxSTC_C_COMMENTDOC, wxColor(0xFF, 0xFF, 0xFF));

	m_codeEditor->StyleSetFont(wxSTC_C_WORD, font);
	m_codeEditor->StyleSetForeground(wxSTC_C_WORD, wxColor(0x00, 0x00, 0xFF));
	m_codeEditor->StyleSetBackground(wxSTC_C_WORD, wxColor(0xFF, 0xFF, 0xFF));

	m_codeEditor->StyleSetFont(wxSTC_C_OPERATOR, font);
	m_codeEditor->StyleSetForeground(wxSTC_C_OPERATOR, wxColor(0x80, 0x80, 0x80));
	m_codeEditor->StyleSetBackground(wxSTC_C_OPERATOR, wxColor(0xFF, 0xFF, 0xFF));

	m_codeEditor->StyleSetFont(wxSTC_C_STRING, font);
	m_codeEditor->StyleSetForeground(wxSTC_C_STRING, wxColor(0xFF, 0x80, 0x80));
	m_codeEditor->StyleSetBackground(wxSTC_C_STRING, wxColor(0xFF, 0xFF, 0xFF));

	m_codeEditor->StyleSetFont(wxSTC_C_STRINGEOL, font);
	m_codeEditor->StyleSetForeground(wxSTC_C_STRINGEOL, wxColor(0xFF, 0x80, 0x80));
	m_codeEditor->StyleSetBackground(wxSTC_C_STRINGEOL, wxColor(0xFF, 0xFF, 0xFF));

	m_codeEditor->StyleSetFont(wxSTC_C_CHARACTER, font);
	m_codeEditor->StyleSetForeground(wxSTC_C_CHARACTER, wxColor(0xFF, 0x80, 0x80));
	m_codeEditor->StyleSetBackground(wxSTC_C_CHARACTER, wxColor(0xFF, 0xFF, 0xFF));

	m_codeEditor->StyleSetFont(wxSTC_C_NUMBER, font);
	m_codeEditor->StyleSetForeground(wxSTC_C_NUMBER, wxColor(0xFF, 0x00, 0x00));
	m_codeEditor->StyleSetBackground(wxSTC_C_NUMBER, wxColor(0xFF, 0xFF, 0xFF));

	m_codeEditor->StyleSetSize(wxSTC_STYLE_LINENUMBER, font.GetPointSize());

	m_codeEditor->SetIndent(4);
	m_codeEditor->SetTabWidth(4);

	m_codeEditor->SetUseTabs(true);
	m_codeEditor->SetTabIndents(true);
	m_codeEditor->SetBackSpaceUnIndents(true);
	m_codeEditor->SetViewWhiteSpace(false);

	m_codeEditor->SetDoubleBuffered(true);

	// Use the newer D2D drawing.
	//m_codeEditor->SetTechnology(wxSTC_TECHNOLOGY_DIRECTWRITE);

	// Figure out how wide the margin needs to be do display
	// the most number of linqes we'd reasonbly have.
	int marginSize = m_codeEditor->TextWidth(wxSTC_STYLE_LINENUMBER, "_999999");
	m_codeEditor->SetMarginWidth(DEF_LINENUMBER_ID, marginSize);

	// folding
	m_codeEditor->SetMarginType(DEF_FOLDING_ID, wxSTC_MARGIN_SYMBOL);
	m_codeEditor->SetMarginMask(DEF_FOLDING_ID, wxSTC_MASK_FOLDERS);

	m_codeEditor->SetMarginWidth(DEF_FOLDING_ID, FromDIP(16));
	m_codeEditor->SetMarginSensitive(DEF_FOLDING_ID, true);
}

///////////////////////////////////////////////////////////////////////////

CFrameCodeRunner::CFrameCodeRunner(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style) :
	wxFrame(parent, id, title, pos, size, style), m_compileCode(new CCompileCode), m_procUnit(new CProcUnit)
{
	this->SetSizeHints(wxDefaultSize, wxDefaultSize);

	wxBoxSizer* bSizerMain;
	bSizerMain = new wxBoxSizer(wxVERTICAL);

	m_codeEditor = new wxStyledTextCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0, wxEmptyString);

	// initialize styles
	m_codeEditor->StyleClearAll();

	//Set margin cursor
	for (int margin = 0; margin < m_codeEditor->GetMarginCount(); margin++) {
		m_codeEditor->SetMarginCursor(margin, wxSTC_CURSORARROW);
	}

	m_codeEditor->SetAutomaticFold(wxSTC_AUTOMATICFOLD_CHANGE);

	//set Lexer to LEX_CONTAINER: This will trigger the styleneeded event so you can do your own highlighting
	m_codeEditor->SetLexer(wxSTC_LEX_CONTAINER);

	//set edge mode
	m_codeEditor->SetEdgeMode(wxSTC_EDGE_MULTILINE);

	// set visibility
	m_codeEditor->SetVisiblePolicy(wxSTC_VISIBLE_STRICT | wxSTC_VISIBLE_SLOP, 1);
	m_codeEditor->SetXCaretPolicy(wxSTC_CARET_EVEN | wxSTC_VISIBLE_STRICT | wxSTC_CARET_SLOP, 1);
	m_codeEditor->SetYCaretPolicy(wxSTC_CARET_EVEN | wxSTC_VISIBLE_STRICT | wxSTC_CARET_SLOP, 1);

	//markers
	m_codeEditor->MarkerDefine(wxSTC_MARKNUM_FOLDER, wxSTC_MARK_BOXPLUS, *wxWHITE, *wxBLACK);
	m_codeEditor->MarkerDefine(wxSTC_MARKNUM_FOLDEROPEN, wxSTC_MARK_BOXMINUS, *wxWHITE, *wxBLACK);
	m_codeEditor->MarkerDefine(wxSTC_MARKNUM_FOLDERSUB, wxSTC_MARK_VLINE, *wxWHITE, *wxBLACK);
	m_codeEditor->MarkerDefine(wxSTC_MARKNUM_FOLDEREND, wxSTC_MARK_BOXPLUSCONNECTED, *wxWHITE, *wxBLACK);
	m_codeEditor->MarkerDefine(wxSTC_MARKNUM_FOLDEROPENMID, wxSTC_MARK_BOXMINUSCONNECTED, *wxWHITE, *wxBLACK);
	m_codeEditor->MarkerDefine(wxSTC_MARKNUM_FOLDERMIDTAIL, wxSTC_MARK_TCORNER, *wxWHITE, *wxBLACK);
	m_codeEditor->MarkerDefine(wxSTC_MARKNUM_FOLDERTAIL, wxSTC_MARK_LCORNER, *wxWHITE, *wxBLACK);

	// annotations
	m_codeEditor->AnnotationSetVisible(wxSTC_ANNOTATION_BOXED);

	// Set fold flags
	m_codeEditor->SetFoldFlags(wxSTC_FOLDFLAG_LINEBEFORE_CONTRACTED | wxSTC_FOLDFLAG_LINEAFTER_CONTRACTED);

	// Setup the dwell time before a tooltip is displayed.
	m_codeEditor->SetMouseDwellTime(200);

	// Setup caret line
	//m_codeEditor->SetCaretLineVisible(true);

	// miscellaneous
	m_codeEditor->SetLayoutCache(wxSTC_CACHE_PAGE);

	//Turn the fold markers red when the caret is a line in the group (optional)
	m_codeEditor->MarkerEnableHighlight(true);

	bSizerMain->Add(m_codeEditor, 3, wxEXPAND | wxALL, 5);

	wxBoxSizer* bSizerButton;
	bSizerButton = new wxBoxSizer(wxHORIZONTAL);

	m_buttonSyntaxCheck = new wxButton(this, wxID_ANY, _("Syntax control"), wxDefaultPosition, wxDefaultSize, 0);
	m_buttonSyntaxCheck->SetForegroundColour(wxColour(255, 0, 0));

	bSizerButton->Add(m_buttonSyntaxCheck, 0, wxALL, 5);

	m_buttonRunCode = new wxButton(this, wxID_ANY, _("Run code"), wxDefaultPosition, wxDefaultSize, 0);
	m_buttonRunCode->SetBackgroundColour(wxColour(128, 255, 128));

	bSizerButton->Add(m_buttonRunCode, 1, wxALL, 5);

	m_buttonClearOutput = new wxButton(this, wxID_ANY, _("Clear output"), wxDefaultPosition, wxDefaultSize, 0);
	m_buttonClearOutput->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT));
	m_buttonClearOutput->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));

	bSizerButton->Add(m_buttonClearOutput, 0, wxALL, 5);
	bSizerMain->Add(bSizerButton, 0, wxEXPAND, 5);

	m_output = new wxStyledTextCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0, wxEmptyString);
	m_output->SetUseTabs(true);
	m_output->SetTabWidth(4);
	m_output->SetIndent(4);
	m_output->SetTabIndents(true);
	m_output->SetBackSpaceUnIndents(true);
	m_output->SetViewEOL(false);
	m_output->SetViewWhiteSpace(false);
	m_output->SetMarginWidth(2, 0);
	m_output->SetIndentationGuides(true);
	m_output->SetReadOnly(true);
	m_output->SetMarginType(1, wxSTC_MARGIN_SYMBOL);
	m_output->SetMarginMask(1, wxSTC_MASK_FOLDERS);
	m_output->SetMarginWidth(1, 16);
	m_output->SetMarginSensitive(1, true);
	m_output->SetProperty(wxT("fold"), wxT("1"));
	m_output->SetFoldFlags(wxSTC_FOLDFLAG_LINEBEFORE_CONTRACTED | wxSTC_FOLDFLAG_LINEAFTER_CONTRACTED);
	m_output->SetMarginType(0, wxSTC_MARGIN_NUMBER);
	m_output->SetMarginWidth(0, m_output->TextWidth(wxSTC_STYLE_LINENUMBER, wxT("_99999")));
	m_output->MarkerDefine(wxSTC_MARKNUM_FOLDER, wxSTC_MARK_BOXPLUS);
	m_output->MarkerSetBackground(wxSTC_MARKNUM_FOLDER, wxColour(wxT("BLACK")));
	m_output->MarkerSetForeground(wxSTC_MARKNUM_FOLDER, wxColour(wxT("WHITE")));
	m_output->MarkerDefine(wxSTC_MARKNUM_FOLDEROPEN, wxSTC_MARK_BOXMINUS);
	m_output->MarkerSetBackground(wxSTC_MARKNUM_FOLDEROPEN, wxColour(wxT("BLACK")));
	m_output->MarkerSetForeground(wxSTC_MARKNUM_FOLDEROPEN, wxColour(wxT("WHITE")));
	m_output->MarkerDefine(wxSTC_MARKNUM_FOLDERSUB, wxSTC_MARK_EMPTY);
	m_output->MarkerDefine(wxSTC_MARKNUM_FOLDEREND, wxSTC_MARK_BOXPLUS);
	m_output->MarkerSetBackground(wxSTC_MARKNUM_FOLDEREND, wxColour(wxT("BLACK")));
	m_output->MarkerSetForeground(wxSTC_MARKNUM_FOLDEREND, wxColour(wxT("WHITE")));
	m_output->MarkerDefine(wxSTC_MARKNUM_FOLDEROPENMID, wxSTC_MARK_BOXMINUS);
	m_output->MarkerSetBackground(wxSTC_MARKNUM_FOLDEROPENMID, wxColour(wxT("BLACK")));
	m_output->MarkerSetForeground(wxSTC_MARKNUM_FOLDEROPENMID, wxColour(wxT("WHITE")));
	m_output->MarkerDefine(wxSTC_MARKNUM_FOLDERMIDTAIL, wxSTC_MARK_EMPTY);
	m_output->MarkerDefine(wxSTC_MARKNUM_FOLDERTAIL, wxSTC_MARK_EMPTY);
	m_output->SetSelBackground(true, wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT));
	m_output->SetSelForeground(true, wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT));

	//Set margin cursor
	for (int margin = 0; margin < m_output->GetMarginCount(); margin++) {
		m_output->SetMarginCursor(margin, wxSTC_CURSORARROW);
	}

	bSizerMain->Add(m_output, 1, wxEXPAND | wxALL, 5);

	m_output->SetCodePage(wxSTC_CP_UTF8); //Устанавливаем кодировку Юникод (UTF-8)

	SetFontColorSettings();

	this->SetSizer(bSizerMain);
	this->Layout();
	m_statusBar = this->CreateStatusBar(1, wxSTB_SIZEGRIP, wxID_ANY);

	this->Centre(wxBOTH);

	for (auto ctor : CValue::GetListCtorsByType(eCtorObjectType_object_context)) {
		m_compileCode->AddContextVariable(ctor->GetClassName(), ctor->CreateObject());
	}

	// Connect Events
	m_buttonSyntaxCheck->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(CFrameCodeRunner::SyntaxCheckOnButtonClick), nullptr, this);
	m_buttonRunCode->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(CFrameCodeRunner::RunCodeOnButtonClick), nullptr, this);
	m_buttonClearOutput->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(CFrameCodeRunner::ClearOutputOnButtonClick), nullptr, this);

	m_codeEditor->Connect(wxEVT_KEY_DOWN, wxKeyEventHandler(CFrameCodeRunner::OnKeyDown), nullptr, this);
	m_codeEditor->Connect(wxEVT_STC_STYLENEEDED, wxStyledTextEventHandler(CFrameCodeRunner::OnStyleNeeded), nullptr, this);
	m_codeEditor->Connect(wxEVT_STC_MARGINCLICK, wxStyledTextEventHandler(CFrameCodeRunner::OnMarginClick), nullptr, this);
	m_codeEditor->Connect(wxEVT_STC_MODIFIED, wxStyledTextEventHandler(CFrameCodeRunner::OnChagedText), nullptr, this);
	m_codeEditor->Connect(wxEVT_STC_NEEDSHOWN, wxStyledTextEventHandler(CFrameCodeRunner::OnNeedShow), nullptr, this);

}

CFrameCodeRunner::~CFrameCodeRunner()
{
	wxDELETE(m_compileCode);
	wxDELETE(m_procUnit);
}
