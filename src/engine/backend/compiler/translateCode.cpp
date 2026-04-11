////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, 2С-team
//	Description : translate module 
////////////////////////////////////////////////////////////////////////////

#include "translateCode.h"

//////////////////////////////////////////////////////////////////////
//                           Constants
//////////////////////////////////////////////////////////////////////

//empty lexem  
const ibLexem gs_nullLexem = {};

//keywords 
static std::map<wxString, void*> s_listHelpDescription; //description of keywords and system functions
static std::map<wxString, void*> s_listHashKeyword;

ibTranslateCode::ibDefineCollection ibTranslateCode::ms_listDefine; //global array of definitions
std::map<wxString, void*> ibTranslateCode::ms_listHashKeyWord; //list of keywords

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
	{"Procedure"},
	{"EndProcedure"},
	{"Function"},
	{"EndFunction"},
	{"Export"},
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
};

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

void ibTranslateCode::ibDefineCollection::RemoveDef(const wxString& strName)
{
	auto iterator = std::find_if(m_defineList.begin(), m_defineList.end(),
		[strName](const auto& pair) { return stringUtils::CompareString(pair.first, strName); }
	);

	if (iterator != m_defineList.end()) m_defineList.erase(iterator);
}

bool ibTranslateCode::ibDefineCollection::HasDefine(const wxString& strName) const
{
	auto iterator = std::find_if(m_defineList.begin(), m_defineList.end(),
		[strName](const auto& pair) { return stringUtils::CompareString(pair.first, strName); }
	);

	if (iterator != m_defineList.end()) return true;

	static int nLevel = 0;

	nLevel++;

	if (nLevel > MAX_OBJECTS_LEVEL) ibBackendCoreException::Error(_("Recursive module call (#3)"));

	//find in parent
	bool result = false;
	if (m_parentDefine != nullptr)
		result = m_parentDefine->HasDefine(strName);
	nLevel--;
	return result;
}

ibLexemList* ibTranslateCode::ibDefineCollection::GetDefine(const wxString& strName)
{
	auto iterator = std::find_if(m_defineList.begin(), m_defineList.end(),
		[strName](const auto& pair) { return stringUtils::CompareString(pair.first, strName); }
	);

	if (iterator != m_defineList.end()) return iterator->second;

	//find in parent
	if (m_parentDefine != nullptr && m_parentDefine->HasDefine(strName))
		return m_parentDefine->GetDefine(strName);

	ibLexemList* lexList = new ibLexemList();
	m_defineList[stringUtils::MakeUpper(strName)] = lexList;
	return lexList;
}

void ibTranslateCode::ibDefineCollection::SetDefine(const wxString& strName, ibLexemList* src)
{
	ibLexemList* dst = GetDefine(strName);
	dst->clear();
	if (src != nullptr) {
		for (unsigned int i = 0; i < src->size(); i++) dst->push_back(*src[i].data());
	}
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
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

ibTranslateCode::ibTranslateCode() : m_defineList(nullptr),
m_bAutoDeleteDefList(false),
m_nModePreparing(LEXEM_ADD)
{
	//prepare keyword buffer
	if (ms_listHashKeyWord.size() == 0) LoadKeyWords(); //only once
}

ibTranslateCode::ibTranslateCode(const wxString& strModuleName, const wxString& strDocPath) : m_defineList(nullptr),
m_strModuleName(strModuleName), m_strDocPath(strDocPath),
m_bAutoDeleteDefList(false),
m_nModePreparing(LEXEM_ADD)
{
	//prepare keyword buffer
	if (ms_listHashKeyWord.size() == 0) LoadKeyWords(); //only once
}

ibTranslateCode::ibTranslateCode(const wxString& strFileName) : m_defineList(nullptr),
m_strFileName(strFileName),
m_bAutoDeleteDefList(false),
m_nModePreparing(LEXEM_ADD)
{
	//prepare keyword buffer
	if (ms_listHashKeyWord.size() == 0) LoadKeyWords(); //only once
}

ibTranslateCode::~ibTranslateCode()
{
	if (m_bAutoDeleteDefList) wxDELETE(m_defineList);
}

/**
* prepare keyword buffer
*/

void ibTranslateCode::LoadKeyWords()
{
	ms_listHashKeyWord.clear();

	for (unsigned int i = 0; i < sizeof(s_listKeyWord) / sizeof(s_listKeyWord[0]); i++)
	{
		const wxString& strEng = stringUtils::MakeUpper(s_listKeyWord[i].m_strKeyWord);
		ms_listHashKeyWord[strEng] = (void*)(i + 1);

		//add to array for parser
		s_listHashKeyword[strEng] = (void*)1;
		s_listHelpDescription[strEng] = &s_listKeyWord[i].m_strShortDescription;
	}
}

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

		const size_t alloc_size = CalcAllocSize();
		if (alloc_size > m_listLexem.capacity())
			m_listLexem.reserve(alloc_size + 1000);
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
	if (!IsNumber()) {
		SetError(ERROR_TRANSLATE_NUMBER, m_currentPos);
		return wxEmptyString;
	}
	SkipSpaces();
	if (m_currentPos >= m_bufferSize) {
		SetError(ERROR_TRANSLATE_NUMBER, m_currentPos);
		return wxEmptyString;
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
	else if (IsWord() && m_strBuffer[next_pos] != wxT(' ')) {
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
	//auto it = std::find_if(std::execution::par, ms_listHashKeyWord.begin(), ms_listHashKeyWord.end(),
	//	[strKeyWord](const std::pair<const wxString, void*>& pair) -> bool {
	//		return stringUtils::CompareString(pair.first, strKeyWord);
	//	}
	//);

	//if (it != ms_listHashKeyWord.end())
	//	return ((int)it->second) - 1;

	for (auto& pair : ms_listHashKeyWord) {
		if (stringUtils::CompareString(pair.first, strKeyWord))
			return static_cast<int>(reinterpret_cast<intptr_t>(pair.second)) - 1;
	}

	return wxNOT_FOUND;
}

wxString ibTranslateCode::GetKeyWord(int k)
{
	auto it = std::find_if(ms_listHashKeyWord.begin(), ms_listHashKeyWord.end(),
		[k](const std::pair<const wxString, void*>& pair) -> bool {
			return k == static_cast<int>(reinterpret_cast<intptr_t>(pair.second)) - 1;
		}
	);

	if (it != ms_listHashKeyWord.end())
		return it->first;

	return wxEmptyString;
}

/**
* PrepareLexem
* PASS1 - loading lexemes for subsequent quick access during recognition
*/

bool ibTranslateCode::PrepareLexem()
{
	m_listLexem.clear();

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

		m_current_lex.m_strModuleName = m_strModuleName;
		m_current_lex.m_strDocPath = m_strDocPath;
		m_current_lex.m_strFileName = m_strFileName;

		m_current_lex.m_numLine = m_currentLine;
		m_current_lex.m_numString = m_currentPos;//if an error occurs later, this is the string that should be returned to the user
#ifdef UTF8_LEXEM_TRANSLATE
		m_current_lex.m_numUtf8String = m_currentUtf8Pos;
#endif

		if (IsWord()) {

			wxString strOrig;
			if (GetWord(s, strOrig)) {

				//processing user definitions (#define)
				if (m_defineList->HasDefine(s)) {
					ibLexemList* pDef = m_defineList->GetDefine(s);
					for (unsigned int i = 0; i < pDef->size(); i++) {
						ibLexem* m_current_lex = pDef[i].data();
						m_current_lex->m_numString = m_currentPos;
						m_current_lex->m_numLine = m_currentLine;//for breakpoints
#ifdef UTF8_LEXEM_TRANSLATE
						m_current_lex->m_numUtf8String = m_currentUtf8Pos;
#endif
						m_current_lex->m_strModuleName = m_strModuleName;
						m_current_lex->m_strDocPath = m_strDocPath;
						m_current_lex->m_strFileName = m_strFileName;
						m_listLexem.push_back(*m_current_lex);
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

					if (k >= 0) {
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

	m_current_lex.m_strModuleName = m_strModuleName;
	m_current_lex.m_strDocPath = m_strDocPath;
	m_current_lex.m_strFileName = m_strFileName;

	m_current_lex.m_numLine = total_line + m_currentLine;
	m_current_lex.m_numString = total_pos + m_currentPos;
#ifdef UTF8_LEXEM_TRANSLATE
	m_current_lex.m_numUtf8String = total_pos_utf8 + m_currentUtf8Pos;
#endif

	m_listLexem.emplace_back(std::move(m_current_lex));
	return true;
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
		m_defineList->SetDefine(strName, &translate.m_listLexem);
		m_currentLine = translate.m_currentLine;
	}
	else if (nMode == LEXEM_ADD) {
		for (unsigned int i = 0; i < translate.m_listLexem.size() - 1; i++) {//excluding ENDPROGRAM
			m_listLexem.push_back(translate.m_listLexem[i]);
		}

		m_currentLine = translate.m_currentLine;
		m_currentPos = translate.m_currentPos;
#ifdef UTF8_LEXEM_TRANSLATE
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

	ibTranslateCode translate;

	translate.Clear();

	translate.m_strBuffer.assign(m_strBuffer);
	translate.m_strBUFFER.assign(m_strBUFFER);

	translate.m_bufferSize = m_bufferSize;

	size_t alloc_size = 1;

	while (!translate.IsEnd()) {
		if (translate.IsWord()) {
			try {
				(void)translate.GetWord();
				alloc_size++;
			}
			catch (...)
			{
			}
		}
		else if (translate.IsNumber() || translate.IsString() || translate.IsDate()) {
			if (translate.IsNumber()) {
				try {
					(void)translate.GetNumber();
					alloc_size++;
				}
				catch (...)
				{
				}
			}
			else if (translate.IsString()) {
				try {
					(void)translate.GetString();
					alloc_size++;
				}
				catch (...)
				{
				}
			}
			else if (translate.IsDate()) {
				try {
					(void)translate.GetDate();
					alloc_size++;
				}
				catch (...)
				{
				}
			}
		}
		else {
			try {
				(void)translate.GetByte();
				alloc_size++;
			}
			catch (...)
			{
			}
		}
	}

	return alloc_size;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////

#include <wx/module.h>

class wxOESKeywordModule : public wxModule
{
public:
	wxOESKeywordModule() : wxModule() {}
	virtual bool OnInit() {
		ibTranslateCode::LoadKeyWords();
		return true;
	}
	virtual void OnExit() {}
private:
	wxDECLARE_DYNAMIC_CLASS(wxOESKeywordModule);
};

wxIMPLEMENT_DYNAMIC_CLASS(wxOESKeywordModule, wxModule)