////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : autoComplete loader  
////////////////////////////////////////////////////////////////////////////

#include "codeEditor.h"

#include "frmclient/docView/tabDocument.h"   // the module's tab — what the caret's questions are asked of

#include <wx/file.h>
#include <wx/stdpaths.h>
#include <algorithm>
#include <cstring>
#include <memory>

// ⭐⭐ THE DESKTOP'S ANSWERS ARE THE SERVER'S NOW — the members of what the caret stands after (ibValueAtCaret), the names
// in scope (ibNamesAtCaret), the classes a `new` or a `type` takes, the common forms, a call's form: the compiler and the
// configuration were the desktop's process's, and are the server's. Asked of the tab's view, which answers as the
// desktop's editor did (frmserver ibModuleTextView: FetchCompletion, FetchCallTip). The debugger's answer for a caret the
// compiler could not resolve (the desktop's LoadFromDebugger, ShowAutoComplete) comes with the debugger.
bool ibCodeEditor::FetchAtCaret(ibProtocol::ibView::ibModule::ibRequest kind, ibProtocolNode& answer)
{
	if (m_document == nullptr)
		return false;

	// The text as it is now — what the person typed may not be the server's yet — and the caret in characters, as the
	// compiler measures it (GetRealPosition).
	ibProtocolNode request;
	request.SetValue(ibProtocol::ibView::ibModule::Kind, static_cast<int>(kind))
		.SetValue(ibProtocol::ibView::ibModule::Text, GetText())
		.SetValue(ibProtocol::ibView::ibModule::Position, GetRealPosition());
	return m_document->Fetch(request, answer);
}

void ibCodeEditor::PrepareTooTipExpression(unsigned int currPos, wxString& expression, wxString& currentWord, bool& outHasPoint)
{
	bool hasPoint = false;

	for (unsigned int i = 0; i < m_tc.GetLexCount(); i++)
	{
		if (m_tc.GetLexAt(i).m_numString > currPos
			&& !hasPoint) break;

		if (m_tc.GetLexAt(i).m_lexType == IDENTIFIER)
		{
			if (hasPoint) expression += m_tc.GetLexAt(i).m_valData.GetString();
			else expression = m_tc.GetLexAt(i).m_valData.GetString();

			currentWord = m_tc.GetLexAt(i).m_valData.GetString();

			if (i < m_tc.GetLexCount() - 1) {
				const ibLexem& lex = m_tc.GetLexAt(i + 1);
				if (lex.m_lexType == DELIMITER && lex.m_numData == '(')
					expression = wxEmptyString;
				hasPoint = lex.m_lexType == DELIMITER && lex.m_numData == '.';
			}
			else hasPoint = false;
		}
		else if (m_tc.GetLexAt(i).m_lexType == DELIMITER
			&& m_tc.GetLexAt(i).m_numData == '.')
		{
			if (!expression.IsEmpty())
				expression += '.';

			currentWord = wxEmptyString; hasPoint = true;
		}
		else
		{
			expression = wxEmptyString; currentWord = wxEmptyString;
		}
	}

	outHasPoint = hasPoint;
}

namespace {

// Strip EOL markers in-place per the editor's current EOL mode so the
// leading-whitespace scan that follows doesn't trip over `\r`/`\n`.
void StripEOL(wxString& s, int eolMode)
{
	switch (eolMode) {
	case wxSTC_EOL_CRLF:
		s.Replace(wxT("\r"), wxEmptyString);
		s.Replace(wxT("\n"), wxEmptyString);
		break;
	case wxSTC_EOL_CR:
		s.Replace(wxT("\r"), wxEmptyString);
		break;
	default:
		s.Replace(wxT("\n"), wxEmptyString);
		break;
	}
}

// Append the editor's EOL marker(s).
void AppendEOL(wxString& s, int eolMode)
{
	switch (eolMode) {
	case wxSTC_EOL_CRLF: s += wxT("\r\n"); break;
	case wxSTC_EOL_CR:   s += wxT("\r");   break;
	default:             s += wxT("\n");   break;
	}
}

// Count leading '\t'/' ' chars; outReplacePos receives one past the last
// whitespace character (the splice end for indent rewrite below).
int CountLeadingIndent(const wxString& s, int& outReplacePos)
{
	int count = 0;
	outReplacePos = 0;
	const int len = (int)s.length();
	for (int i = 0; i < len; ++i) {
		const wxUniChar c = s[i];
		if (c == wxT('\t') || c == wxT(' ')) {
			count++;
			outReplacePos = i + 1;
		}
		else break;
	}
	return count;
}

// True for lines that contain only whitespace (incl. EOL chars). Used
// to walk back through blank lines while resolving a CES soft-header
// region — `if (cond)` followed by blanks before the body is still
// one soft-stmt scope.
bool IsBlankOrWhitespace(const wxString& line)
{
	for (size_t i = 0; i < line.length(); ++i) {
		const wxUniChar c = line[i];
		if (c != wxT(' ') && c != wxT('\t') && c != wxT('\r') && c != wxT('\n'))
			return false;
	}
	return true;
}

// True when the line is whitespace + a single `{` (with optional trailing
// whitespace / EOL). Marks an Allman-style brace opener that "consumes"
// any pending soft single-statement body bump from the previous header
// — the `{` itself sits at the header's indent, not at body indent.
bool IsLineJustOpenBrace(const wxString& line)
{
	size_t i = 0;
	while (i < line.length() && (line[i] == wxT(' ') || line[i] == wxT('\t')))
		++i;
	if (i >= line.length() || line[i] != wxT('{'))
		return false;
	for (++i; i < line.length(); ++i) {
		const wxUniChar c = line[i];
		if (c != wxT(' ') && c != wxT('\t') && c != wxT('\r') && c != wxT('\n'))
			return false;
	}
	return true;
}

// CES brace-less single-statement header: `if (cond)`, `while (cond)`,
// `for (...)`, `for each (...)` — no `{` on the line, condition closes
// with `)`. Fold parser doesn't open a brace fold here (there's no
// matching closer), so the line carries no header flag and the next
// line would otherwise inherit the same indent. The bump applies only
// to the immediate Enter; the line after the body returns to base.
bool IsCESSingleStmtHeader(const wxString& line)
{
	size_t i = 0;
	while (i < line.length() && (line[i] == wxT(' ') || line[i] == wxT('\t')))
		++i;
	if (i >= line.length()) return false;

	wxString core = line.Mid(i);
	if (int cmt = core.Find(wxT("//")); cmt != wxNOT_FOUND)
		core = core.Left(cmt);
	while (!core.empty() && (core.Last() == wxT(' ') || core.Last() == wxT('\t') ||
	                         core.Last() == wxT('\r') || core.Last() == wxT('\n')))
		core.RemoveLast();
	if (core.empty()) return false;

	if (core.Last() != wxT(')')) return false;
	if (core.Contains(wxT('{'))) return false;

	auto matchKw = [&](const wxString& kw) {
		if (core.length() <= kw.length()) return false;
		if (core.Mid(0, kw.length()).Lower() != kw) return false;
		const wxChar after = core[kw.length()];
		return after == wxT(' ') || after == wxT('\t') || after == wxT('(');
	};
	return matchKw(wxT("if")) || matchKw(wxT("while")) || matchKw(wxT("for"));
}

} // namespace

/**
 * PrepareTABs
 *   Auto-indent the current line on Enter:
 *     1. Read the cached fold mask for the line.
 *     2. Compute the target indent for the CURRENT line based on the
 *        fold flag — HEADER aligns to its own level, ELSE/EndXxx (WHITE)
 *        align to the parent indent.
 *     3. Rewrite the current line's leading tabs to match.
 *     4. Append EOL + indent for the NEW line that Enter is about to
 *        produce, and replace the line slice through STC's target API.
 */
void ibCodeEditor::PrepareTABs()
{
	const int currPosition = GetCurrentPos();
	const int currLine     = LineFromPosition(currPosition);
	const int startLinePos = PositionFromLine(currLine);
	const int level        = m_fp.GetFoldMask(currLine);
	const int eolMode      = GetEOLMode();

	wxString rawBufferLine;
	if (startLinePos != currPosition) {
		const auto buf = GetTextRangeRaw(startLinePos, currPosition);
		rawBufferLine = wxString::FromUTF8(buf.data(), buf.length());
	}

	// Strip any EOL chars upfront so the indent rewrite operates on a
	// pure (whitespace-prefix + text) buffer. Typically the line slice
	// before the caret has none, but this also closes a latent bug where
	// the previous version could leave duplicated EOLs when no fold flag
	// matched (cursor on a free-form line with stray CR/LF).
	StripEOL(rawBufferLine, eolMode);

	int foldLevel = level ^ wxSTC_FOLDLEVELBASE_FLAG;

	// CES brace-less single-statement form: `if (cond)`, `while (cond)`,
	// `for (...)`. Fold parser tracks only `{...}` braces, so the body line
	// has no header flag and the fold-derived foldLevel for body and header
	// match. softBodyExtra carries +1 for the body line so its indent is
	// preserved. currIsSoftHeader bumps the next-line indent. Both stack
	// for nested headers. Walk back through blank lines so `if (cond)\n
	// <blank>\n  body;` still recognises the soft-body region.
	const bool isCES = (m_tc.GetCodeStyle() == CODE_CES);
	bool prevIsSoftHeader = false;
	if (isCES) {
		for (int l = currLine - 1; l >= 0; --l) {
			const wxString prev = GetLine(l);
			if (IsBlankOrWhitespace(prev)) continue;
			prevIsSoftHeader = IsCESSingleStmtHeader(prev);
			break;
		}
	}
	const bool currIsSoftHeader = isCES && IsCESSingleStmtHeader(rawBufferLine);
	const bool currLineBlank    = IsBlankOrWhitespace(rawBufferLine);
	// Allman: `{` on its own line absorbs the soft-body bump. The `{` line
	// itself sits at the soft header's indent; body inside braces at +1.
	// Without this the `{` would be re-indented to body level by the
	// HEADER branch's applyIndent call.
	const bool currLineIsBraceOpener = isCES && IsLineJustOpenBrace(rawBufferLine);
	const int  softBodyExtra    = (prevIsSoftHeader && !currLineIsBraceOpener) ? 1 : 0;


	// CES indent rewrite is no-shrink: fold parser only tracks `{...}`,
	// so manual indents inside soft-body regions or hand-aligned closers
	// would otherwise be stripped. VES keeps strict match — its fold
	// vector is keyword-fenced and accurate.
	auto applyIndent = [&](int targetTabs) {
		int replacePos = 0;
		const int currentIndent = CountLeadingIndent(rawBufferLine, replacePos);
		if (isCES) {
			if (currentIndent < targetTabs)
				rawBufferLine.replace(0, replacePos, wxString(wxT('\t'), targetTabs));
		}
		else if (currentIndent != targetTabs) {
			rawBufferLine.replace(0, replacePos, wxString(wxT('\t'), targetTabs));
		}
	};

	if ((level & wxSTC_FOLDLEVELHEADER_FLAG) != 0) {
		// Procedure/Function/If/While/Try header — line itself sits at
		// foldLevel; Enter on the header opens its body, +1 indent.
		foldLevel ^= wxSTC_FOLDLEVELHEADER_FLAG;
		applyIndent(foldLevel + softBodyExtra);
		if (startLinePos + foldLevel != currPosition) foldLevel++;
		// Inside-brace body of a soft-scope `{` opener: lines inside the
		// braces sit at brace-fold + soft carry. Without this carry the
		// body of `if (x)\n\t{` lands at the brace level only.
		foldLevel += softBodyExtra;
	}
	else if ((level & wxSTC_FOLDLEVELELSE_FLAG) != 0) {
		// Else/ElseIf/Except — pseudo-header at child indent; visually
		// aligns to parent indent (foldLevel - 1).
		foldLevel ^= wxSTC_FOLDLEVELELSE_FLAG;
		if (foldLevel >= 0 && LineLength(currLine) > 0)
			applyIndent(std::max(0, foldLevel - 1) + softBodyExtra);
		if (startLinePos + foldLevel - 1 == currPosition) foldLevel--;
	}
	else if ((level & wxSTC_FOLDLEVELWHITE_FLAG) != 0) {
		// EndProcedure/EndFunction/EndIf/EndDo/EndTry — closer at parent
		// indent; the WHITE flag's level is already child, so subtract 1.
		foldLevel = (foldLevel ^ wxSTC_FOLDLEVELWHITE_FLAG) - 1;
		if (foldLevel >= 0 && LineLength(currLine) > 0)
			applyIndent(foldLevel + softBodyExtra);
	}
	else if ((level & wxSTC_FOLDLEVELBASE_FLAG) != 0) {
		// Plain code line inside a block — indent matches foldLevel.
		if (foldLevel >= 0 && LineLength(currLine) > 0)
			applyIndent(foldLevel + softBodyExtra);
	}

	// New-line indent: if current is a soft header, body goes one deeper
	// than the current line's effective indent (foldLevel + softBodyExtra).
	// If current line is still blank inside a soft-body region, carry the
	// softBodyExtra forward — the body hasn't been typed yet, the next
	// Enter stays in the same scope. Otherwise the body terminates after
	// this statement and the next line returns to header level.
	if (currIsSoftHeader)
		foldLevel += 1 + softBodyExtra;
	else if (currLineBlank && prevIsSoftHeader)
		foldLevel += softBodyExtra;

	AppendEOL(rawBufferLine, eolMode);
	rawBufferLine.append(foldLevel, wxT('\t'));

	// wxSTC byte-positions: convert to UTF-8 once for both ReplaceTargetRaw
	// and the post-replace caret math.
	const auto utf8 = rawBufferLine.utf8_str();
	SetTargetStart((int)startLinePos);
	SetTargetEnd((int)currPosition);
	ReplaceTargetRaw(utf8.data(), utf8.length());

	const size_t length = utf8.length();
	GotoLine(LineFromPosition(startLinePos + length));
	SetEmptySelection(startLinePos + length);
}

// Compute the fold-based target indent (in tab units) for a single
// line — same decision tree as PrepareTABs, but operates on already-
// committed lines rather than the buffer-being-edited.
namespace {
int ComputeTargetIndent(int level)
{
	int foldLevel = level ^ wxSTC_FOLDLEVELBASE_FLAG;

	if ((level & wxSTC_FOLDLEVELHEADER_FLAG) != 0) {
		return std::max(0, foldLevel ^ wxSTC_FOLDLEVELHEADER_FLAG);
	}
	if ((level & wxSTC_FOLDLEVELELSE_FLAG) != 0) {
		const int parent = (foldLevel ^ wxSTC_FOLDLEVELELSE_FLAG) - 1;
		return std::max(0, parent);
	}
	if ((level & wxSTC_FOLDLEVELWHITE_FLAG) != 0) {
		return std::max(0, (foldLevel ^ wxSTC_FOLDLEVELWHITE_FLAG) - 1);
	}
	return std::max(0, foldLevel);
}
} // namespace

void ibCodeEditor::FormatSelection()
{
	if (!IsEditable())
		return;

	int selStart = 0, selEnd = 0;
	GetSelection(&selStart, &selEnd);

	int firstLine = LineFromPosition(selStart);
	int lastLine  = LineFromPosition(selEnd);

	// Collapsed selection past the line start sticks to the previous
	// line — typical for caret at column 0; without this the user's
	// "format current line" selects the wrong line.
	if (firstLine == lastLine && selStart == selEnd) {
		// no-op selection — format just the caret line
	}
	else if (selEnd > selStart && PositionFromLine(lastLine) == selEnd && lastLine > firstLine) {
		// Selection ends exactly at the start of lastLine — exclude that
		// trailing line so a triple-click style line-select doesn't
		// reformat one extra line below.
		lastLine--;
	}

	BeginUndoAction();
	for (int line = firstLine; line <= lastLine; line++) {
		const int mask = m_fp.GetFoldMask(line);
		const int target = ComputeTargetIndent(mask);
		// SetLineIndentation honours UseTabs / IndentSize so the
		// rewrite respects the editor's per-doc tab/space preference.
		SetLineIndentation(line, target * GetIndent());
	}
	EndUndoAction();
}

// Brace pair highlight on caret movement. wxSTC's BraceHighlight wants
// matched positions; we resolve them manually because the custom styler
// doesn't tag braces, and wxSTC's BraceMatch fails without that. Three
// pairs supported: `{}`, `()`, `[]`.
namespace {

bool IsOpenBrace(wxChar c)  { return c == wxT('{') || c == wxT('(') || c == wxT('['); }
bool IsCloseBrace(wxChar c) { return c == wxT('}') || c == wxT(')') || c == wxT(']'); }
wxChar PairOf(wxChar c)
{
	switch ((int)c) {
		case '{': return wxT('}');
		case '}': return wxT('{');
		case '(': return wxT(')');
		case ')': return wxT('(');
		case '[': return wxT(']');
		case ']': return wxT('[');
	}
	return wxT('\0');
}

} // namespace

void ibCodeEditor::OnUpdateUI(wxStyledTextEvent& event)
{
	const int caret = GetCurrentPos();

	// Prefer brace just before caret, fall back to brace at caret.
	int bracePos = -1;
	wxChar braceCh = 0;
	if (caret > 0) {
		const wxChar c = (wxChar)GetCharAt(caret - 1);
		if (IsOpenBrace(c) || IsCloseBrace(c)) {
			bracePos = caret - 1;
			braceCh  = c;
		}
	}
	if (bracePos < 0) {
		const wxChar c = (wxChar)GetCharAt(caret);
		if (IsOpenBrace(c) || IsCloseBrace(c)) {
			bracePos = caret;
			braceCh  = c;
		}
	}

	if (bracePos < 0) {
		BraceHighlight(wxSTC_INVALID_POSITION, wxSTC_INVALID_POSITION);
		event.Skip();
		return;
	}

	const wxChar wantedPair = PairOf(braceCh);
	const bool   forward    = IsOpenBrace(braceCh);
	const int    total      = GetLength();
	int          matchPos   = -1;
	int          depth      = 1;

	if (forward) {
		for (int p = bracePos + 1; p < total; ++p) {
			const wxChar c = (wxChar)GetCharAt(p);
			if (c == braceCh)   ++depth;
			else if (c == wantedPair) { if (--depth == 0) { matchPos = p; break; } }
		}
	}
	else {
		for (int p = bracePos - 1; p >= 0; --p) {
			const wxChar c = (wxChar)GetCharAt(p);
			if (c == braceCh)   ++depth;
			else if (c == wantedPair) { if (--depth == 0) { matchPos = p; break; } }
		}
	}

	if (matchPos < 0)
		BraceBadLight(bracePos);
	else
		BraceHighlight(bracePos, matchPos);

	event.Skip();
}

// Auto-align a typed brace. Fires from wxEVT_STC_CHARADDED on every
// character; gates on `{` / `}` and rewrites the leading indent only
// when the line so far is whitespace-only. Mixed-content typing like
// `foo();}` is left alone — the user clearly didn't intend a fresh
// closer there.
//
// `}` matching is a manual backward depth walk over raw bytes — wxSTC's
// `BraceMatch` requires the lexer to style brace characters and our
// custom styler doesn't, so it returns -1 here.
//
// `{` on a blank line right after a CES single-statement header
// (`if (cond)` / `while (cond)` / `for (...)`) absorbs the pending
// soft-body bump (Allman style): `{` sits at the header's indent.
void ibCodeEditor::OnCharAdded(wxStyledTextEvent& event)
{
	const int key = event.GetKey();
	if (key != '}' && key != '{') {
		event.Skip();
		return;
	}

	const int caret      = GetCurrentPos();
	const int line       = LineFromPosition(caret);
	const int lineStart  = PositionFromLine(line);
	const int bracePos   = caret - 1;  // the just-typed brace

	if (bracePos < lineStart) {
		event.Skip();
		return;
	}

	for (int p = lineStart; p < bracePos; ++p) {
		const wxChar c = (wxChar)GetCharAt(p);
		if (c != wxT(' ') && c != wxT('\t')) {
			event.Skip();
			return;
		}
	}

	if (key == '}') {
		int matchPos = -1;
		int depth = 1;  // bracePos is a `}`, balance starts at 1
		for (int p = bracePos - 1; p >= 0; --p) {
			const wxChar c = (wxChar)GetCharAt(p);
			if (c == wxT('}'))      ++depth;
			else if (c == wxT('{')) { if (--depth == 0) { matchPos = p; break; } }
		}

		if (matchPos < 0) {
			event.Skip();
			return;
		}

		const int matchLine   = LineFromPosition(matchPos);
		const int matchIndent = GetLineIndentation(matchLine);
		const int currIndent  = GetLineIndentation(line);

		if (matchIndent != currIndent)
			SetLineIndentation(line, matchIndent);
	}
	else {
		// `{` — Allman dedent under a CES soft single-statement header.
		if (m_tc.GetCodeStyle() != CODE_CES) {
			event.Skip();
			return;
		}

		int headerLine = -1;
		for (int l = line - 1; l >= 0; --l) {
			const wxString prev = GetLine(l);
			if (IsBlankOrWhitespace(prev)) continue;
			if (IsCESSingleStmtHeader(prev)) headerLine = l;
			break;  // first non-blank line found, done
		}

		if (headerLine < 0) {
			event.Skip();
			return;
		}

		const int headerIndent = GetLineIndentation(headerLine);
		const int currIndent   = GetLineIndentation(line);

		if (headerIndent != currIndent)
			SetLineIndentation(line, headerIndent);
	}

	event.Skip();
}

void ibCodeEditor::IncreaseIndent()
{
	if (!IsEditable())
		return;
	// wxSTC's Tab() command — with selection, indents every covered
	// line by IndentSize; without selection, inserts a tab at caret.
	Tab();
}

void ibCodeEditor::DecreaseIndent()
{
	if (!IsEditable())
		return;
	BackTab();
}

namespace {
// Compute first / last line covered by the current selection. If nothing
// is selected, both equal the caret line. Triple-click style line-select
// (ends at column 0 of the next line) drops the trailing line so the
// command doesn't grab one extra line below the visible selection.
void SelectedLineRange(ibCodeEditor* ed, int& firstLine, int& lastLine)
{
	int selStart = 0, selEnd = 0;
	ed->GetSelection(&selStart, &selEnd);
	firstLine = ed->LineFromPosition(selStart);
	lastLine  = ed->LineFromPosition(selEnd);
	if (selEnd > selStart && ed->PositionFromLine(lastLine) == selEnd && lastLine > firstLine)
		lastLine--;
}
} // namespace

void ibCodeEditor::AddCommentsToSelection()
{
	if (!IsEditable())
		return;

	int firstLine = 0, lastLine = 0;
	SelectedLineRange(this, firstLine, lastLine);

	BeginUndoAction();
	for (int line = firstLine; line <= lastLine; line++) {
		const int pos = PositionFromLine(line);
		Replace(pos, pos, "//");
	}
	EndUndoAction();
}

void ibCodeEditor::RemoveCommentsFromSelection()
{
	if (!IsEditable())
		return;

	int firstLine = 0, lastLine = 0;
	SelectedLineRange(this, firstLine, lastLine);

	BeginUndoAction();
	for (int line = firstLine; line <= lastLine; line++) {
		const int startPos = PositionFromLine(line);
		const wxString sLine = GetLineRaw(line);
		for (unsigned int i = 0; i + 1 < sLine.length(); i++) {
			if (sLine[i] == '/' && sLine[i + 1] == '/') {
				Replace(startPos + i, startPos + i + 2, wxEmptyString);
				break;
			}
		}
	}
	EndUndoAction();
}

void ibCodeEditor::LoadAutoComplete()
{
	int realPos = GetRealPosition();

	// Find the word start
	int currentPos = GetCurrentPos();

	int wordStartPos = WordStartPosition(currentPos, true);

	// Display the autocompletion list
	int lenEntered = currentPos - wordStartPos;

	if (m_ct.Active())
		m_ct.Cancel();

	const ibTranslateCode::ibCaretText at = m_tc.CaretAt((unsigned int)realPos);

	// User stands AT a word boundary (Ctrl+Space at the very start of an identifier, or in
	// trailing whitespace). The word under the caret then lands as the Append filter — and
	// `Find("MESSAGE")` rejects every system keyword like `If`, `Then`. Clear it so the dropdown
	// shows all candidates; the user filters live by typing forward.
	const wxString currentWord = (lenEntered == 0) ? wxString() : at.m_word;

	m_ac.Start(currentWord, currentPos, lenEntered, TextHeight(GetCurrentLine()));

	// What may be written here, by the place the caret stands in — the members after a dot, a call's names, what is in
	// scope — the tab's answer (ibProtocol::ibView::ibModule::ibRequest::Complete); the icon each wears is its kind.
	ibProtocolNode answer;
	if (FetchAtCaret(ibProtocol::ibView::ibModule::ibRequest::Complete, answer)) {
		for (const ibProtocolNode& name : answer.Children()) {
			m_ac.Append(static_cast<short>(name.GetInt(ibProtocol::ibView::ibModule::Kind)),
				name.GetString(ibProtocol::ibView::ibModule::Name), name.GetString(ibProtocol::ibView::ibModule::Help));
		}
	}

	wxPoint position = PointFromPosition(wordStartPos);
	position.y += TextHeight(GetCurrentLine());

	m_ac.Show(position);
}

void ibCodeEditor::LoadToolTip(const wxPoint& pos)
{
	if (!IsDebuggerEnterLoop() || m_document == nullptr)
		return;

	int currentPos = GetRealPositionFromPoint(pos);
	wxString expression, currentWord; bool hasPoint = false;
	PrepareTooTipExpression(currentPos, expression, currentWord, hasPoint);

	expression.Trim(true).Trim(false);

	if (expression.IsEmpty()) {
		ibCodeEditor::SetDebugValue(wxEmptyString);
		m_askedExpression.clear();
		return;
	}

	// ⭐⭐ ONE QUESTION PER WORD, BUT THE ANSWER PUT BACK ON EVERY MOVEMENT. Both halves matter, and they
	// are not the same thing.
	//
	// The value is shown as the window's TOOLTIP, which the system pops on ITS own schedule and shows
	// whatever text it finds in place at that moment. Laying the text down again and again is what makes
	// it appear smoothly — it used to happen by accident, because every movement asked again and the
	// answer arrived again. ASKING, though, costs a full round trip to the application parked at the
	// breakpoint; laying the same answer back costs nothing.
	//
	// 🛑 THE ASKING WAS MEASURED 2026-09-25, in the technology journal, because it took the debugger down:
	// moving the mouse over code at one breakpoint was 440 EvalToolTip requests, 36 of them inside the
	// last 150 ms. (That storm was the CONDITION of the detaching, not its cause — see SendExpressions in
	// debugServer.cpp for the frame that actually broke.)
	if (expression == m_askedExpression) {
		ibCodeEditor::SetDebugValue(m_askedValue);
		return;
	}

	// A DIFFERENT WORD — whatever is up belongs to the previous one, and until this one is answered there
	// is nothing true to show.
	ibCodeEditor::SetDebugValue(wxEmptyString);

	m_askedExpression = expression;

	OnEvaluateToolTip(expression);
}

// ⭐ THE CALL FORM OF WHAT IS BEING WRITTEN — the same question as the dropdown, asked for one
// name instead of all of them (the tab answers both).
void ibCodeEditor::LoadCallTip()
{
	// ⚠ TWO POSITIONS, AND THEY ARE NOT THE SAME NUMBER. The compiler measures a caret in
	// CHARACTERS (GetRealPosition counts them); wxSTC places a window by its own document position,
	// which is a BYTE offset. Any non-ASCII text above the caret pulls them apart — measured
	// 2026-09-07 on a module whose messages are in Russian: the call tip was drawn four lines above
	// the call it described.
	const int currentPos = GetRealPosition();
	const int screenPos = GetCurrentPos();

	const ibTranslateCode::ibCaretText at = m_tc.CaretAt((unsigned int)currentPos);
	if (at.m_word.IsEmpty())
		return;

	// The constructor's help after a `new`, the member's after a dot, a written function's parameters — the tab's
	// answer (ibProtocol::ibView::ibModule::ibRequest::CallTip).
	ibProtocolNode answer;
	if (!FetchAtCaret(ibProtocol::ibView::ibModule::ibRequest::CallTip, answer))
		return;

	const wxString description = answer.GetString(ibProtocol::ibView::ibModule::Text);
	if (!description.IsEmpty())
		m_ct.Show(screenPos, description);
}