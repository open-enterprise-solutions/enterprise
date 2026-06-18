////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : parser for autocomplete 
////////////////////////////////////////////////////////////////////////////

#include "codeEditorParser.h"

#pragma warning(disable : 4018)

ibParserModule::ibParserModule() = default;

bool ibParserModule::ParseModule(const wxString& sModule)
{
	m_content.clear();

	// 1. Tokenize the source into the lexem stream.
	Load(sModule);

	try {
		PrepareLexem();
	}
	catch (const ibBackendException&)
	{
		return false;
	};

	ibLexem lex;

	while ((lex = ExpectLexem()).m_lexType != ERRORTYPE)
	{
		// Skip the IF condition: walk to its THEN.
		if (lex.m_lexType == KEYWORD && lex.m_numData == KEY_IF)
		{
			while (m_cursor + 1 < m_listLexem.size())
			{
				lex = ExpectLexem();
				if (lex.m_lexType == KEYWORD && lex.m_numData == KEY_THEN) break;
			}

			lex = ExpectLexem();
		}

		// Skip the WHILE header: walk to its DO.
		if (lex.m_lexType == KEYWORD && lex.m_numData == KEY_WHILE)
		{
			while (m_cursor + 1 < m_listLexem.size())
			{
				lex = ExpectLexem();
				if (lex.m_lexType == KEYWORD && lex.m_numData == KEY_DO) break;
			}
			lex = ExpectLexem();
		}

		// Skip a ternary expression: walk to the closing ';'.
		if (lex.m_lexType == DELIMITER && lex.m_numData == '?')
		{
			while (m_cursor + 1 < m_listLexem.size())
			{
				lex = ExpectLexem();
				if (lex.m_lexType == DELIMITER && lex.m_numData == ';') break;
			}
			lex = ExpectLexem();
		}

		// Variable declaration: collect names + optional array size + export flag.
		if (lex.m_lexType == KEYWORD && lex.m_numData == KEY_VAR) {
			while (m_cursor + 1 < m_listLexem.size()) {
				wxString name = ExpectIdentifier(true);
				int nArrayCount = -1;
				if (IsNextDelimeter('[')) { // this is an array declaration
					nArrayCount = 0;
					ExpectDelimeter('[');
					if (!IsNextDelimeter(']')) {
						ibValue vConst = ExpectConstant();
						if (vConst.GetType() != ibValueTypes::TYPE_NUMBER || vConst.GetNumber() < 0)
							continue;
						nArrayCount = vConst.GetInteger();
					}
					ExpectDelimeter(']');
				}

				bool isExport = false;

				if (IsNextKeyWord(KEY_PUBLIC))
				{
					ExpectKeyword(KEY_PUBLIC);
					isExport = true;
				}
				else if (IsNextKeyWord(KEY_PRIVATE))   ExpectKeyword(KEY_PRIVATE);
				else if (IsNextKeyWord(KEY_PROTECTED)) ExpectKeyword(KEY_PROTECTED);

				if (IsNextDelimeter('='))// initial initialization - works only inside the text of modules (but not re-declaring procedures and functions)
				{
					if (nArrayCount >= 0) ExpectDelimeter(',');//Error!
					ExpectDelimeter('=');
				}

				ibModuleElement data;
				data.m_name = name;
				data.m_lineStart = lex.m_numLine;
				data.m_lineEnd = lex.m_numLine;
				data.m_imageIndex = 358;

				if (isExport)
					data.m_eType = ibContentType::eExportVariable;
				else data.m_eType = ibContentType::eVariable;

				m_content.push_back(data);

				if (!IsNextDelimeter(',')) break;
				ExpectDelimeter(',');
			}
		}

		// Function / procedure declaration: pull header text + collect params.
		if (lex.m_lexType == KEYWORD && (lex.m_numData == KEY_FUNCTION || lex.m_numData == KEY_PROCEDURE)) {
			bool isFunction = lex.m_numData == KEY_FUNCTION;

			// Anonymous lambda detection: keyword followed directly by `(`
			// (no identifier between them) means this is Function/Procedure
			// used as expression — typically `var x = Function(args) ...`.
			// Anonymous bodies aren't navigable by name, so skip the body
			// without registering a content entry. Depth counter handles
			// nested anonymous bodies (lambda inside lambda) — every
			// KEY_FUNCTION / KEY_PROCEDURE bumps depth, every
			// KEY_ENDFUNCTION / KEY_ENDPROCEDURE drops it.
			if (IsNextDelimeter('(')) {
				int depth = 1;
				while (m_cursor < (m_listLexem.size() - 1)) {
					const ibLexem& nl = m_listLexem[m_cursor + 1];
					if (nl.m_lexType == KEYWORD) {
						if (nl.m_numData == KEY_FUNCTION || nl.m_numData == KEY_PROCEDURE) {
							depth++;
						}
						else if (nl.m_numData == KEY_ENDFUNCTION || nl.m_numData == KEY_ENDPROCEDURE) {
							if (--depth == 0) {
								(void)ExpectLexem(); // consume the matching end
								break;
							}
						}
					}
					(void)ExpectLexem();
				}
				continue;
			}

			// pull out the text of the function declaration
			lex = PreviewGetLexem();
			wxString shortDescription;
			int nRes = m_strBuffer.find('\n', lex.m_numString);
			if (nRes >= 0) {
				shortDescription = m_strBuffer.substr(lex.m_numString, nRes - lex.m_numString - 1);
				nRes = shortDescription.find_first_of('/');
				if (nRes > 0)
				{
					if (shortDescription[nRes - 1] == '/') {// so this is a comment
						shortDescription = shortDescription.substr(nRes + 1);
					}
				}
				else
				{
					nRes = shortDescription.find_first_of(')');
					shortDescription = shortDescription.substr(0, nRes + 1);
				}
			}

			wxString strFuncName = ExpectIdentifier(true);

			// compile the list of formal parameters + register them as local
			ExpectDelimeter('(');
			if (!IsNextDelimeter(')'))
			{
				while (m_cursor + 1 < m_listLexem.size())
				{
					if (IsNextKeyWord(KEY_VAL))
					{
						ExpectKeyword(KEY_VAL);
					}

					/*wxString name =*/ (void)ExpectIdentifier(true);

					if (IsNextDelimeter('['))// this is an array
					{
						ExpectDelimeter('[');
						ExpectDelimeter(']');
					}
					else if (IsNextDelimeter('='))
					{
						ExpectDelimeter('=');
						ibValue vConstant = ExpectConstant();
					}

					if (IsNextDelimeter(')')) break;

					ExpectDelimeter(',');
				}
			}

			ExpectDelimeter(')');

			bool isExport = false;

			if (IsNextKeyWord(KEY_PUBLIC))
			{
				ExpectKeyword(KEY_PUBLIC); isExport = true;
			}
			else if (IsNextKeyWord(KEY_PRIVATE))   ExpectKeyword(KEY_PRIVATE);
			else if (IsNextKeyWord(KEY_PROTECTED)) ExpectKeyword(KEY_PROTECTED);

			ibModuleElement data;
			data.m_name = strFuncName;
			data.m_shortDescription = shortDescription;
			data.m_lineStart = lex.m_numLine;
			data.m_lineEnd = lex.m_numLine;

			if (isFunction) {
				data.m_imageIndex = 353;
				if (isExport) {
					data.m_eType = ibContentType::eExportFunction;
				}
				else {
					data.m_eType = ibContentType::eFunction;
				}
			}
			else {
				data.m_imageIndex = 352;
				if (isExport) { data.m_eType = ibContentType::eExportProcedure; }
				else { data.m_eType = ibContentType::eProcedure; }
			}

			// Body walk — depth-aware so nested lambdas
			// (`Function(...) ... EndFunction` as an inline expression
			// inside this body) don't terminate the outer's end-search
			// prematurely on their own EndFunction. Pre-Phase A this
			// could only happen if a programmer wrote `Function` in a
			// declaration mid-body (which CompileBlock rejected anyway);
			// with anonymous lambdas as expressions it's now legitimate.
			int depth = 1;
			while (m_cursor < (m_listLexem.size() - 1)) {

				if (IsNextKeyWord(KEY_ENDFUNCTION)) {
					if (--depth == 0) {
						data.m_lineEnd = m_listLexem[m_cursor + 1].m_numLine;
						ExpectKeyword(KEY_ENDFUNCTION); break;
					}
					ExpectKeyword(KEY_ENDFUNCTION);
					continue;
				}
				else if (IsNextKeyWord(KEY_ENDPROCEDURE)) {
					if (--depth == 0) {
						data.m_lineEnd = m_listLexem[m_cursor + 1].m_numLine;
						ExpectKeyword(KEY_ENDPROCEDURE); break;
					}
					ExpectKeyword(KEY_ENDPROCEDURE);
					continue;
				}
				else if (IsNextKeyWord(KEY_FUNCTION) || IsNextKeyWord(KEY_PROCEDURE)) {
					depth++;
				}

				lex = ExpectLexem();
			}

			m_content.push_back(data);
		}
	}

	if (m_cursor + 1 < m_listLexem.size() - 1)
		return false;
	return true;
}

/**
 * GetLexem
 *   Advance to the next lexem and return it. Returns gs_nullLexem if
 *   the cursor is past the end of the list.
 */
const ibLexem& ibParserModule::GetLexem()
{
	if (m_cursor + 1 < m_listLexem.size())
		return m_listLexem[++m_cursor];
	return gs_nullLexem;
}

/**
 * PreviewGetLexem
 *   Peek the next non-trivial lexem without advancing the cursor.
 *   Skips both ';' and '\n' delimiters — same designer-side behaviour
 *   as ibPrecompileCode::PreviewGetLexem.
 */
const ibLexem& ibParserModule::PreviewGetLexem()
{
	while (true) {
		const ibLexem& lex = GetLexem();
		if (!(lex.m_lexType == DELIMITER && (lex.m_numData == ';' || lex.m_numData == '\n'))) {
			m_cursor--;
			return lex;
		}
	}
	return gs_nullLexem;
}

/**
 * ExpectLexem
 *   Same as GetLexem; ERRORTYPE is treated silently because the
 *   parser walk only collects module elements and doesn't surface
 *   compile errors to the user.
 */
const ibLexem& ibParserModule::ExpectLexem()
{
	return GetLexem();
}

/**
 * ExpectDelimeter
 *   Consume lexems until the matching delimiter is found, or the lexem
 *   list is exhausted.
 */
void ibParserModule::ExpectDelimeter(const wxUniChar& c)
{
	while (m_cursor + 1 < m_listLexem.size()) {
		const ibLexem& lex = ExpectLexem();
		if (lex.m_lexType == DELIMITER && c == lex.m_numData)
			return;
	}
}
/**
 * IsNextDelimeter
 *   Predicate: is the next lexem the given delimiter? Does not advance.
 */
bool ibParserModule::IsNextDelimeter(const wxUniChar& c)
{
	if (m_cursor + 1 < m_listLexem.size()) {
		const ibLexem& lex = m_listLexem[m_cursor + 1];
		if (lex.m_lexType == DELIMITER && c == lex.m_numData)
			return true;
	}
	return false;
}

/**
 * IsNextKeyWord
 *   Predicate: is the next lexem the given keyword? Does not advance.
 */
bool ibParserModule::IsNextKeyWord(int nKey)
{
	if (m_cursor + 1 < m_listLexem.size()) {
		const ibLexem& lex = m_listLexem[m_cursor + 1];
		if (lex.m_lexType == KEYWORD && lex.m_numData == nKey)
			return true;
	}
	return false;
}

/**
 * ExpectKeyword
 *   Consume lexems until the given keyword is matched, or the lexem
 *   list is exhausted.
 */
void ibParserModule::ExpectKeyword(int nKey)
{
	ibLexem lex = ExpectLexem();
	while (!(lex.m_lexType == KEYWORD && lex.m_numData == nKey)) {
		if (m_cursor + 1 >= m_listLexem.size())
			break;
		lex = ExpectLexem();
	}
}

/**
 * ExpectIdentifier
 *   Consume the next lexem as an identifier. Returns the identifier
 *   string (real-cased version when strRealName=true), or empty
 *   string if the next lexem is not an identifier.
 */
wxString ibParserModule::ExpectIdentifier(bool strRealName)
{
	const ibLexem& lex = ExpectLexem();

	if (lex.m_lexType != IDENTIFIER) {
		if (strRealName && lex.m_lexType == KEYWORD)
			return lex.m_strData;
		return wxEmptyString;
	}

	if (strRealName)
		return lex.m_valData.GetString();
	else
		return lex.m_strData;
}

/**
 * ExpectConstant
 *   Consume the next lexem as a (possibly signed) numeric constant.
 *   Handles unary +/- prefix and flips the sign on negation.
 */
ibValue ibParserModule::ExpectConstant()
{
	ibLexem lex;
	int sign = 0;
	if (IsNextDelimeter('-') || IsNextDelimeter('+')) {
		sign = 1;
		if (IsNextDelimeter('-'))
			sign = -1;
		lex = ExpectLexem();
	}

	lex = ExpectLexem();

	if (lex.m_lexType != CONSTANT)
		return lex.m_valData;

	if (sign) {
		// check that the constant is of numeric type	
		if (lex.m_valData.GetType() != ibValueTypes::TYPE_NUMBER)
			return lex.m_valData;
		// change sign for minus
		if (sign == -1)
			lex.m_valData.m_fData = -lex.m_valData.m_fData;
	}
	return lex.m_valData;
}

