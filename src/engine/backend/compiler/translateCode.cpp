////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, 2C-team
//	Description : translate module 
////////////////////////////////////////////////////////////////////////////

#include "translateCode.h"

//////////////////////////////////////////////////////////////////////
//                           Constants
//////////////////////////////////////////////////////////////////////

//empty lexem  
const ibLexem gs_nullLexem = {};

const ibTranslateCode::ibDefineCollection ibTranslateCode::ms_listDefine; //root of every module's define chain — immutable, see translateCode.h

// (s_listHelpDescription / s_listHashKeyword lived here too — two more process-wide
// maps that LoadKeyWords filled and nothing ever read. Removed.)

//////////////////////////////////////////////////////////////////////
// Global array
//////////////////////////////////////////////////////////////////////
struct ibKeyWords s_listKeyWord[] =
{
	{"If"},
	{"Then"},
	{"Else"},
	{"Elseif"},
	{"Endif"},
	{"For"},
	{"Foreach"},
	{"To"},
	{"In"},
	{"Do"},
	{"EndDo"},
	{"While"},
	{"GoTo"},
	{"Not"},
	{"And"},
	{"Or"},
	{"Mod"},
	{"Procedure"},
	{"EndProcedure"},
	{"Function"},
	{"EndFunction"},
	{"Public"},      // KEY_PUBLIC    — was "Export"; leading access modifier
	{"Private"},     // KEY_PRIVATE
	{"Protected"},   // KEY_PROTECTED
	{"Cached"},      // KEY_CACHED    — memoisation; combines with the three above
	{"Val"},
	{"Return"},
	{"Try"},
	{"Except"},
	{"Endtry"},
	{"Continue"},
	{"Break"},
	{"Raise"},
	{"Var"},

	//create object
	{"New"},

	//undefined type
	{"Undefined"},

	//null type
	{"Null"},

	//boolean type
	{"True"},
	{"False"},

	//preprocessor:
	{"#Define"},
	{"#Undef"},
	{"#Ifdef"},
	{"#Ifndef"},
	{"#Else"},
	{"#Endif"},
	{"#Region"},
	{"#EndRegion"},

	// === LINQ keywords ===
	// Order MUST match codeDef.h's KEY_FROM..KEY_RESTRICT enum block —
	// translator lookup is by index into s_listKeyWord.
	{"From"},
	{"Where"},
	{"Select"},
	{"OrderBy"},
	{"Ascending"},
	{"Descending"},
	{"Take"},
	{"Skip"},
	{"Distinct"},
	{"Join"},
	{"On"},
	{"Equals"},
	{"Group"},
	{"By"},
	{"Into"},
	{"Restrict"},
};

// THIS TABLE AND THE KEY_* ENUM ARE ONE THING IN TWO PLACES, and the translator
// looks a keyword up by INDEX — s_listKeyWord[KEY_MOD]. A comment asking the next
// person to keep them in step is not a guard: adding an enumerator without its
// string shifts every name after it by one, so `Mod` starts spelling `Procedure`
// and the failure surfaces as a mis-parsed module, nowhere near the edit.
static_assert(WXSIZEOF(s_listKeyWord) == LastKeyWord,
	"s_listKeyWord and the KEY_* enum (codeDef.h) must stay in lock-step: "
	"one entry per enumerator, in the same order.");

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

// The map folds case in its comparator (ibCaseFoldLess), so these are plain
// O(log n) lookups by the name as written — where the old code ran a linear
// std::find_if over the whole map, calling CompareString on each entry, for
// every identifier in every module.

void ibTranslateCode::ibDefineCollection::RemoveDef(const wxString& strName)
{
	m_defineList.erase(strName);
}

const ibLexemList* ibTranslateCode::ibDefineCollection::FindDefine(const wxString& strName) const
{
	// Iterative walk — the chain is a parent list, not a tree, so recursion bought
	// nothing and its depth guard was a function-local `static` counter: shared by
	// every thread, and left incremented when the throw below unwound past it.
	int nLevel = 0;
	for (const ibDefineCollection* scope = this; scope != nullptr; scope = scope->m_parentDefine) {
		if (++nLevel > MAX_OBJECTS_LEVEL) ibBackendCoreException::Error(_("Recursive module call (#3)"));
		auto iterator = scope->m_defineList.find(strName);
		if (iterator != scope->m_defineList.end()) return &iterator->second;
	}

	return nullptr;
}

void ibTranslateCode::ibDefineCollection::SetDefine(const wxString& strName, const ibLexemList* src)
{
	// Always OUR map. The previous version resolved the destination through the
	// chain-walking lookup, so redefining a name an ancestor held overwrote the
	// ANCESTOR's entry — a module rewriting its parent's #Define, and, the moment
	// anything seeded the static root, one session rewriting every other's.
	ibLexemList& dst = m_defineList[strName];
	if (src != nullptr)
		dst = *src;
	else
		dst.clear();
}

void ibTranslateCode::ibDefineCollection::SetDefine(const wxString& strName, const wxString& strValue)
{
	ibLexemList listLexem;

	if (strValue.length() > 0) {
		ibLexem Lex;
		Lex.m_lexType = CONSTANT;
		if (strValue[0] == wxT('-') || strValue[0] == wxT('+') || (strValue[0] >= wxT('0') && strValue[0] <= wxT('9'))) //digit
			Lex.m_valData.SetNumber(strValue);
		else
			Lex.m_valData.SetString(strValue);
		listLexem.push_back(Lex);
		SetDefine(strName, &listLexem);
	}
	else {
		SetDefine(strName, nullptr);
	}
}

//////////////////////////////////////////////////////////////////////
// ibLexem — out-of-line accessors (need ibTranslateCode definition)
//////////////////////////////////////////////////////////////////////

// These return a REFERENCE, so the fallback must be an object that outlives the call.
//
// They used to spell it `… ? m_translateCode->m_strX : wxString()`, which returns a
// reference to a DEAD TEMPORARY — and on every call, not just the null one: a conditional
// whose second operand is an lvalue and whose third is a prvalue yields a prvalue, so even
// the non-null branch was copied into a temporary first. The caller then read freed stack.
//
// MSVC survived it by accident (the temporary's stack slot happened to stay intact long
// enough for AddLineInfo to copy out of it); GCC reuses the memory immediately and every
// test that compiles a script segfaulted. A file-scope empty string keeps both operands
// lvalues, so the conditional stays an lvalue and the reference is real.
static const wxString s_emptyLexemString;

const wxString& ibLexem::GetModuleName() const {
	return m_translateCode ? m_translateCode->m_strModuleName : s_emptyLexemString;
}
const wxString& ibLexem::GetDocPath() const {
	return m_translateCode ? m_translateCode->m_strDocPath : s_emptyLexemString;
}
const wxString& ibLexem::GetFileName() const {
	return m_translateCode ? m_translateCode->m_strFileName : s_emptyLexemString;
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

ibTranslateCode::ibTranslateCode() : m_current_lex(this),
m_defineList(nullptr),
m_bAutoDeleteDefList(false),
m_nModePreparing(LEXEM_ADD)
{
}

ibTranslateCode::ibTranslateCode(const wxString& strModuleName, const wxString& strDocPath) : m_current_lex(this),
m_defineList(nullptr),
m_bAutoDeleteDefList(false),
m_nModePreparing(LEXEM_ADD),
m_strModuleName(strModuleName), m_strDocPath(strDocPath)
{
}

ibTranslateCode::ibTranslateCode(const wxString& strFileName) : m_current_lex(this),
m_defineList(nullptr),
m_bAutoDeleteDefList(false),
m_nModePreparing(LEXEM_ADD),
m_strFileName(strFileName)
{
}

ibTranslateCode::~ibTranslateCode()
{
	if (m_bAutoDeleteDefList) wxDELETE(m_defineList);
}

// `IsAllowedKey` is declared on ibTranslateCode but its body lives in
// compileCode.cpp — the gate needs the active code-style (gs_codeStyle,
// owned by ibCompileCode), and putting the body there keeps the include
// chain one-way (compile → translate, never the reverse). `IsKeyWord`
// below dispatches through it so lexer / highlighter / autocomplete /
// parser all inherit the CES-vs-VES filter from one place.

//////////////////////////////////////////////////////////////////////
// Translating
//////////////////////////////////////////////////////////////////////

/**
* Reset
* Purpose:
* Prepare variables to start compilation
* Return value:
* none
*/

void ibTranslateCode::Clear()
{
	m_strBuffer.clear();
	m_strBUFFER.clear();

	//m_listLexem.Clear();
	//m_listTranslateCode.Clear();

	if (m_defineList != nullptr) m_defineList->Clear();

#ifdef UTF8_LEXEM_TRANSLATE
	m_bufferSize = m_currentPos = m_currentLine = m_currentUtf8Pos = 0;
#else
	m_bufferSize = m_currentPos = m_currentLine = 0;
#endif // UTF8_LEXEM_TRANSLATE
}

/**
* Load
* Purpose:
* Load the buffer with source text + prepare variables for compilation
* Return value:
* none
*/

void ibTranslateCode::Load(const wxString& strCode)
{
	Clear();

	m_bufferSize = strCode.length();

	if (m_bufferSize > 0) {

		m_strBuffer.assign(strCode);
		m_strBUFFER.assign(strCode);

		std::transform(
			m_strBUFFER.begin(), m_strBUFFER.end(),
			m_strBUFFER.begin(),
#ifdef wxUSE_UNICODE
			[](const auto& c) { return ::towupper(c); }
#else
			[](const auto& c) { return ::toupper(c); }
#endif // wxUSE_UNICODE
		);

		// Capacity is reserved by the full-PrepareLexem path (after its
		// clear()) so we don't move stale lexems into a new buffer just
		// to drop them. Incremental PrepareLexem(line, ...) reuses the
		// existing capacity; it depends on the previous parse's lexems
		// being intact for its rewind/patch logic, so Load must not
		// touch m_listLexem.
	}
}

/**
* SetError
* Purpose:
* Remember the translation error and raise an exception
* Return value:
* The method does not return control!strCurWord
*/

void ibTranslateCode::SetError(int codeError, unsigned int currPos, const wxString& errorDesc) const
{
	unsigned int start_pos = 0;

	//look for the beginning of the string where the translation error message is returned
	for (unsigned int i = (currPos == m_strBuffer.length() ? currPos - 1 : currPos); i > 0; i--) {
		if (m_strBuffer[i] == wxT('\n')) {
			start_pos = i + 1; break;
		};
	}

	const int currLine = 1 + std::count(
		m_strBuffer.begin(), m_strBuffer.begin() + start_pos, wxT('\n'));

	ibTranslateCode::SetError(codeError,
		m_strFileName, m_strModuleName, m_strDocPath,
		currPos, currLine,
		errorDesc
	);
}

/**
* SkipSpaces
* Purpose:
* Skip all insignificant spaces from input buffer
* plus comments from input buffer redirect to output buffer
* Return value:
* NONE
*/

void ibTranslateCode::SkipSpaces() const
{
	unsigned int i = m_currentPos;
#ifdef UTF8_LEXEM_TRANSLATE
	unsigned int i_utf8 = m_currentUtf8Pos;
	unsigned int i_utf8_offset = i_utf8;
#endif	
	for (; i < m_bufferSize; i++) {
		const auto& c = m_strBuffer[i];
#ifdef UTF8_LEXEM_TRANSLATE
		i_utf8 = i_utf8_offset;
		i_utf8_offset = i_utf8;
		unsigned int i_utf8_step = 0;
		(void)SetUtf8CharOffset(c, i_utf8_step);
		i_utf8_offset += i_utf8_step;
#endif
		if (c != wxT(' ') && c != wxT('\t') && c != wxT('\n') && c != wxT('\r')) {
			if (c == wxT('/')) { //maybe it's a comment
				if (i + 1 < m_bufferSize) {
					if (m_strBuffer[i + 1] == wxT('/')) { //skip comments

#ifdef UTF8_LEXEM_TRANSLATE
						unsigned int j_utf8 = i_utf8;
						unsigned int j_utf8_offset = j_utf8;
#endif	
						for (unsigned int j = i; j < m_bufferSize; j++) {

							const auto& w = m_strBuffer[j];
#ifdef UTF8_LEXEM_TRANSLATE
							j_utf8 = j_utf8_offset;
							j_utf8_offset = j_utf8;
							unsigned int j_utf8_step = 0;
							(void)SetUtf8CharOffset(w, j_utf8_step);
							j_utf8_offset += j_utf8_step;
#endif
							m_currentPos = j;
#ifdef UTF8_LEXEM_TRANSLATE
							m_currentUtf8Pos = j_utf8;
#endif
							if (w == wxT('\n') || w == wxT('\r')) {
								//process next line
								SkipSpaces();
								return;
							}
						}
						i = m_currentPos + 1;
#ifdef UTF8_LEXEM_TRANSLATE
						i_utf8 = m_currentUtf8Pos + i_utf8_step;
#endif
					}
				}
			}
			m_currentPos = i;
#ifdef UTF8_LEXEM_TRANSLATE
			m_currentUtf8Pos = i_utf8;
#endif
			break;
		}
		else if (c == wxT('\n')) {
			m_currentLine++;
		}
	}

	if (i == m_bufferSize) {
		m_currentPos = i;
#ifdef UTF8_LEXEM_TRANSLATE
		m_currentUtf8Pos = i_utf8;
#endif
	}
}

/**
* IsByte
* Purpose:
* Check if the next byte (excluding spaces) is equal to
* the GIVEN byte
* Return value:
* true,false
*/

bool ibTranslateCode::IsByte(const wxUniChar& c) const
{
	SkipSpaces();
	if (m_currentPos >= m_bufferSize)
		return false;
	if (m_strBuffer[m_currentPos] == c)
		return true;
	return false;
}

/**
* GetByte
* Purpose:
* Get a byte from the sample (excluding spaces)
* if there is no such byte, an exception is thrown
* Return value:
* Byte from the buffer
*/

bool ibTranslateCode::GetByte(wxUniChar* c) const
{
	SkipSpaces();

	if (m_currentPos < m_bufferSize) {
		const auto& byte = m_strBuffer[m_currentPos++];
#ifdef UTF8_LEXEM_TRANSLATE
		SetUtf8CharOffset(byte, m_currentUtf8Pos);
#endif
		if (c != nullptr) *c = byte;
		return true;
	}

	SetError(ERROR_TRANSLATE_BYTE, m_currentPos);
	return false;
}

/**
* IsWord
* Purpose:
* Check (without changing the current cursor position)
* whether the next set of letters is a word (skipping spaces, etc.)
* Return value:
* true,false
*/

void ibTranslateCode::SkipLine() const
{
#ifdef UTF8_LEXEM_TRANSLATE
	unsigned int i_utf8 = m_currentUtf8Pos, i_utf8_offset = i_utf8;
#endif
	for (unsigned int i = m_currentPos; i < m_bufferSize; i++) {

		const auto& c = m_strBuffer[i];
#ifdef UTF8_LEXEM_TRANSLATE
		i_utf8 = i_utf8_offset;
		unsigned int i_utf8_step = 0;
		(void)SetUtf8CharOffset(c, i_utf8_step);
		i_utf8_offset = i_utf8 + i_utf8_step;
#endif
		m_currentPos = i;
#ifdef UTF8_LEXEM_TRANSLATE
		m_currentUtf8Pos = i_utf8;
#endif
		if (c == wxT('\n') || c == wxT('\r')) {
			SkipSpaces();   // steps over the break itself and counts the line
			return;
		}
	}

	m_currentPos = m_bufferSize;
#ifdef UTF8_LEXEM_TRANSLATE
	m_currentUtf8Pos = i_utf8_offset;
#endif
}

bool ibTranslateCode::IsWord() const
{
	SkipSpaces();
	if (m_currentPos < m_bufferSize) {
		const auto& c = m_strBuffer[m_currentPos];
#ifdef wxUSE_UNICODE
		if (((c == wxT('_')) || iswalpha(c) || (c == wxT('#'))) && (c != wxT('[') && c != wxT(']')))
#else 
		if (((c == wxT('_')) || isalpha(c) || (c == wxT('#'))) && (c != wxT('[') && c != wxT(']')))
#endif
			return true;
	}
	return false;
}

/**
* GetWord
* Purpose:
* Select the next word from the buffer
* if there is no word (i.e. the next set of letters is not a word), then an exception is thrown
* Parameter: getPoint
* true - consider the period as a component of the word (to obtain the constant number)
* Return value:
* Word from the buffer
*/

bool ibTranslateCode::GetWord(wxString* strWord, wxString* strRealName, bool realName, bool get_point) const
{
	SkipSpaces();

	if (m_currentPos >= m_bufferSize) {
		SetError(ERROR_TRANSLATE_WORD, m_currentPos);
		return false;
	}

	int next_pos = m_currentPos;
#ifdef UTF8_LEXEM_TRANSLATE
	int next_utf8_pos = m_currentUtf8Pos;
#endif

	if (strWord != nullptr) strWord->clear();

#ifdef UTF8_LEXEM_TRANSLATE
	unsigned int i_utf8 = m_currentUtf8Pos;
	unsigned int i_utf8_offset = i_utf8;
#endif

	for (unsigned int i = m_currentPos; i < m_bufferSize; i++) {
		const auto& c = m_strBuffer[i];
#ifdef UTF8_LEXEM_TRANSLATE
		i_utf8 = i_utf8_offset;
		i_utf8_offset = i_utf8;
		unsigned int i_utf8_step = 0;
		(void)SetUtf8CharOffset(c, i_utf8_step);
		i_utf8_offset += i_utf8_step;
#endif	
		// if array then break
		if (c == wxT('[') || c == wxT(']')) break;

		if ((c == wxT('_')) ||
#ifdef wxUSE_UNICODE
			iswalpha(c) || isdigit(c) ||
#else 
			isalpha(c) || isdigit(c) ||
#endif 
			(c == wxT('#') && i == m_currentPos) || //if the first # symbol is a special word
			(c == wxT('.') && get_point)) {

			if (c == wxT('.') && get_point)
				get_point = false; //the dot must appear only once

			next_pos = i + 1;
#ifdef UTF8_LEXEM_TRANSLATE
			next_utf8_pos = i_utf8 + i_utf8_step;
#endif
		}
		else break;

		if (strRealName != nullptr) strRealName->Append(c);
		if (strWord != nullptr) strWord->Append(realName ? c : m_strBUFFER[i]);
	}

	const unsigned int first_pos = m_currentPos;

	m_currentPos = next_pos;
#ifdef UTF8_LEXEM_TRANSLATE
	m_currentUtf8Pos = next_utf8_pos;
#endif

	if (first_pos == next_pos) {
		SetError(ERROR_TRANSLATE_WORD, first_pos);
		return false;
	}

	return true;
}

/**
* GetStrToEndLine
* ​​Purpose:
* Get the entire string to the end (character 13 or the end of the program code)
* String
*/

wxString ibTranslateCode::GetStrToEndLine() const
{
	unsigned int start_pos = m_currentPos;
	unsigned int i = m_currentPos;
#ifdef UTF8_LEXEM_TRANSLATE
	unsigned int i_utf8 = m_currentUtf8Pos;
	unsigned int i_utf8_offset = i_utf8;
#endif
	for (; i < m_bufferSize; i++) {
		const auto& c = m_strBuffer[i];
#ifdef UTF8_LEXEM_TRANSLATE
		i_utf8 = i_utf8_offset;
		i_utf8_offset = i_utf8;
		unsigned int i_utf8_step = 0;
		(void)SetUtf8CharOffset(c, i_utf8_step);
		i_utf8_offset += i_utf8_step;
#endif
		if (c == wxT('\r') || c == wxT('\n')) {
			i += 1;
#ifdef UTF8_LEXEM_TRANSLATE
			i_utf8 += i_utf8_step;
#endif
			if (c == wxT('\n')) m_currentLine++;
			break;
		}
	}
	m_currentPos = i;
#ifdef UTF8_LEXEM_TRANSLATE
	m_currentUtf8Pos = i_utf8;
#endif	
	return m_strBuffer.substr(start_pos, m_currentPos - start_pos);
}

/**
* IsNumber
* Purpose:
* Check (without changing the current cursor position)
* whether the next set of letters is a constant number (skipping spaces, etc.)
* Return value:
* true,false
*/

bool ibTranslateCode::IsNumber() const
{
	SkipSpaces();
	if (m_currentPos < m_bufferSize)
		return isdigit(m_strBuffer[m_currentPos]);
	return false;
}

/**
* GetNumber
* Purpose:
* Get a number from the selection
* if there is no number, an exception is thrown
* Return value:
* Number from the buffer
*/

bool ibTranslateCode::GetNumber(wxString* strNumber) const
{
	// 🛑 `return false`, NOT `return wxEmptyString` — this returned a POINTER from a bool function,
	// left over from when the signature was `wxString GetNumber()`. It compiles, and a non-null
	// pointer is TRUE, so both of these error paths were reporting SUCCESS. Every sibling Get* in
	// this file returns false here. (It went unnoticed because SetError above already records the
	// failure, so the compile still refused — but a caller reading the RESULT was told it worked.)
	if (!IsNumber()) {
		SetError(ERROR_TRANSLATE_NUMBER, m_currentPos);
		return false;
	}
	SkipSpaces();
	if (m_currentPos >= m_bufferSize) {
		SetError(ERROR_TRANSLATE_NUMBER, m_currentPos);
		return false;
	}
	unsigned int next_pos = m_currentPos, error_pos = m_currentPos, point_pos = 0;
#ifdef UTF8_LEXEM_TRANSLATE
	unsigned int next_utf8_pos = m_currentUtf8Pos;
#endif
	if (strNumber != nullptr) strNumber->clear();
#ifdef UTF8_LEXEM_TRANSLATE
	unsigned int i_utf8 = m_currentUtf8Pos;
	unsigned int i_utf8_offset = i_utf8;
#endif
	for (unsigned int i = m_currentPos; i < m_bufferSize; i++) {
		const auto& c = m_strBuffer[i];
#ifdef UTF8_LEXEM_TRANSLATE
		i_utf8 = i_utf8_offset;
		i_utf8_offset = i_utf8;
		unsigned int i_utf8_step = 0;
		(void)SetUtf8CharOffset(c, i_utf8_step);
		i_utf8_offset += i_utf8_step;
#endif
		if (isdigit(c) || (c == wxT('.'))) {
			if (c == wxT('.'))
				point_pos++; //dot must appear only once
			next_pos = i + 1;
#ifdef UTF8_LEXEM_TRANSLATE
			next_utf8_pos = i_utf8 + i_utf8_step;
#endif
		}
		else break;
		error_pos = i + 1;
		if (strNumber != nullptr) strNumber->Append(c);
	}

	const unsigned int first_pos = m_currentPos;

	m_currentPos = next_pos;
#ifdef UTF8_LEXEM_TRANSLATE
	m_currentUtf8Pos = next_utf8_pos;
#endif	

	if (first_pos == next_pos) {
		SetError(ERROR_TRANSLATE_NUMBER, first_pos);
		return false;
	}
	else if (IsWord() && !wxIsspace(m_strBuffer[next_pos])) {
		// Originally compared `!= ' '` — rejected `20\nselect` as
		// "number-glued-to-word" because newline isn't the literal
		// space char. IsWord() above calls SkipSpaces() (which advances
		// past ALL whitespace incl. \n / \t), so the IsWord check
		// might return true for a far-away identifier after a wide
		// whitespace gap. Use wxIsspace at next_pos so any whitespace
		// separator (space / tab / newline / CR) is accepted.
		SetError(ERROR_TRANSLATE_NUMBER, error_pos);
		return false;
	}
	else if (point_pos > 1) {
		SetError(ERROR_TRANSLATE_NUMBER, error_pos);
		return false;
	}

	return true;
}

/**
* IsString
* Purpose:
* Check if the following set of characters (excluding spaces) is a constant string enclosed in quotes
* Return value:
* true,false
*/

bool ibTranslateCode::IsString() const
{
	return IsByte(wxT('\"')) || IsByte(wxT('|'));
}

/**
* GetString
* Purpose:
* Get a string enclosed in quotes from the selection
* if there is no such string, an exception is thrown
* Return value:
* String from the buffer
*/

bool ibTranslateCode::GetString(wxString* strString) const
{
	if (!IsString()) {
		SetError(ERROR_TRANSLATE_STRING, m_currentPos);
		return false;
	}
	unsigned int count_char = 0; bool skip_space = false;
	SkipSpaces();
	if (m_currentPos >= m_bufferSize) {
		SetError(ERROR_TRANSLATE_WORD, m_currentPos);
		return false;
	}
	unsigned int next_pos = m_currentPos, error_pos = m_currentPos;
#ifdef UTF8_LEXEM_TRANSLATE
	unsigned int next_utf8_pos = m_currentUtf8Pos, error_utf8_pos = m_currentUtf8Pos;
#endif
	if (strString != nullptr) strString->clear();
#ifdef UTF8_LEXEM_TRANSLATE
	unsigned int i_utf8 = m_currentUtf8Pos;
	unsigned int i_utf8_offset = i_utf8;
#endif
	for (unsigned int i = m_currentPos; i < m_bufferSize; i++) {
		const auto& c = m_strBuffer[i];
#ifdef UTF8_LEXEM_TRANSLATE
		i_utf8 = i_utf8_offset;
		i_utf8_offset = i_utf8;
		unsigned int i_utf8_step = 0;
		(void)SetUtf8CharOffset(c, i_utf8_step);
		i_utf8_offset += i_utf8_step;
#endif	
		if (c == wxT('\n')) {
			// ⚠⚠ A CLOSED STRING ENDS AT ITS QUOTE. This branch exists for the CONTINUED string —
			//     "line one
			//     |line two"
			// — where the newline is part of the literal and the next line opens with `|`. When the
			// string has already closed (count_char == 2), the newline after it belongs to whatever
			// follows, and running this branch clobbered `next_pos` back to the string's own start,
			// leaving the lexer to resume INSIDE the literal it had just read. Everything after that
			// point tokenised as garbage and surfaced as a lexical error some lines later.
			//
			// It survived because it needs a string to be the LAST TOKEN ON A LINE: in script text a
			// quote is nearly always followed by `;` or `)`, and query text was only ever tested on
			// ONE line. The renderer prints a clause per line, so `WHERE\n\tCode = "A"\nGROUP BY …`
			// — an entirely ordinary query — could not be read back.
			if (count_char >= 2)
				break;
			if (strString != nullptr) strString->Append(wxT('\n'));
			next_pos = m_currentPos + 1;
#ifdef UTF8_LEXEM_TRANSLATE
			next_utf8_pos = m_currentUtf8Pos + 1;
#endif
			m_currentLine++;
			skip_space = true;
		}
		if (skip_space && c == wxT('|')) {
			skip_space = false;
			continue;
		}
		else if (skip_space && (c == wxT(' ') || c == wxT('\t') || c == wxT('\n') || c == wxT('\r'))) {
			continue;
		}
		else if (skip_space && (c != wxT(' ') && c != wxT('\t') && c != wxT('\n') && c != wxT('\r'))) {
			break;
		}
		error_pos = i + 1;
#ifdef UTF8_LEXEM_TRANSLATE
		error_utf8_pos = i_utf8 + i_utf8_step;
#endif
		if (count_char < 2) {
			next_pos = i + 1;
#ifdef UTF8_LEXEM_TRANSLATE
			next_utf8_pos = i_utf8 + i_utf8_step;
#endif
			if (c == wxT('\"')) {
				if (i != m_currentPos && i + 1 < m_bufferSize) {
					if (m_strBuffer[i + 1] == wxT('\"')) {
						if (strString != nullptr) strString->Append(wxT('\"'));
						i++;
						next_pos = i + 1;
#ifdef UTF8_LEXEM_TRANSLATE
						i_utf8++;
						next_utf8_pos = i_utf8 + 1;
#endif
						continue;
					}
				}
				count_char++;
				continue;
			}
		}
		else break;
		if (strString != nullptr) strString->Append(c);
	}

	const unsigned int first_pos = m_currentPos;

	m_currentPos = next_pos;
#ifdef UTF8_LEXEM_TRANSLATE
	m_currentUtf8Pos = next_utf8_pos;
#endif

	if (first_pos == next_pos) {
		SetError(ERROR_TRANSLATE_STRING, first_pos);
		return false;
	}
	else if (count_char < 2) {
		SetError(ERROR_TRANSLATE_STRING, error_pos);
		m_currentPos = error_pos;
#ifdef UTF8_LEXEM_TRANSLATE
		m_currentUtf8Pos = error_utf8_pos;
#endif
		return false;
	}

	return true;
}

/**
* IsDate
* Purpose:
* Check if the following set of characters (excluding spaces) is a constant date enclosed in apostrophes
* Return value:
* true,false
*/

bool ibTranslateCode::IsDate() const
{
	return IsByte(wxT('\''));
}

/**
* GetDate
* Purpose:
* Get a date from the selection enclosed in apostrophes
* if there is no such date, an exception is thrown
* Return value:
* The date from the buffer, enclosed in quotes
*/

bool ibTranslateCode::GetDate(wxString* strDate) const
{
	if (!IsDate()) {
		SetError(ERROR_TRANSLATE_DATE, m_currentPos);
		return false;
	}
	unsigned int count_char = 0;
	SkipSpaces();
	if (m_currentPos >= m_bufferSize) {
		SetError(ERROR_TRANSLATE_WORD, m_currentPos);
		return false;
	}
	unsigned int next_pos = m_currentPos, error_pos = m_currentPos;
#ifdef UTF8_LEXEM_TRANSLATE
	unsigned int next_utf8_pos = m_currentUtf8Pos, error_utf8_pos = m_currentUtf8Pos;
#endif
	if (strDate != nullptr) strDate->clear();
#ifdef UTF8_LEXEM_TRANSLATE
	unsigned int i_utf8 = m_currentUtf8Pos;
	unsigned int i_utf8_offset = i_utf8;
#endif
	for (unsigned int i = m_currentPos; i < m_bufferSize; i++) {
		const auto& c = m_strBuffer[i];
#ifdef UTF8_LEXEM_TRANSLATE
		i_utf8 = i_utf8_offset;
		i_utf8_offset = i_utf8;
		unsigned int i_utf8_step = 0;
		(void)SetUtf8CharOffset(c, i_utf8_step);
		i_utf8_offset += i_utf8_step;
#endif
		if (c == wxT('\n')) {
			next_pos = m_currentPos + 1;
#ifdef UTF8_LEXEM_TRANSLATE
			next_utf8_pos = m_currentUtf8Pos + i_utf8_step;
#endif
			m_currentLine++;
			break;
		}
		error_pos = i + 1;
#ifdef UTF8_LEXEM_TRANSLATE
		error_utf8_pos = i_utf8 + i_utf8_step;
#endif
		if (count_char < 2) {
			next_pos = i + 1;
#ifdef UTF8_LEXEM_TRANSLATE
			next_utf8_pos = i_utf8 + i_utf8_step;
#endif
			if (c == wxT('\'')) {
				count_char++;
				continue;
			}
		}
		else break;
		if (strDate != nullptr) strDate->Append(c);
	}

	const unsigned int first_pos = m_currentPos;

	m_currentPos = next_pos;
#ifdef UTF8_LEXEM_TRANSLATE
	m_currentUtf8Pos = next_utf8_pos;
#endif

	if (first_pos == next_pos) {
		SetError(ERROR_TRANSLATE_DATE, first_pos);
		return false;
	}
	else if (count_char < 2) {
		SetError(ERROR_TRANSLATE_DATE, error_pos);
		m_currentPos = error_pos;
#ifdef UTF8_LEXEM_TRANSLATE
		m_currentUtf8Pos = error_utf8_pos;
#endif
		return false;
	}
	else if (IsWord()) {
		SetError(ERROR_TRANSLATE_DATE, error_pos);
		return false;
	}
	return true;
}

/**
* IsEnd
* Purpose:
* Check the end of translation sign (i.e. reached the end of the buffer)
* Return value:
* true,false
*/

bool ibTranslateCode::IsEnd() const
{
	SkipSpaces();
	if (m_currentPos < m_bufferSize)
		return false;
	return true;
}

/**
* IsKeyWord
* Purpose:
* Determines whether the specified word is a service operator
* Return value if:
* -1: no
* greater than or equal to 0: number in the list of service words
*/

int ibTranslateCode::IsKeyWord(const wxString& strKeyWord)
{
	// The inverse of s_listKeyWord: spelling -> the number this returns. Built ONCE,
	// on first use, and const from then on.
	//
	// It used to be a mutable static that every ibTranslateCode ctor topped up
	// behind `if (ms_listHashKeyWord.size() == 0) LoadKeyWords()` — an unguarded
	// check-then-fill, where LoadKeyWords opens with clear(). Two sessions compiling
	// at once could both see it empty, and a third could be reading the map while
	// one of them wiped it. A function-local static gets the one-time, thread-safe
	// initialisation from the language itself (C++11 [stmt.dcl]/4) — and, sitting
	// in its one caller, needs no accessor to reach it.
	static const std::map<wxString, int, ibCaseFoldLess> s_keyWordNumber = [] {
		std::map<wxString, int, ibCaseFoldLess> listNumber;
		for (int i = 0; i < static_cast<int>(WXSIZEOF(s_listKeyWord)); i++)
			listNumber[s_listKeyWord[i].m_strKeyWord] = i;
		return listNumber;
	}();

	auto it = s_keyWordNumber.find(strKeyWord);//case-folded by the map's comparator
	if (it != s_keyWordNumber.end()) {
		const int idx = it->second;
		// Code-style gate: hides VES-only block-fence keywords (Then /
		// Do / End*) when CES is active. Body lives in compileCode.cpp
		// so the gate reads gs_codeStyle without flipping the include
		// chain. Hidden keywords look like plain identifiers to every
		// consumer of IsKeyWord — lexer, syntax highlighter, the CES
		// parser (which then rejects them as unknown identifiers).
		if (!IsAllowedKey(idx))
			return wxNOT_FOUND;
		return idx;
	}

	return wxNOT_FOUND;
}

wxString ibTranslateCode::GetKeyWord(int k)
{
	// s_listKeyWord IS the inverse — index straight into it instead of scanning the
	// map for the entry pointing back at k. It also spells the keyword the way the
	// language does (PascalCase), where the map used to be keyed upper-cased and
	// this returned "ENDPROCEDURE"; compileCode.cpp already reads the array
	// directly for the same reason when it names a keyword in an error.
	if (k < 0 || k >= static_cast<int>(WXSIZEOF(s_listKeyWord)))
		return wxEmptyString;

	return s_listKeyWord[k].m_strKeyWord;
}

/**
* PrepareLexem
* PASS1 - loading lexemes for subsequent quick access during recognition
*/

bool ibTranslateCode::PrepareLexem()
{
	m_listLexem.clear();

	// Reserve capacity here (after clear) instead of in Load — that way
	// the reserve grows from an empty vector with no per-element move
	// work, and incremental PrepareLexem(line, ...) (which depends on
	// previous-parse lexems being intact) is unaffected.
	const size_t alloc_size = CalcAllocSize();
	if (alloc_size > m_listLexem.capacity())
		m_listLexem.reserve(alloc_size + 1000);

	if (m_defineList == nullptr) {
		m_defineList = new ibDefineCollection();
		m_defineList->SetParent(&ms_listDefine);
		m_bAutoDeleteDefList = true;//indication that the array with definitions was created by us (and not passed as a definition translation)
	}


#ifdef UTF8_LEXEM_TRANSLATE
	unsigned int total_line = 0,
		total_pos = 0, total_pos_utf8 = 0;
#else 
	unsigned int total_line = 0,
		total_pos = 0;
#endif

	for (auto& translateCode : m_listTranslateCode) {
		translateCode->m_currentLine = translateCode->m_currentPos = 0;
		if (!translateCode->PrepareLexem())
			return false;
		for (auto& m_current_lex : translateCode->m_listLexem) {
			if (m_current_lex.m_lexType != ENDPROGRAM) {
				m_listLexem.push_back(m_current_lex);
			}
		}
		total_line += translateCode->m_currentLine;
		total_pos += translateCode->m_currentPos;
#ifdef UTF8_LEXEM_TRANSLATE
		total_pos_utf8 += translateCode->m_currentUtf8Pos;
#endif
	}

	wxString s;

	while (!IsEnd()) {

		// m_current_lex.m_translateCode is bound once in the ctor and
		// preserved across emplace_back moves — no per-iteration write here.

		m_current_lex.m_numLine = m_currentLine;
		m_current_lex.m_numString = m_currentPos;//if an error occurs later, this is the string that should be returned to the user
#ifdef UTF8_LEXEM_TRANSLATE
		m_current_lex.m_numUtf8String = m_currentUtf8Pos;
#endif

		if (IsWord()) {

			wxString strOrig;
			if (GetWord(s, strOrig)) {

				//processing user definitions (#define)
				// One chain walk, not the HasDefine-then-GetDefine pair.
				if (const ibLexemList* pDef = m_defineList->FindDefine(s)) {
					for (const ibLexem& lexDef : *pDef) {
						// COPY first, stamp the copy. The old code stamped position and
						// back-pointer into the STORED definition and copied afterwards,
						// so every expansion rewrote the dictionary entry it was reading
						// — shared with each later use of the name and, up the chain,
						// with the defining module itself.
						//
						// (It also indexed as `pDef[i].data()`, pointer arithmetic on the
						// LIST pointer rather than into the list: correct for the first
						// lexem by coincidence, reading a nonexistent vector object for
						// every one after it — so any define longer than a single lexem,
						// `#Define MAX 10 + 5`, was undefined behaviour.)
						m_listLexem.push_back(lexDef);
						ibLexem& lexUse = m_listLexem.back();
						lexUse.m_numString = m_currentPos;
						lexUse.m_numLine = m_currentLine;//for breakpoints
#ifdef UTF8_LEXEM_TRANSLATE
						lexUse.m_numUtf8String = m_currentUtf8Pos;
#endif
						// Rebind to consumer's translate so source attribution
						// follows the expansion site (consumer's module name /
						// doc path), not the original definition's.
						lexUse.m_translateCode = this;
					}
					continue;
				}

				const int k = IsKeyWord(s);

				//undefined
				if (k == KEY_UNDEFINED) {
					m_current_lex.m_lexType = CONSTANT;
					m_current_lex.m_valData.SetType(ibValueTypes::TYPE_EMPTY);
				}
				//boolean
				else if (k == KEY_TRUE || k == KEY_FALSE) {
					m_current_lex.m_lexType = CONSTANT;
					m_current_lex.m_valData.SetBoolean(s);
				}
				//null
				else if (k == KEY_NULL) {
					m_current_lex.m_lexType = CONSTANT;
					m_current_lex.m_valData.SetType(ibValueTypes::TYPE_NULL);
				}
				else {

					// AFTER A DOT, A KEYWORD IS A MEMBER NAME. `sel.Where(...)`,
					// `q.Select(...)` — the contextual LINQ words are ordinary
					// method names in a property position, and classifying them
					// as KEYWORD there breaks both the parse and the editor's
					// completion after `sel.`. The lexer already knows what came
					// before it; asking is cheaper than reclassifying later.
					if (k >= 0 && !PreviousLexemIsDot()) {
						m_current_lex.m_lexType = KEYWORD;
						m_current_lex.m_numData = k;
					}
					else {
						m_current_lex.m_lexType = IDENTIFIER;
					}

					m_current_lex.m_valData = strOrig;
				}

				m_current_lex.m_strData = s;
			}
		}
		else if (IsNumber() || IsString() || IsDate()) {
			m_current_lex.m_lexType = CONSTANT;
			if (IsNumber()) {
				const int curPos = m_currentPos;
				GetNumber(s);
				if (!m_current_lex.m_valData.SetNumber(s)) {
					SetError(ERROR_TRANSLATE_NUMBER, curPos);
				}
				m_current_lex.m_strData = s;
				int n = m_listLexem.size() - 1;
				if (n >= 0) {
					if (m_listLexem[n].m_lexType == DELIMITER && (m_listLexem[n].m_numData == wxT('-') || m_listLexem[n].m_numData == wxT('+'))) {
						n--;
						if (n >= 0) {
							if (m_listLexem[n].m_lexType == DELIMITER &&
								(
									m_listLexem[n].m_numData == wxT('[') ||
									m_listLexem[n].m_numData == wxT('(') ||
									m_listLexem[n].m_numData == wxT(',') ||
									m_listLexem[n].m_numData == wxT('<') ||
									m_listLexem[n].m_numData == wxT('>') ||
									m_listLexem[n].m_numData == wxT('='))) {
								n++;
								if (m_listLexem[n].m_numData == wxT('-'))
									m_current_lex.m_valData.m_fData = -m_current_lex.m_valData.m_fData;
								m_listLexem[n] = m_current_lex;
								continue;
							}
						}
					}
				}
			}
			else {
				if (IsString()) {
					const int curPos = m_currentPos;
					GetString(s);
					if (!m_current_lex.m_valData.SetString(s)) {
						SetError(ERROR_TRANSLATE_STRING, curPos);
					}
					m_current_lex.m_strData = s;
				}
				else if (IsDate()) {
					const int curPos = m_currentPos;
					GetDate(s);
					if (!m_current_lex.m_valData.SetDate(s)) {
						SetError(ERROR_TRANSLATE_DATE, curPos);
					}
					m_current_lex.m_strData = s;
				}
			}

			m_listLexem.emplace_back(std::move(m_current_lex));
			continue;
		}
		else if (IsByte('~')) {
			s.Clear();
			GetByte();//skip the separator and auxiliary symbol of the mark (as unnecessary)
			continue;
		}
		else {

			s.Clear();

			m_current_lex.m_lexType = DELIMITER;
			wxUniChar byte; GetByte(byte);
			m_current_lex.m_numData = byte;

			if (m_current_lex.m_numData <= 13) continue;
		}
		m_current_lex.m_strData = s;
		if (m_current_lex.m_lexType == KEYWORD) {
			// ⭐⭐ EDITING: A DIRECTIVE IS A LINE THAT IS NOT CODE. Every branch below ACTS on one —
			// registers a name, hides an excluded region, demands an `#endregion` — and raises when
			// the line is malformed. See ibLexemMode for why none of that is wanted about a text
			// being typed into. A directive's effect IS its line, so the line is what is read past.
			if (m_lexemMode == ibLexemMode::Editing && IsDirective(m_current_lex.m_numData)) {
				SkipLine();
				continue;
			}
			if (m_current_lex.m_numData == KEY_DEFINE && m_nModePreparing != LEXEM_ADDDEF) { //setting an arbitrary identifier
				if (!IsWord()) {
					SetError(ERROR_IDENTIFIER_DEFINE, m_currentPos);
				}
				wxString strName; GetWord(strName);
				//add the translation result to the list of definitions
				if (LEXEM_ADD == m_nModePreparing)
					PrepareFromCurrent(LEXEM_ADDDEF, strName);
				else
					PrepareFromCurrent(LEXEM_IGNORE, strName);

				continue;
			}
			else if (m_current_lex.m_numData == KEY_UNDEF) {//removing the identifier
				if (!IsWord()) {
					SetError(ERROR_IDENTIFIER_DEFINE, m_currentPos);
				}
				wxString strName; GetWord(strName);
				m_defineList->RemoveDef(strName);
				continue;
			}
			else if (m_current_lex.m_numData == KEY_IFDEF || m_current_lex.m_numData == KEY_IFNDEF) { //conditional compilation

				if (!IsWord()) {
					SetError(ERROR_IDENTIFIER_DEFINE, m_currentPos);
				}

				wxString strName; GetWord(strName);

				bool bHasDef = m_defineList->HasDefine(strName);
				if (m_current_lex.m_numData == KEY_IFNDEF)
					bHasDef = !bHasDef;

				//translate the entire block until #else or #endif is encountered
				int nMode = 0;
				if (bHasDef)
					nMode = LEXEM_ADD;//add the translation result to the list of lexemes
				else
					nMode = LEXEM_IGNORE;//otherwise ignore

				PrepareFromCurrent(nMode);

				if (!IsWord()) {
					SetError(ERROR_USE_ENDDEF, m_currentPos);
				}

				wxString strWord; GetWord(strWord);
				if (IsKeyWord(strWord) == KEY_ELSEDEF) {//suddenly #else

					//translate again
					if (!bHasDef)
						nMode = LEXEM_ADD;//add the translation result to the list of lexemes
					else
						nMode = LEXEM_IGNORE;//otherwise ignore

					PrepareFromCurrent(nMode);

					if (!IsWord()) {
						SetError(ERROR_USE_ENDDEF, m_currentPos);
					}

					GetWord(strWord);
				}
				//Require #endif
				if (IsKeyWord(strWord) != KEY_ENDIFDEF) {
					SetError(ERROR_USE_ENDDEF, m_currentPos);
				}
				continue;
			}
			else if (m_current_lex.m_numData == KEY_ENDIFDEF) {//end of conditional compilation
				m_currentPos = m_current_lex.m_numString;//here we saved the previous value
#ifdef UTF8_LEXEM_TRANSLATE
				m_currentUtf8Pos = m_current_lex.m_numUtf8String;
#endif
				break;
			}
			else if (m_current_lex.m_numData == KEY_ELSEDEF) {//"Otherwise" of conditional compilation
				//return to the beginning of the conditional operator
				m_currentPos = m_current_lex.m_numString;//here we saved the previous value
#ifdef UTF8_LEXEM_TRANSLATE
				m_currentUtf8Pos = m_current_lex.m_numUtf8String;
#endif
				break;
			}
			else if (m_current_lex.m_numData == KEY_REGION) {
				if (!IsWord()) {
					SetError(ERROR_IDENTIFIER_REGION, m_currentPos);
				}
				/*const wxString &strName = */GetWord();
				PrepareFromCurrent(LEXEM_ADD);
				if (!IsWord()) {
					SetError(ERROR_USE_ENDREGION, m_currentPos);
				}
				wxString strWord; GetWord(strWord);
				//Require #endregion
				if (IsKeyWord(strWord) != KEY_ENDREGION) {
					SetError(ERROR_USE_ENDREGION, m_currentPos);
				}
				continue;
			}
			else if (m_current_lex.m_numData == KEY_ENDREGION) {
				m_currentPos = m_current_lex.m_numString;//here we saved the previous value
#ifdef UTF8_LEXEM_TRANSLATE
				m_currentUtf8Pos = m_current_lex.m_numUtf8String;
#endif
				break;
			}
		}
		m_listLexem.emplace_back(std::move(m_current_lex));
	}

	m_current_lex.m_lexType = ENDPROGRAM;
	m_current_lex.m_numData = 0;

	// m_translateCode bound at ctor time, kept through moves.

	m_current_lex.m_numLine = total_line + m_currentLine;
	m_current_lex.m_numString = total_pos + m_currentPos;
#ifdef UTF8_LEXEM_TRANSLATE
	m_current_lex.m_numUtf8String = total_pos_utf8 + m_currentUtf8Pos;
#endif

	m_listLexem.emplace_back(std::move(m_current_lex));
	return true;
}

// ⭐ THE PATCH — see the declaration. Moved here from the editor's own copy of this class, which is
// where it was written and not where its data lives: every line of it rewrites m_listLexem.
void ibTranslateCode::PrepareLexem(const ibTextEdit& edit)
{
	// A patch has no baseline: without a prior full pass the index math below has not even the
	// trailing ENDPROGRAM marker to stand on.
	if (!HasLexem())
		return;

	const unsigned int line = edit.m_line;
	const int line_offset = edit.m_lineOffset;
	const int pos_offset = edit.m_posOffset;
#ifdef UTF8_LEXEM_TRANSLATE
	const int pos_offset_utf8 = edit.m_posOffsetUtf8;
#endif

	m_currentLine = m_currentPos = 0;

	// Rewind the tokenizer to one lexem BEFORE the first lexem at or past `line` (or before the
	// ENDPROGRAM marker if the edit is past the last real lexem). Re-tokenization restarts at that
	// lexem's start position — the erase pass below drops it together with the edited region, and
	// the tokenizer then re-emits it as the first new lexem. This minimises the rewind to a single
	// lexem of replay.
	unsigned int lexem_idx = 0;
	bool insert_after = false;
	auto hint = m_listLexem.begin();

	for (size_t i = 0; i < m_listLexem.size(); ++i) {

		const bool atTriggerLine = m_listLexem[i].m_numLine >= line;
		const bool atEndProgram = m_listLexem[i].m_lexType == ENDPROGRAM;

		if (!atTriggerLine && !atEndProgram)
			continue;

		if (i > 0) {
			m_currentLine = m_listLexem[i - 1].m_numLine;
			m_currentPos = m_listLexem[i - 1].m_numString;
#ifdef UTF8_LEXEM_TRANSLATE
			m_currentUtf8Pos = m_listLexem[i - 1].m_numUtf8String;
#endif
			lexem_idx = (unsigned int)(i - 1);
			if (lexem_idx > 0) std::advance(hint, lexem_idx - 1);
			insert_after = atEndProgram ? true : (lexem_idx > 0);
		}
		break;
	}

	wxString s;

	const bool insert_text = pos_offset > 0;
	const bool delete_text = pos_offset < 0;

	m_listLexem.erase(
		std::remove_if(m_listLexem.begin() + lexem_idx, m_listLexem.end() - 1,
			[&](const auto& e) {
				if (insert_text) return e.m_numLine <= line;
				if (delete_text) return e.m_numLine <= (line - line_offset);
				return false;
			}),
		m_listLexem.end() - 1
	);

	if (m_listLexem.size() <= 1) {
		hint = m_listLexem.begin();
		insert_after = false;
	}

	while (!IsEnd()) {

		if (insert_text && m_currentLine > (line + line_offset)) break;
		else if (delete_text && (m_currentLine > line)) break;

		m_current_lex.m_numLine = m_currentLine;
		m_current_lex.m_numString = m_currentPos;
#ifdef UTF8_LEXEM_TRANSLATE
		m_current_lex.m_numUtf8String = m_currentUtf8Pos;
#endif // UTF8_LEXEM_TRANSLATE

		if (IsWord()) {

			wxString strOrig;

			if (GetWord(s, strOrig)) {

				const int k = IsKeyWord(s);

				//undefined
				if (k == KEY_UNDEFINED) {
					m_current_lex.m_lexType = CONSTANT;
					m_current_lex.m_valData.SetType(ibValueTypes::TYPE_EMPTY);
				}
				//boolean
				else if (k == KEY_TRUE || k == KEY_FALSE) {
					m_current_lex.m_lexType = CONSTANT;
					m_current_lex.m_valData.SetBoolean(s);
				}
				//null
				else if (k == KEY_NULL) {
					m_current_lex.m_lexType = CONSTANT;
					m_current_lex.m_valData.SetType(ibValueTypes::TYPE_NULL);
				}
				else {

					// After a `.` a keyword is a MEMBER NAME — `sel.Where(...)`. Asked here, at
					// classification time, exactly as the full pass asks it; that is what makes a
					// second sweep over the whole list unnecessary — and a sweep would have undone
					// this walk's entire reason for existing.
					if (k >= 0 && !PreviousLexemIsDot()) {
						m_current_lex.m_lexType = KEYWORD;
						m_current_lex.m_numData = k;
					}
					else {
						m_current_lex.m_lexType = IDENTIFIER;
					}

					m_current_lex.m_valData = strOrig;
				}
			}

			m_current_lex.m_strData = s;
		}
		else if (IsNumber() || IsString() || IsDate()) {
			m_current_lex.m_lexType = CONSTANT;
			if (IsNumber()) {

				GetNumber(s); m_current_lex.m_valData.SetNumber(s);

				if (hint != m_listLexem.begin() && hint->m_lexType == DELIMITER && (hint->m_numData == '-' || hint->m_numData == '+')) {
					auto prev = std::prev(hint, 1);
					if (prev != m_listLexem.begin() && prev->m_lexType == DELIMITER && (prev->m_numData == '[' || prev->m_numData == '(' || prev->m_numData == ',' || prev->m_numData == '<' || prev->m_numData == '>' || prev->m_numData == '=')) {
						if (hint->m_numData == '-')
							m_current_lex.m_valData.m_fData = -m_current_lex.m_valData.m_fData;
						*hint = std::move(m_current_lex);
						continue;
					}
				}
			}
			else {
				if (IsString()) {
					GetString(s); m_current_lex.m_valData.SetString(s);
				}
				else if (IsDate()) {
					GetDate(s); m_current_lex.m_valData.SetDate(s);
				}
			}

			if (insert_after) {
				hint = m_listLexem.emplace(
					std::next(hint, 1), std::move(m_current_lex));
			}
			else {
				hint = m_listLexem.emplace(
					hint, std::move(m_current_lex));
				insert_after = true;
			}

			continue;
		}
		else if (IsByte('~')) {
			s.clear();
			GetByte();
			continue;
		}
		else {

			s.clear();

			m_current_lex.m_lexType = DELIMITER;
			wxUniChar byte; GetByte(byte);
			m_current_lex.m_numData = byte;

			if (m_current_lex.m_numData <= 13) continue;
		}
		m_current_lex.m_strData = s;
		// The same rule the full pass makes, and it has to be here too: this walk re-lexes the
		// edited lines, which is exactly where a half-written directive lives. See ibLexemMode.
		if (m_current_lex.m_lexType == KEYWORD
			&& m_lexemMode == ibLexemMode::Editing && IsDirective(m_current_lex.m_numData)) {
			SkipLine();
			continue;
		}

		if (insert_after) {
			hint = m_listLexem.emplace(
				std::next(hint, 1), std::move(m_current_lex));
		}
		else {
			hint = m_listLexem.emplace(
				hint, std::move(m_current_lex));
			insert_after = true;
		}
	}

	const size_t lex_size = m_listLexem.size() - 1;

	if (lex_size > 0) {

		const size_t lex_distance = std::distance(m_listLexem.begin(), insert_after ? hint + 1 : hint);

		for (unsigned int i = (unsigned int)lex_distance; i < lex_size; i++) {
			m_listLexem[i].m_numLine += line_offset;
			m_listLexem[i].m_numString += pos_offset;
#ifdef UTF8_LEXEM_TRANSLATE
			m_listLexem[i].m_numUtf8String += pos_offset_utf8;
#endif // UTF8_LEXEM_TRANSLATE
		}
	}

	m_listLexem[lex_size].m_numString += pos_offset;
#ifdef UTF8_LEXEM_TRANSLATE
	m_listLexem[lex_size].m_numUtf8String += pos_offset_utf8;
#endif
}

// ⭐⭐ WHAT THE CARET IS STANDING IN — see the declaration.
//
// IT ASKS ABOUT THE TOKENS AT THE CARET, NOT ABOUT THE ONES IT PASSED. The version this replaces
// replayed the stream from the first token, carrying `expression` / `hasPoint` / `hasKeyword`
// forward and resetting them on anything unrelated — so the answer was "the last interesting thing
// I saw", and it had to defend itself against its own memory (a string literal earlier in the
// module poisoning the filter, and a hardcoded list of three platform functions to decide when a
// literal counted). A completion is about what is being written HERE; three tokens back is the
// whole of what that needs.
ibTranslateCode::ibCaretText ibTranslateCode::CaretAt(unsigned int caret) const
{
	ibCaretText answer;

	if (m_listLexem.empty())
		return answer;

	// The last token that begins before the caret — the one being typed, or the one just closed.
	size_t at = 0;
	bool found = false;
	for (size_t i = 0; i < m_listLexem.size(); i++) {
		if (m_listLexem[i].m_numString >= caret)
			break;
		if (m_listLexem[i].m_lexType == ENDPROGRAM)
			break;
		at = i; found = true;
	}

	if (!found)
		return answer;   // the caret is before any code: open, and nothing typed yet

	const ibLexem& here = m_listLexem[at];

	// WHAT A LIST FILTERS BY. The word under the caret, whatever kind it is — an identifier being
	// typed, or the contents of a string literal being typed inside a call. It is not part of the
	// question, which is why it is filled the same way in every branch below.
	if (here.m_lexType == IDENTIFIER || here.m_lexType == CONSTANT)
		answer.m_word = here.m_valData.GetString();

	// A MEMBER ACCESS: the caret is on the dot, or on the name being typed after one.
	const bool onDot = here.m_lexType == DELIMITER && here.m_numData == '.';
	const bool afterDot = here.m_lexType == IDENTIFIER && at > 0
		&& m_listLexem[at - 1].m_lexType == DELIMITER && m_listLexem[at - 1].m_numData == '.';

	if (onDot || afterDot) {
		answer.m_place = ibCaretPlace::AfterDot;
		answer.m_expression = ExpressionEndingAt(onDot ? at : at - 1);
		return answer;
	}

	// `New <name>` — the keyword names what may follow it, and what follows is the path so far.
	{
		const size_t nameAt = (here.m_lexType == IDENTIFIER && at > 0) ? at - 1 : at;
		if (m_listLexem[nameAt].m_lexType == KEYWORD && m_listLexem[nameAt].m_numData == KEY_NEW) {
			answer.m_place = ibCaretPlace::InKeyword;
			answer.m_keyword = m_listLexem[nameAt].m_valData.GetString();
			answer.m_expression = (nameAt == at) ? wxString() : answer.m_word;
			return answer;
		}
	}

	// ⭐ INSIDE A CALL'S ARGUMENTS, AND THE CALL IS ALL THIS SAYS. Which calls have names worth
	// offering — `Type`, `GetCommonForm`, `ShowCommonForm` — is the CALLER's knowledge, and the
	// caller already holds that list; carrying a second copy here made the lexer know about
	// platform functions, and made every OTHER call's argument answer with nothing at all.
	{
		int depth = 0;
		for (size_t i = at + 1; i-- > 0; ) {
			const ibLexem& lex = m_listLexem[i];
			if (lex.m_lexType != DELIMITER)
				continue;

			// 🛑 A CALL DOES NOT SPAN STATEMENTS. Without this the search for an unmatched `(` runs
			// to the top of the module and finds one left open by a line somebody is still writing —
			// so a caret several statements below reported itself as being inside THAT call, and the
			// list it wanted (the members after a dot) never came. Measured 2026-09-07: an unclosed
			// `Catalogs.Property(` four lines up silenced `TitleLocation.` completely.
			if (lex.m_numData == ';' || lex.m_numData == '{' || lex.m_numData == '}')
				break;

			if (lex.m_numData == ')') { depth++; continue; }
			if (lex.m_numData != '(') continue;
			if (depth > 0) { depth--; continue; }

			// An unmatched `(` — the caret is inside its arguments. Named by whatever it opened on.
			if (i > 0 && m_listLexem[i - 1].m_lexType == IDENTIFIER) {
				answer.m_place = ibCaretPlace::InKeyword;
				answer.m_keyword = m_listLexem[i - 1].m_valData.GetString();
			}
			break;
		}
	}

	return answer;
}

// The dotted path ENDING at `dotAt` — `Catalogs.Goods` for a caret in `Catalogs.Goods.`. Walked
// backwards over `name . name . name`, which is the only shape a path has.
wxString ibTranslateCode::ExpressionEndingAt(size_t dotAt) const
{
	std::vector<wxString> parts;

	for (size_t i = dotAt + 1; i-- > 0; ) {

		const ibLexem& lex = m_listLexem[i];

		if (lex.m_lexType == DELIMITER && lex.m_numData == '.')
			continue;

		if (lex.m_lexType != IDENTIFIER)
			break;

		parts.push_back(lex.m_valData.GetString());

		// A name reached through a call (`f().x`) is not a path anyone can spell back.
		if (i > 0 && m_listLexem[i - 1].m_lexType == DELIMITER && m_listLexem[i - 1].m_numData == ')')
			break;
		if (i == 0 || m_listLexem[i - 1].m_lexType != DELIMITER || m_listLexem[i - 1].m_numData != '.')
			break;
	}

	wxString path;
	for (size_t i = parts.size(); i-- > 0; ) {
		if (!path.IsEmpty()) path += wxT('.');
		path += parts[i];
	}
	return path;
}

/**
* create lexemes starting from the current position
*/

void ibTranslateCode::PrepareFromCurrent(int nMode, const wxString& strName)
{
	ibTranslateCode translate;

	translate.m_defineList = m_defineList;
	translate.m_nModePreparing = nMode;
	translate.m_strModuleName = m_strModuleName;

	translate.Load(m_strBuffer);

	//start line number
	translate.m_currentLine = m_currentLine;
	translate.m_currentPos = m_currentPos;
#ifdef UTF8_LEXEM_TRANSLATE
	translate.m_currentUtf8Pos = m_currentUtf8Pos;
#endif

	//end line number
	if (nMode == LEXEM_ADDDEF) {
		GetStrToEndLine();
		translate.m_bufferSize = m_currentPos;
	}

	translate.PrepareLexem();

	if (nMode == LEXEM_ADDDEF) {
		// Re-anchor BEFORE storing, for the same reason LEXEM_ADD does below: the
		// local `translate` dies with this call, so the stored definition would sit
		// in the table holding back-pointers into a dead object. It used to be
		// papered over at expansion, which stamped `this` into the stored lexems —
		// that write is gone now (expansion stamps its own copy), so the entry has
		// to go in clean. translate.m_strModuleName was set to ours above.
		for (ibLexem& lex : translate.m_listLexem) lex.m_translateCode = this;
		m_defineList->SetDefine(strName, &translate.m_listLexem);
		m_currentLine = translate.m_currentLine;
	}
	else if (nMode == LEXEM_ADD) {
		for (unsigned int i = 0; i < translate.m_listLexem.size() - 1; i++) {//excluding ENDPROGRAM
			m_listLexem.push_back(translate.m_listLexem[i]);
			// Re-anchor to caller's translate — the local `translate`
			// object dies when this function returns; lexems' back-pointer
			// would otherwise dangle. translate.m_strModuleName was set
			// to ours above so the strings are identical.
			m_listLexem.back().m_translateCode = this;
		}

		m_currentLine = translate.m_currentLine;
		m_currentPos = translate.m_currentPos;
#ifdef UTF8_LEXEM_TRANSLATE
		// ⚠ WRITES INTO `translate`, AND THAT IS NOT THE TYPO IT LOOKS LIKE. The two lines above
		// read OUT of it and the else-branch below does too, so an audit reads this as reversed —
		// it was "fixed" that way on 2026-09-08 and the sweep over real modules fell from 224/285
		// to 146/285 in one run. This branch has taken lexems from a DIFFERENT buffer, whose
		// character count is its own; carrying it back would corrupt the caret's own count, which
		// is measured in characters. Byte position re-anchors, character position does not.
		translate.m_currentUtf8Pos = m_currentUtf8Pos;
#endif
	}
	else {
		m_currentLine = translate.m_currentLine;
		m_currentPos = translate.m_currentPos;
#ifdef UTF8_LEXEM_TRANSLATE
		m_currentUtf8Pos = translate.m_currentUtf8Pos;
#endif
	}
}

void ibTranslateCode::AppendModule(ibTranslateCode* module)
{
	auto iterator = std::find(m_listTranslateCode.begin(), m_listTranslateCode.end(), module);
	if (iterator != m_listTranslateCode.end())
		return;
	m_listTranslateCode.push_back(module);
}

void ibTranslateCode::RemoveModule(ibTranslateCode* module)
{
	m_listTranslateCode.erase(
		std::remove(m_listTranslateCode.begin(), m_listTranslateCode.end(), module),
		m_listTranslateCode.end()
	);
}

void ibTranslateCode::OnSetParent(ibTranslateCode* setParent)
{
	if (!m_defineList) {
		m_defineList = new ibDefineCollection;
		m_defineList->SetParent(&ms_listDefine);
		m_bAutoDeleteDefList = true;//sign of auto deletion
	}

	if (setParent) {
		m_defineList->SetParent(setParent->m_defineList);
	}
	else {
		m_defineList->SetParent(&ms_listDefine);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////

size_t ibTranslateCode::CalcAllocSize() const {

	// Heuristic — average token is ~4 source chars (identifiers, numbers,
	// short keywords, single-char delimiters; whitespace and comments
	// don't produce lexems). +16 for tiny inputs. The previous version
	// ran a full tokenizer pass over a copy of both buffers just to
	// count and ate ~46% of the Enter-keypress OnTextChange handler.
	// Slight over-estimate is intentional — vector::reserve takes the
	// extra capacity without complaint, and one cheap arithmetic op
	// beats a second tokenization round.
	return m_bufferSize > 0 ? (m_bufferSize / 4) + 16 : 1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////

// A wxModule subclass used to sit here to preload the keyword table at startup.
// It declared neither wxDECLARE_DYNAMIC_CLASS nor wxIMPLEMENT_DYNAMIC_CLASS —
// wxModule::RegisterModules finds candidates through wxClassInfo, so without them
// the class was never instantiated and OnInit never ran (compare wxFrontendModule
// in frontend/artProvider/artProvider.cpp, which has both). The table was in fact
// being built by whichever ibTranslateCode ctor got there first. IsKeyWord's own
// static now does the same job on first use, once, without a module to register.

