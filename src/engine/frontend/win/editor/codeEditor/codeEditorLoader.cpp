////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : autoComplete loader  
////////////////////////////////////////////////////////////////////////////

#include "codeEditor.h"
#include "codeEditorParser.h"

#include "backend/metaCollection/partial/commonObject.h"

#include "frontend/docView/docView.h"

#include <wx/file.h>
#include <wx/stdpaths.h>
#include <cstring>

void ibCodeEditor::AddKeywordFromObject(const ibValue& vObject)
{
	if (vObject.GetType() != ibValueTypes::TYPE_EMPTY &&
		vObject.GetType() != ibValueTypes::TYPE_OLE) {
		for (long i = 0; i < vObject.GetNMethods(); i++) {
			if (vObject.HasRetVal(i)) {
				m_ac.Append(
					ibContentType::eFunction,
					vObject.GetMethodName(i),
					vObject.GetMethodHelper(i)
				);
			}
			else {
				m_ac.Append(
					ibContentType::eProcedure,
					vObject.GetMethodName(i),
					vObject.GetMethodHelper(i)
				);
			}
		}
		for (long i = 0; i < vObject.GetNProps(); i++) {
			// Scope-local props (ThisObject / ThisForm) must not
			// surface in autocomplete after a chain walk reaches a
			// foreign object (`Catalogs.Catalog1.CreateElement().` вЂ¦).
			if (vObject.IsPropScoped(i)) continue;
			m_ac.Append(
				ibContentType::eVariable,
				vObject.GetPropName(i),
				wxEmptyString
			);
		}
		ibRuntimeModuleDataObject* moduleDataObject = dynamic_cast<ibRuntimeModuleDataObject*>(vObject.GetRef());
		if (moduleDataObject != nullptr) {
			const ibValueMetaObjectModuleBase* computeModuleObject = moduleDataObject->GetMetaObject();
			if (computeModuleObject != nullptr) {
				ibParserModule cParser;
				if (cParser.ParseModule(computeModuleObject->GetModuleText())) {
					for (auto code : cParser.GetAllContent()) {
						if (code.m_eType == eExportVariable) {
							m_ac.Append(
								ibContentType::eExportVariable,
								code.m_name,
								wxEmptyString
							);
						}
						else if (code.m_eType == eExportProcedure) {
							m_ac.Append(
								ibContentType::eExportFunction,
								code.m_name,
								code.m_shortDescription
							);
						}
						else if (code.m_eType == eExportFunction) {
							m_ac.Append(
								ibContentType::eExportFunction,
								code.m_name,
								code.m_shortDescription
							);
						}
					}
				}
			}
		}
		ibValueManagerDataObject* managerDataObject = dynamic_cast<ibValueManagerDataObject*>(vObject.GetRef());
		if (managerDataObject != nullptr) {
			const ibValueMetaObjectCommonModule* computeManagerModule = managerDataObject->GetManagerModule();
			if (computeManagerModule != nullptr) {
				ibParserModule cParser;
				if (cParser.ParseModule(computeManagerModule->GetModuleText())) {
					for (auto code : cParser.GetAllContent()) {
						if (code.m_eType == eExportVariable) {
							m_ac.Append(ibContentType::eExportVariable, code.m_name, wxEmptyString);
						}
						else if (code.m_eType == eExportProcedure) {
							m_ac.Append(ibContentType::eExportFunction, code.m_name, code.m_shortDescription);
						}
						else if (code.m_eType == eExportFunction) {
							m_ac.Append(ibContentType::eExportFunction, code.m_name, code.m_shortDescription);
						}
					}
				}
			}
		}
	}
	else if (IsDebuggerEnterLoop() && m_document != nullptr) {
		ibPrecompileContext* currContext = m_precompileModule->GetCurrentContext();
		if (currContext && currContext->FindVariable(m_precompileModule->GetLastParentKeyword())) {
			m_ac.Cancel();
			const ibValueMetaObject* metaObject = m_document->GetMetaObject();
			wxASSERT(metaObject);
			OnEvaluateAutocomplete(
				metaObject->GetFileName(),
				metaObject->GetDocPath(),
				m_precompileModule->GetLastExpression(),
				m_precompileModule->GetLastKeyword(),
				GetCurrentPos()
			);
		}
	}
}

bool ibCodeEditor::PrepareExpression(unsigned int currPos, wxString& expression, wxString& keyword, wxString& currentWord, bool& outHasPoint)
{
	bool hasPoint = false, hasKeyword = false;
	for (unsigned int i = 0; i < m_precompileModule->GetLexems().size(); i++)
	{
		if (m_precompileModule->GetLexems()[i].m_lexType == IDENTIFIER)
		{
			if (hasPoint) expression += m_precompileModule->GetLexems()[i].m_valData.GetString();
			else expression = m_precompileModule->GetLexems()[i].m_valData.GetString();

			currentWord = m_precompileModule->GetLexems()[i].m_valData.GetString();

			if (i < m_precompileModule->GetLexems().size() - 1) {
				if (m_precompileModule->GetLexems()[i + 1].m_numString >= currPos)
					break;
				const ibLexem& lex = m_precompileModule->GetLexems()[i + 1];
				if (lex.m_lexType == DELIMITER && lex.m_numData == '(')
					expression = wxEmptyString;
				if (lex.m_lexType == DELIMITER && lex.m_numData == '(' && !hasPoint)
					keyword = currentWord;

				if (lex.m_lexType != ENDPROGRAM)
					hasPoint = lex.m_lexType == DELIMITER && lex.m_numData == '.';
			}

			hasKeyword = hasKeyword ? i == m_precompileModule->GetLexems().size() - 1 : false;
		}
		else if (m_precompileModule->GetLexems()[i].m_lexType == KEYWORD && m_precompileModule->GetLexems()[i].m_numData == KEY_NEW)
		{
			expression = wxEmptyString; currentWord = wxEmptyString;
			keyword = m_precompileModule->GetLexems()[i].m_valData.GetString(); hasKeyword = true;
		}
		else if (m_precompileModule->GetLexems()[i].m_lexType == CONSTANT)
		{
			// Special-keyword contexts where the autocomplete dropdown
			// should be filtered by the literal currently being typed
			// inside the call's argument string — e.g.
			// showCommonForm("...|") completes against form names.
			//
			// Outside those contexts a literal MUST NOT leak into
			// currentWord — otherwise an unrelated string constant
			// earlier in the source (var = "hello"; <caret>) would
			// poison the autocomplete filter and hide everything.
			const bool inSpecialCall =
				stringUtils::CompareString(keyword, wxT("type"))
				|| stringUtils::CompareString(keyword, wxT("showCommonForm"))
				|| (stringUtils::CompareString(keyword, wxT("getCommonForm")) && !hasPoint);

			if (inSpecialCall) {
				currentWord = m_precompileModule->GetLexems()[i].m_valData.GetString();
				hasKeyword = true;
			}
			else {
				currentWord = wxEmptyString;
				hasKeyword = false;
			}
		}
		else if (m_precompileModule->GetLexems()[i].m_lexType == DELIMITER
			&& m_precompileModule->GetLexems()[i].m_numData == '.')
		{
			if (!expression.IsEmpty())
				expression += '.';

			currentWord = wxEmptyString; hasPoint = true; hasKeyword = false;
		}
		else
		{
			if (m_precompileModule->GetLexems()[i].m_lexType != ENDPROGRAM) {
				expression = wxEmptyString; currentWord = wxEmptyString;
			}

			hasKeyword = false;
		}

		if (i < m_precompileModule->GetLexems().size() - 1 &&
			m_precompileModule->GetLexems()[i + 1].m_numString >= currPos) break;
	}

	outHasPoint = hasPoint; return hasKeyword;
}

void ibCodeEditor::PrepareTooTipExpression(unsigned int currPos, wxString& expression, wxString& currentWord, bool& outHasPoint)
{
	bool hasPoint = false;

	for (unsigned int i = 0; i < m_precompileModule->GetLexems().size(); i++)
	{
		if (m_precompileModule->GetLexems()[i].m_numString > currPos
			&& !hasPoint) break;

		if (m_precompileModule->GetLexems()[i].m_lexType == IDENTIFIER)
		{
			if (hasPoint) expression += m_precompileModule->GetLexems()[i].m_valData.GetString();
			else expression = m_precompileModule->GetLexems()[i].m_valData.GetString();

			currentWord = m_precompileModule->GetLexems()[i].m_valData.GetString();

			if (i < m_precompileModule->GetLexems().size() - 1) {
				const ibLexem& lex = m_precompileModule->GetLexems()[i + 1];
				if (lex.m_lexType == DELIMITER && lex.m_numData == '(')
					expression = wxEmptyString;
				hasPoint = lex.m_lexType == DELIMITER && lex.m_numData == '.';
			}
			else hasPoint = false;
		}
		else if (m_precompileModule->GetLexems()[i].m_lexType == DELIMITER
			&& m_precompileModule->GetLexems()[i].m_numData == '.')
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
	const bool isCES = (ibCompileCode::GetCodeStyle() == CODE_CES);
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
		if (ibCompileCode::GetCodeStyle() != CODE_CES) {
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
	int wordEndPos = WordEndPosition(currentPos, false);

	// Display the autocompletion list
	int lenEntered = currentPos - wordStartPos;

	wxString expression, keyword, currentWord; bool hasPoint = true;

	if (m_ct.Active())
		m_ct.Cancel();

	const bool hasKeyword = PrepareExpression(realPos, expression, keyword, currentWord, hasPoint);

	// User stands AT a word boundary (Ctrl+Space at the very start of an
	// identifier, or in trailing whitespace). PrepareExpression greedily
	// captures the full identifier from the lex stream which then lands as
	// the Append filter — and `Find("MESSAGE")` rejects every system keyword
	// like `If`, `Then`, etc. Force-clear currentWord here so the dropdown
	// shows all candidates; the user filters live by typing forward.
	if (lenEntered == 0)
		currentWord = wxEmptyString;

	if (!hasKeyword) {
		m_ac.Start(currentWord, currentPos, lenEntered, TextHeight(GetCurrentLine()));
		if (hasPoint) LoadIntelliList();
		else          LoadSysKeyword();
	}
	else {
		m_ac.Start(currentWord, currentPos, lenEntered, TextHeight(GetCurrentLine()));
		LoadFromKeyWord(keyword);
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
		SetToolTip(nullptr); return;
	}

	const ibValueMetaObject* metaObject = m_document->GetMetaObject();
	wxASSERT(metaObject);
	OnEvaluateToolTip(
		metaObject->GetFileName(),
		metaObject->GetDocPath(),
		expression
	);
}

void ibCodeEditor::LoadCallTip()
{
	// Find the word start
	int currentPos = GetRealPosition();

	wxString expression, keyword, currentWord, sDescription; bool hasPoint = true;

	if (!PrepareExpression(currentPos, expression, keyword, currentWord, hasPoint)) {
		if (hasPoint) {
			m_precompileModule->SetCurrentPos(GetRealPosition());
			//Collect text
			if (m_precompileModule->Compile()) {
				ibValue vObject = m_precompileModule->GetComputeValue();
				for (long i = 0; i < vObject.GetNMethods(); i++) {
					wxString sMethod = vObject.GetMethodName(i);
					if (stringUtils::CompareString(sMethod, currentWord)) {
						sDescription = vObject.GetMethodHelper(i);
						break;
					}
				}

				ibRuntimeModuleDataObject* moduleDataObject = dynamic_cast<ibRuntimeModuleDataObject*>(vObject.GetRef());
				if (moduleDataObject) {
					const ibValueMetaObjectModuleBase* computeModuleObject = moduleDataObject->GetMetaObject();
					if (computeModuleObject) {
						ibParserModule cParser;
						if (cParser.ParseModule(computeModuleObject->GetModuleText())) {
							for (auto code : cParser.GetAllContent()) {
								if (code.m_eType == eExportProcedure || code.m_eType == eExportFunction) {
									if (stringUtils::CompareString(code.m_name, currentWord)) {
										sDescription = code.m_shortDescription;
										break;
									}
								}
							}
						}
					}
				}

				ibValueManagerDataObject* managerDataObject = dynamic_cast<ibValueManagerDataObject*>(vObject.GetRef());
				if (managerDataObject) {
					const ibValueMetaObjectCommonModule* computeManagerModule = managerDataObject->GetManagerModule();
					if (computeManagerModule) {
						ibParserModule cParser;
						if (cParser.ParseModule(computeManagerModule->GetModuleText())) {
							for (auto code : cParser.GetAllContent()) {
								if (stringUtils::CompareString(code.m_name, currentWord)) {
									sDescription = code.m_shortDescription;
									break;
								}
							}
						}
					}
				}
			}
		}
		else
		{
			//Collect text
			m_precompileModule->SetCurrentPos(GetRealPosition());

			if (m_precompileModule->Compile()) {
				ibPrecompileContext* rootContext = m_precompileModule->GetContext();
				for (auto function : rootContext->m_functions) {
					ibPrecompileFunction* functionContext = function.second;
					if (stringUtils::CompareString(function.first, currentWord)) {
						sDescription = functionContext->m_shortDescription;
						break;
					}
				}
			}
		}
	}
	else {

		if (stringUtils::CompareString(keyword, wxT("new"))) {
			if (ibValue::IsRegisterCtor(expression)) {
				const ibCtorAbstractType* objectValueAbstract =
					ibValue::GetAvailableCtor(expression);
				ibValue* newObject = objectValueAbstract->CreateObject();
				ibValue::ibMemberTable* methodHelper = newObject->GetPMethods();
				if (methodHelper != nullptr) {
					for (long idx = 0; idx < methodHelper->GetNConstructors(); idx++) {
						sDescription = methodHelper->GetConstructorHelper(idx);
					}
				}
				wxDELETE(newObject);
			}
		}
	}

	if (!sDescription.IsEmpty()) {
		m_ct.Show(currentPos, sDescription);
	}

	m_precompileModule->Clear();
}

void ibCodeEditor::LoadSysKeyword()
{
	m_precompileModule->SetCurrentPos(GetRealPosition());

	for (int i = 0; i < LastKeyWord; i++) {
		m_ac.Append(ibContentType::eVariable,
			s_listKeyWord[i].m_strKeyWord,
			s_listKeyWord[i].m_strShortDescription
		);
	}

	if (m_precompileModule->Compile()) {
		ibPrecompileContext* rootContext = m_precompileModule->GetContext();
		const int caretPos = (int)GetRealPosition();
		for (const auto& variable : rootContext->m_variables) {
			const ibPrecompileVariable& v = variable.second;
			if (v.m_isTempVar)
				continue;
			// declPos > caret → declared below current line; not yet visible.
			if (v.m_declPos > caretPos)
				continue;
			m_ac.Append(v.m_isExport ?
				ibContentType::eExportVariable : ibContentType::eVariable, v.m_realName, wxEmptyString
			);
		}

		for (auto function : rootContext->m_functions) {
			ibPrecompileFunction* functionContext = function.second;
			if (functionContext->m_context) {
				if (functionContext->m_context->m_returnKind == RETURN_FUNCTION) {
					m_ac.Append(functionContext->m_isExport ? ibContentType::eExportFunction : ibContentType::eFunction, functionContext->m_realName, functionContext->m_shortDescription);
				}
				else {
					m_ac.Append(functionContext->m_isExport ? ibContentType::eExportProcedure : ibContentType::eProcedure, functionContext->m_realName, functionContext->m_shortDescription);
				}
			}
			else {
				m_ac.Append(functionContext->m_isExport ? ibContentType::eExportFunction : ibContentType::eFunction, functionContext->m_realName, functionContext->m_shortDescription);
			}

			if (m_precompileModule->GetCurrentContext() && m_precompileModule->GetCurrentContext() == functionContext->m_context) {
				for (const auto& variable : m_precompileModule->GetCurrentContext()->m_variables) {
					const ibPrecompileVariable& v = variable.second;
					if (v.m_isTempVar)
						continue;
					// Same declared-above-caret gate as for root context. Function
					// parameters keep declPos=0 so they always show up; only
					// in-body `var x` / implicit `x = expr` declarations are
					// position-gated.
					if (v.m_declPos > caretPos)
						continue;
					m_ac.Append(v.m_isExport ?
						ibContentType::eExportVariable : ibContentType::eVariable, v.m_realName, wxEmptyString
					);
				}
			}
		}
	}

	m_precompileModule->Clear();
}

void ibCodeEditor::LoadIntelliList()
{
	m_precompileModule->SetCurrentPos(GetRealPosition());
	m_precompileModule->SetCalcValue(true);

	if (m_precompileModule->Compile())
		AddKeywordFromObject(m_precompileModule->GetComputeValue());

	m_precompileModule->SetCalcValue(false);
	m_precompileModule->Clear();
}

#include "backend/metaData.h"
#include "backend/objCtor.h"

void ibCodeEditor::LoadFromKeyWord(const wxString& keyword)
{
	if (stringUtils::CompareString(keyword, wxT("new"))) {
		for (auto class_obj : ibValue::GetListCtorsByType(ibCtorObjectType::ibCtorObjectType_object_value))
			m_ac.Append(ibContentType::eVariable, class_obj->GetClassName(), wxEmptyString);
	}
	else if (stringUtils::CompareString(keyword, wxT("type")))
	{
		for (auto class_obj : ibValue::GetListCtorsByType(ibCtorObjectType::ibCtorObjectType_object_primitive))
			m_ac.Append(ibContentType::eVariable, class_obj->GetClassName(), wxEmptyString);

		for (auto class_obj : ibValue::GetListCtorsByType(ibCtorObjectType::ibCtorObjectType_object_value))
			m_ac.Append(ibContentType::eVariable, class_obj->GetClassName(), wxEmptyString);

		for (auto class_obj : ibValue::GetListCtorsByType(ibCtorObjectType::ibCtorObjectType_object_control))
			m_ac.Append(ibContentType::eVariable, class_obj->GetClassName(), wxEmptyString);

		for (auto class_obj : ibValue::GetListCtorsByType(ibCtorObjectType::ibCtorObjectType_object_system))
			m_ac.Append(ibContentType::eVariable, class_obj->GetClassName(), wxEmptyString);

		for (auto class_obj : ibValue::GetListCtorsByType(ibCtorObjectType::ibCtorObjectType_object_enum))
			m_ac.Append(ibContentType::eVariable, class_obj->GetClassName(), wxEmptyString);

		if (m_document) {
			const ibValueMetaObject* metaObject = m_document->GetMetaObject();
			if (metaObject) {
				const ibMetaData* metaData = metaObject->GetMetaData();
				wxASSERT(metaData);

				for (auto class_obj : metaData->GetListCtorsByType(ibCtorObjectMetaType::ibCtorObjectMetaType_Object))
					m_ac.Append(ibContentType::eVariable, class_obj->GetClassName(), wxEmptyString);
				for (auto class_obj : metaData->GetListCtorsByType(ibCtorObjectMetaType::ibCtorObjectMetaType_Reference))
					m_ac.Append(ibContentType::eVariable, class_obj->GetClassName(), wxEmptyString);
				for (auto class_obj : metaData->GetListCtorsByType(ibCtorObjectMetaType::ibCtorObjectMetaType_List))
					m_ac.Append(ibContentType::eVariable, class_obj->GetClassName(), wxEmptyString);
				for (auto class_obj : metaData->GetListCtorsByType(ibCtorObjectMetaType::ibCtorObjectMetaType_Manager))
					m_ac.Append(ibContentType::eVariable, class_obj->GetClassName(), wxEmptyString);
				for (auto class_obj : metaData->GetListCtorsByType(ibCtorObjectMetaType::ibCtorObjectMetaType_Selection))
					m_ac.Append(ibContentType::eVariable, class_obj->GetClassName(), wxEmptyString);
			}
		}
	}
	else if (stringUtils::CompareString(keyword, wxT("showCommonForm"))
		|| stringUtils::CompareString(keyword, wxT("getCommonForm")))
	{
		const ibValueMetaObject* metaObject = m_document->GetMetaObject();
		wxASSERT(metaObject);
		const ibMetaData* metaData = metaObject->GetMetaData();
		wxASSERT(metaData);

		for (const auto object : metaData->GetAnyArrayObject(g_metaCommonFormCLSID))
			m_ac.Append(ibContentType::eVariable, object->GetName(), wxEmptyString);
	}
}

#include "backend/fileSystem/fs.h"

void ibCodeEditor::ShowAutoComplete(const ibDebugAutoCompleteData& autoCompleteData)
{
	m_ac.Cancel();

	for (unsigned int i = 0; i < autoCompleteData.m_arrVar.size(); i++) {
		m_ac.Append(ibContentType::eVariable, autoCompleteData.m_arrVar[i].m_variableName, wxEmptyString);
	}

	for (unsigned int i = 0; i < autoCompleteData.m_arrMeth.size(); i++) {
		m_ac.Append(autoCompleteData.m_arrMeth[i].m_methodRet ? ibContentType::eFunction : ibContentType::eProcedure,
			autoCompleteData.m_arrMeth[i].m_methodName,
			autoCompleteData.m_arrMeth[i].m_methodHelper
		);
	}

	m_ac.Start(autoCompleteData.m_keyword,
		autoCompleteData.m_currentPos,
		autoCompleteData.m_keyword.Length(),
		TextHeight(GetCurrentLine())
	);

	wxPoint position = PointFromPosition(autoCompleteData.m_currentPos);
	position.y += TextHeight(GetCurrentLine());
	m_ac.Show(position);
}