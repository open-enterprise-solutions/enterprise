////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, 2С-team
//	Description : translate module 
////////////////////////////////////////////////////////////////////////////

#include "translateModule.h"
#include "definition.h"
#include "utils/stringUtils.h"

extern std::map<wxString, void*>	aHelpDescription;//описание ключевых слов и системных функций
extern std::map<wxString, void*>	aHashKeywordList;

CDefList CTranslateModule::glDefList; //глобальный массив определений

std::map<wxString, void*>	CTranslateModule::m_aHashKeyWords;//список ключевых слов

//////////////////////////////////////////////////////////////////////
// Global array
//////////////////////////////////////////////////////////////////////
struct aKeyWordsDef s_aKeyWords[] =
{
	{"if"},
	{"then"},
	{"else"},
	{"elseif"},
	{"endif"},
	{"for"},
	{"foreach"},
	{"to"},
	{"in"},
	{"do"},
	{"enddo"},
	{"while"},
	{"goto"},
	{"not"},
	{"and"},
	{"or"},
	{"procedure"},
	{"endprocedure"},
	{"function"},
	{"endfunction"},
	{"export"},
	{"val"},
	{"return"},
	{"try"},
	{"except"},
	{"endtry"},
	{"continue"},
	{"break"},
	{"raise"},
	{"var"},

	//create object
	{"new"},

	//undefined type
	{"undefined"},

	//null type
	{"null"},

	//boolean type
	{"true"},
	{"false"},

	//preprocessor:
	{"#define"},
	{"#undef"},
	{"#ifdef"},
	{"#ifndef"},
	{"#else"},
	{"#endif"},
	{"#region"},
	{"#endregion"},
};

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CTranslateModule::CTranslateModule() : m_aDefList(NULL),
m_bAutoDeleteDefList(false),
m_nModePreparing(LEXEM_ADD)
{
	//подготовка буфера ключевых слов
	if (m_aHashKeyWords.size() == 0) {
		LoadKeyWords(); //только один раз
	}

	Clear();
}

CTranslateModule::CTranslateModule(const wxString& moduleName, const wxString& docPath) : m_aDefList(NULL),
m_sModuleName(moduleName), m_sDocPath(docPath),
m_bAutoDeleteDefList(false),
m_nModePreparing(LEXEM_ADD)
{
	//подготовка буфера ключевых слов
	if (m_aHashKeyWords.size() == 0) {
		LoadKeyWords(); //только один раз
	}

	Clear();
}

CTranslateModule::CTranslateModule(const wxString& fileName) : m_aDefList(NULL),
m_sFileName(fileName),
m_bAutoDeleteDefList(false),
m_nModePreparing(LEXEM_ADD)
{
	//подготовка буфера ключевых слов
	if (m_aHashKeyWords.size() == 0) {
		LoadKeyWords(); //только один раз
	}

	Clear();
}

CTranslateModule::~CTranslateModule()
{
	if (m_bAutoDeleteDefList && m_aDefList) {
		delete m_aDefList;
	}
}

/**
 *подготовка буфера ключевых слов
 */
void CTranslateModule::LoadKeyWords()
{
	m_aHashKeyWords.clear();

#if defined(_LP64) || defined(__LP64__) || defined(__arch64__) || defined(_WIN64)
	for (unsigned long long i = 0; i < sizeof(s_aKeyWords) / sizeof(s_aKeyWords[0]); i++)
#else 
	for (unsigned int i = 0; i < sizeof(s_aKeyWords) / sizeof(s_aKeyWords[0]); i++)
#endif 
	{
		wxString csEng = StringUtils::MakeUpper(s_aKeyWords[i].Eng);

		m_aHashKeyWords[csEng] = (void*)(i + 1);

		//добавляем в массив для синтаксического анализатора
		aHashKeywordList[csEng] = (void*)1;
		aHelpDescription[csEng] = &s_aKeyWords[i].sShortDescription;
	}
}

//////////////////////////////////////////////////////////////////////
// Translating
//////////////////////////////////////////////////////////////////////

/**
* Clear
* Назначение:
* Подготовить переменные для начала компиляции
* Возвращаемое значение:
* нет
*/
void CTranslateModule::Clear()
{
	m_sBuffer.clear();
	m_sBUFFER.clear();

	m_nSizeText = m_nCurPos = m_nCurLine = 0;
}

/**
 * Load
 * Назначение:
 * Загрузить буфер исходным текстом + подготовить переменные для компиляции
 * Возвращаемое значение:
 * нет
 */
void CTranslateModule::Load(const wxString& sCode)
{
	Clear();

	m_sBuffer = sCode;
	m_sBuffer.Trim();

	m_nSizeText = m_sBuffer.Length();

	if (m_sBuffer.IsEmpty())
		return;

	m_sBUFFER = m_sBuffer;
	m_sBUFFER.MakeUpper();
}

/**
 * SetError
 * Назначение:
 * Запомнить ошибку трансляции и вызвать исключение
 * Возвращаемое значение:
 * Метод не возвращает управление!s
 */

void CTranslateModule::SetError(int codeError, int currPos, const wxString& errorDesc) const
{
	int start_pos = 0;

	for (int i = currPos; i > 0; i--) { //ищем начало строки в которой выдается сообщение об ошибке трансляции
		if (m_sBuffer[i] == '\n') {
			start_pos = i + 1; break;
		};
	}

	int currLine = 1 + m_sBuffer.Left(start_pos).Replace('\n', '\n');

	CTranslateModule::SetError(codeError,
		m_sFileName, m_sModuleName, m_sDocPath,
		currPos, currLine,
		errorDesc);
}

/**
 * SetError
 * Назначение:
 * Запомнить ошибку трансляции и вызвать исключение
 * Возвращаемое значение:
 * Метод не возвращает управление!s
 */

void CTranslateModule::SetError(int codeError,
	const wxString& fileName, const wxString& moduleName, const wxString& docPath,
	int currPos, int currLine,
	const wxString& descError) const
{
	CTranslateModule* translateModule = NULL;
	if (m_sDocPath != docPath) {
		for (auto tModule : m_aTranslateModules) {
			if (docPath == tModule->m_sDocPath) {
				translateModule = tModule; break;
			}
		}
	}

	const wxString& buffer = translateModule ?
		translateModule->m_sBuffer :
		m_sBuffer;
	const wxString& codeLineError =
		CTranslateError::FindErrorCodeLine(buffer, currPos);

	CTranslateError::ProcessError(
		fileName,
		moduleName, docPath,
		currPos, currLine,
		codeLineError, codeError, descError
	);
}

/**
 * SkipSpaces
 * Назначение:
 * Пропустить все незначащие пробелы из буфера ввода
 * плюс комментарии из буфера ввода перенаправлять в буфер вывода
 * Возвращаемое значение:
 * НЕТ
 */
void CTranslateModule::SkipSpaces() const
{
	unsigned int i = m_nCurPos;
	for (; i < m_nSizeText; i++) {
		char c = m_sBUFFER[i];
		if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
			if (c == '/') { //может это комментарий
				if (i + 1 < m_nSizeText) {
					if (m_sBUFFER[i + 1] == '/') { //пропускаем комментарии
						for (unsigned int j = i; j < m_nSizeText; j++) {
							m_nCurPos = j;
							if (m_sBUFFER[j] == '\n' || m_sBUFFER[j] == 13) {
								//обрабатываем следующую строку
								SkipSpaces();
								return;
							}
						}
						i = m_nCurPos + 1;
					}
				}
			}
			m_nCurPos = i;
			break;
		}
		else if (c == '\n')
		{
			m_nCurLine++;
		}
	}

	if (i == m_nSizeText) {
		m_nCurPos = m_nSizeText;
	}
}

/**
 * IsByte
 * Назначение:
 * Проверить является ли следующий байт (без учета пробелов) равным
 * ЗАДАННОМУ байту
 * Возвращаемое значение:
 * true,false
 */
bool CTranslateModule::IsByte(char c) const
{
	SkipSpaces();

	if (m_nCurPos >= m_nSizeText)
		return false;

	if (m_sBUFFER[m_nCurPos] == c)
		return true;

	return false;
}

/**
 * GetByte
 * Назначение:
 * Получить из выборки байт (без учета пробелов)
 * если такого байта нет, то генерится исключение
 * Возвращаемое значение:
 * Байт из буфера
 */
char CTranslateModule::GetByte() const
{
	SkipSpaces();

	if (m_nCurPos < m_nSizeText)
		return m_sBuffer[m_nCurPos++];

	SetError(ERROR_TRANSLATE_BYTE, m_nCurPos);
	return '\0';
}

/**
 * IsWord
 * Назначение:
 * Проверить (не изменяя позиции текущего крсора)
 * является ли следующий набор букв словом (пропуская пробелы и пр.)
 * Возвращаемое значение:
 * true,false
 */
bool CTranslateModule::IsWord() const
{
	SkipSpaces();

	if (m_nCurPos < m_nSizeText) {
		if (((m_sBUFFER[m_nCurPos] == '_') ||
			(m_sBUFFER[m_nCurPos] >= 'A' && m_sBUFFER[m_nCurPos] <= 'Z') || (m_sBUFFER[m_nCurPos] >= 'a' && m_sBUFFER[m_nCurPos] <= 'z') ||
			(m_sBUFFER[m_nCurPos] >= 'А' && m_sBUFFER[m_nCurPos] <= 'Я') || (m_sBUFFER[m_nCurPos] >= 'а' && m_sBUFFER[m_nCurPos] <= 'я') ||
			(m_sBUFFER[m_nCurPos] == '#')) && (m_sBUFFER[m_nCurPos] != '[' && m_sBUFFER[m_nCurPos] != ']'))
			return true;
	}

	return false;
}

/**
 * GetWord
 * Назначение:
 * Выбрать из буфера следующее слово
 * если слова нет (т.е. следующий набор букв не является словом), то генерится исключение
 * Параметр: bGetPoint
 * true - учитывать точку как составную часть слова (для получения числа константы)
 * Возвращаемое значение:
 * Слово из буфера
 */
wxString CTranslateModule::GetWord(bool bOrigin, bool bGetPoint, wxString* psOrig)
{
	SkipSpaces();

	if (m_nCurPos >= m_nSizeText)
		SetError(ERROR_TRANSLATE_WORD, m_nCurPos);

	int nNext = m_nCurPos;

	for (unsigned int i = m_nCurPos; i < m_nSizeText; i++)
	{
		char c = m_sBUFFER[i];

		// if array then break
		if (c == '[' || c == ']') break;

		if ((c == '_') ||
			(c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
			(c >= 'А' && c <= 'Я') || (c >= 'а' && c <= 'я') ||
			(c >= '0' && c <= '9') ||
			(c == '#' && i == m_nCurPos) || //если первый символ # - это служебное слово
			(c == '.' && bGetPoint))
		{
			if (c == '.' && bGetPoint)
				bGetPoint = false; //точка должна встречаться только один раз
			nNext = i + 1;
		}
		else break;
	}

	int nFirst = m_nCurPos;
	m_nCurPos = nNext;
	if (nFirst == nNext) SetError(ERROR_TRANSLATE_WORD, nFirst);

	if (bOrigin)
		return m_sBuffer.Mid(nFirst, nNext - nFirst);
	else {
		if (psOrig)
			*psOrig = m_sBuffer.Mid(nFirst, nNext - nFirst);
		return m_sBUFFER.Mid(nFirst, nNext - nFirst);
	}
}

/**
 * GetStrToEndLine
 * Назначение:
 * Получить всю строку до конца (символа 13 или конца кода программы)
 * Строка
 */
wxString CTranslateModule::GetStrToEndLine() const
{
	unsigned int nStart = m_nCurPos;
	unsigned int i = m_nCurPos;
	for (; i < m_nSizeText; i++) {
		if (m_sBuffer[i] == '\r' || m_sBuffer[i] == '\n') { 
			i++; 
			break; 
		}
	}
	m_nCurPos = i;
	return m_sBuffer.Mid(nStart, m_nCurPos - nStart);
}

/**
 * IsNumber
 * Назначение:
 * Проверить (не изменяя позиции текущего крсора)
 * является ли следующий набор букв числом-константой (пропуская пробелы и пр.)
 * Возвращаемое значение:
 * true,false
*/
bool CTranslateModule::IsNumber() const
{
	SkipSpaces();
	if (m_nCurPos < m_nSizeText)
		return m_sBUFFER[m_nCurPos] >= '0' && m_sBUFFER[m_nCurPos] <= '9';
	return false;
}

/**
 * GetNumber
 * Назначение:
 * Получить из выборки число
 * если никакого число нет, то генерится исключение
 * Возвращаемое значение:
 * Число из буфера
 */
wxString CTranslateModule::GetNumber() const
{
	if (!IsNumber())
		SetError(ERROR_TRANSLATE_NUMBER, m_nCurPos);

	SkipSpaces();

	if (m_nCurPos >= m_nSizeText)
		SetError(ERROR_TRANSLATE_NUMBER, m_nCurPos);

	int nNext = m_nCurPos; bool bGetPoint = true;
	for (unsigned int i = m_nCurPos; i < m_nSizeText; i++) {
		char c = m_sBUFFER[i];
		if ((c >= '0' && c <= '9') || (c == '.' && bGetPoint)) {
			if (c == '.' && bGetPoint)
				bGetPoint = false; //точка должна встречаться только один раз
			nNext = i + 1;
		}
		else break;
	}

	int nFirst = m_nCurPos;
	m_nCurPos = nNext;

	if (nFirst == nNext)
		SetError(ERROR_TRANSLATE_NUMBER, nFirst);

	return m_sBUFFER.Mid(nFirst, nNext - nFirst);
}

/**
 * IsString
 * Назначение:
 * Проверить является ли следующий набор символов (без учета пробелов) строкой-константой, заключенной в кавычки
 * Возвращаемое значение:
 * true,false
 */
bool CTranslateModule::IsString() const
{
	return IsByte('\"') ||
		IsByte('|');
}

/**
 * GetString
 * Назначение:
 * Получить из выборки строку, заключенную в кавычки
 * если такой строки нет, то генерится исключение
 * Возвращаемое значение:
 * Строка из буфера
 */
wxString CTranslateModule::GetString() const
{
	wxString sString;

	if (!IsString()) {
		SetError(ERROR_TRANSLATE_STRING, m_nCurPos);
	}

	SkipSpaces();

	unsigned int nStart = 0;
	unsigned int i = m_nCurPos + 1;

	for (; i < m_nSizeText; i++) {
		wxChar c = m_sBuffer[i];
		if (c == '\"') {
			if (i < m_nSizeText - 1) {
				i++;
				c = m_sBuffer[i];
				if (c != '\"')
					break;
			}
		}
		else {
			if (c == '\n') {
				if (nStart > 0)
					break;
				unsigned int pos = i + 1; bool hasNewLine = false;
				for (; pos < m_nSizeText; pos++) {
					if (m_sBuffer[pos] == '\n') {
						hasNewLine = false;
						break;
					}
					if (m_sBuffer[pos] == '|') {
						hasNewLine = true;
						break;
					}
				}
				i = pos - 1;
				if (!hasNewLine) {
					break;
				}
				nStart = sString.length();
			}
			else if (c == '|' && nStart > 0) { //поддержка многострочного текста
				//удаление ненужной части
				sString = sString.Left(nStart + 1);
				m_nCurLine++;
				nStart = 0;
				continue;
			}
			else if (c == '/' && nStart > 0) {
				if (m_sBuffer[i + 1] == '/') { //это комментарий - пропускаем
					i = m_sBuffer.find('\n', i + 1);
					if (i < 0)
					{
						i = m_nSizeText;
						break;
					}
					continue;
				}
			}
		}
		sString += c;
	}
	m_nCurPos = i;
	return sString;
}

/**
* IsDate
* Назначение:
* Проверить является ли следующий набор символов (без учета пробелов) датой-константой, заключенной в апострофы
* Возвращаемое значение:
* true,false
*/
bool CTranslateModule::IsDate() const
{
	return IsByte('\'');
}

/**
* GetDate
* Назначение:
* Получить из выборки дату, заключенную в апострофы
* если такой даты нет, то генерится исключение
* Возвращаемое значение:
* Дата из буфера, заключенная в кавычки
*/
wxString CTranslateModule::GetDate() const
{
	if (!IsDate()) {
		SetError(ERROR_TRANSLATE_DATE, m_nCurPos);
	}

	unsigned int nCount = 0;

	SkipSpaces();

	if (m_nCurPos >= m_nSizeText) {
		SetError(ERROR_TRANSLATE_WORD, m_nCurPos);
	}

	int nNext = m_nCurPos;

	for (unsigned int i = m_nCurPos; i < m_nSizeText; i++) {
		if (m_sBUFFER[i] == '\n') {
			nNext = m_nCurPos + 1;
			break;
		}
		if (nCount < 2) {
			if (m_sBUFFER[i] == '\'')
				nCount++;
			nNext = i + 1;
		}
		else {
			break;
		}
	}

	int nFirst = m_nCurPos;
	m_nCurPos = nNext;
	if (nFirst == nNext) {
		SetError(ERROR_TRANSLATE_DATE, nFirst);
	}

	return m_sBuffer.Mid(nFirst + 1, nNext - nFirst - 2);
}

/**
 * IsEnd
 * Назначение:
 * Проверить признак окончания транслирования (т.е. дошли до конца буфера)
 * Возвращаемое значение:
 * true,false
 */
bool CTranslateModule::IsEnd() const
{
	SkipSpaces();

	if (m_nCurPos < m_nSizeText)
		return false;

	return true;
}

/**
 * IsKeyWord
 * Назначение:
 * Определяет явлеется ли заданное слово служебным оператором
 * Возвращаемое значение,если:
 * -1: нет
 * бельше или равно 0: номер в списке служебных слов
*/
#if defined(_LP64) || defined(__LP64__) || defined(__arch64__) || defined(_WIN64)
long long CTranslateModule::IsKeyWord(const wxString& sKeyWord)
#else 
int CTranslateModule::IsKeyWord(const wxString& sKeyWord)
#endif
{
	auto itHashKeyWords = m_aHashKeyWords.find(StringUtils::MakeUpper(sKeyWord));
#if defined(_LP64) || defined(__LP64__) || defined(__arch64__) || defined(_WIN64)
	if (itHashKeyWords != m_aHashKeyWords.end())
		return ((long long)itHashKeyWords->second) - 1;
#else 
	if (itHashKeyWords != m_aHashKeyWords.end())
		return ((int)itHashKeyWords->second) - 1;
#endif 
	return wxNOT_FOUND;
}

/**
 * PrepareLexem
 * ПРОХОД1 - загрузка лексем для последующего быстрого доступа при распознавании
 */
bool CTranslateModule::PrepareLexem()
{
	if (!m_aDefList)
	{
		m_aDefList = new CDefList();
		m_aDefList->SetParent(&glDefList);
		m_bAutoDeleteDefList = true;//признак, что массив с определениями создали мы (а не передан в качестве трансляции определений)
	}

	wxString s;

	m_aLexemList.clear();

	while (!IsEnd()) {

		lexem_t bytecode;

		bytecode.m_sModuleName = m_sModuleName;
		bytecode.m_sDocPath = m_sDocPath;
		bytecode.m_sFileName = m_sFileName;

		bytecode.m_nNumberLine = m_nCurLine;
		bytecode.m_nNumberString = m_nCurPos;//если в дальнейшем произойдет ошибка, то именно эту строку нужно выдать пользователю

		if (IsWord()) {
			wxString sOrig;
			s = GetWord(false, false, &sOrig);
			//обработка определений пользователя (#define)
			if (m_aDefList->HasDef(s)) {
				CLexemList* pDef = m_aDefList->GetDef(s);
				for (unsigned int i = 0; i < pDef->size(); i++) {
					lexem_t* lex = pDef[i].data();
					lex->m_nNumberString = m_nCurPos;
					lex->m_nNumberLine = m_nCurLine;//для точек останова
					lex->m_sModuleName = m_sModuleName;
					lex->m_sDocPath = m_sDocPath;
					lex->m_sFileName = m_sFileName;
					m_aLexemList.push_back(*lex);
				}
				continue;
			}

			//undefined
			if (s.Lower() == wxT("undefined")) {
				bytecode.m_nType = CONSTANT;
				bytecode.m_vData.SetType(eValueTypes::TYPE_EMPTY);
			}
			//boolean
			else if (s.Lower() == wxT("true") || s.Lower() == wxT("false")) {
				bytecode.m_nType = CONSTANT;
				bytecode.m_vData.SetBoolean(s);
			}
			//null
			else if (s.Lower() == wxT("null")) {
				bytecode.m_nType = CONSTANT;
				bytecode.m_vData.SetType(eValueTypes::TYPE_NULL);
			}

			if (bytecode.m_nType != CONSTANT) {
				int n = IsKeyWord(s);
				bytecode.m_vData = sOrig;
				if (n >= 0) {
					bytecode.m_nType = KEYWORD;
					bytecode.m_nData = n;
				}
				else {
					bytecode.m_nType = IDENTIFIER;
				}
			}
		}
		else if (IsNumber() || IsString() || IsDate()) {
			bytecode.m_nType = CONSTANT;
			if (IsNumber()) {
				bool isSetNumber = bytecode.m_vData.SetNumber(GetNumber());
				if (!isSetNumber)
					SetError(ERROR_TRANSLATE_NUMBER, m_nCurPos);
				int n = m_aLexemList.size() - 1;
				if (n >= 0) {
					if (m_aLexemList[n].m_nType == DELIMITER && (m_aLexemList[n].m_nData == '-' || m_aLexemList[n].m_nData == '+')) {
						n--;
						if (n >= 0) {
							if (m_aLexemList[n].m_nType == DELIMITER &&
								(
									m_aLexemList[n].m_nData == '[' ||
									m_aLexemList[n].m_nData == '(' ||
									m_aLexemList[n].m_nData == ',' ||
									m_aLexemList[n].m_nData == '<' ||
									m_aLexemList[n].m_nData == '>' ||
									m_aLexemList[n].m_nData == '=')) {
								n++;
								if (m_aLexemList[n].m_nData == '-')
									bytecode.m_vData.m_fData = -bytecode.m_vData.m_fData;
								m_aLexemList[n] = bytecode;
								continue;
							}
						}
					}
				}
			}
			else {
				if (IsString()) {
					bool isSetString = bytecode.m_vData.SetString(GetString());
					if (!isSetString)
						SetError(ERROR_TRANSLATE_STRING, m_nCurPos);
				}
				else if (IsDate()) {
					bool isSetDate = bytecode.m_vData.SetDate(GetDate());
					if (!isSetDate)
						SetError(ERROR_TRANSLATE_DATE, m_nCurPos);
				}
			}

			m_aLexemList.push_back(bytecode);
			continue;
		}
		else if (IsByte('~')) {
			s.clear();

			GetByte();//пропускаем разделитель и вспомог. символ метки (как лишние)
			continue;
		}
		else {

			s.clear();

			bytecode.m_nType = DELIMITER;
			bytecode.m_nData = GetByte();

			if (bytecode.m_nData <= 13) {
				continue;
			}
		}
		bytecode.m_sData = s;
		if (bytecode.m_nType == KEYWORD) {
			if (bytecode.m_nData == KEY_DEFINE && m_nModePreparing != LEXEM_ADDDEF)//задание произвольного идентификатора
			{
				if (!IsWord()) {
					SetError(ERROR_IDENTIFIER_DEFINE, m_nCurPos);
				}

				wxString sName = GetWord();

				//результат транслирования добавляем в список определений
				if (LEXEM_ADD == m_nModePreparing)
					PrepareFromCurrent(LEXEM_ADDDEF, sName);
				else
					PrepareFromCurrent(LEXEM_IGNORE, sName);

				continue;
			}
			else if (bytecode.m_nData == KEY_UNDEF) {//удаление идентификатора
				if (!IsWord()) {
					SetError(ERROR_IDENTIFIER_DEFINE, m_nCurPos);
				}

				wxString sName = GetWord();
				m_aDefList->RemoveDef(sName);
				continue;
			}
			else if (bytecode.m_nData == KEY_IFDEF || bytecode.m_nData == KEY_IFNDEF) { //условное компилирование	
				if (!IsWord()) {
					SetError(ERROR_IDENTIFIER_DEFINE, m_nCurPos);
				}

				wxString sName = GetWord();
				bool bHasDef = m_aDefList->HasDef(sName);

				if (bytecode.m_nData == KEY_IFNDEF)
					bHasDef = !bHasDef;

				//транслируем весь блок пока не встретится #else или #endif
				int nMode = 0;

				if (bHasDef)
					nMode = LEXEM_ADD;//результат транслирования добавляем в список лексем
				else
					nMode = LEXEM_IGNORE;//иначе игнорируем
				PrepareFromCurrent(nMode);

				if (!IsWord()) {
					SetError(ERROR_USE_ENDDEF, m_nCurPos);
				}

				wxString sWord = GetWord();
				if (IsKeyWord(sWord) == KEY_ELSEDEF) {//вдруг #else

					//еще раз транслируем
					if (!bHasDef)
						nMode = LEXEM_ADD;//результат транслирования добавляем в список лексем
					else
						nMode = LEXEM_IGNORE;//иначе игнорируем
					PrepareFromCurrent(nMode);

					if (!IsWord()) {
						SetError(ERROR_USE_ENDDEF, m_nCurPos);
					}

					sWord = GetWord();
				}

				//Требуем #endif
				if (IsKeyWord(sWord) != KEY_ENDIFDEF) {
					SetError(ERROR_USE_ENDDEF, m_nCurPos);
				}

				continue;
			}
			else if (bytecode.m_nData == KEY_ENDIFDEF) {//конец условного компилирования	
				m_nCurPos = bytecode.m_nNumberString;//здесь мы сохраняли предыдущее значение
				break;
			}
			else if (bytecode.m_nData == KEY_ELSEDEF) {//"Иначе" условного компилирования
				//возвращаемся на начала условного оператора
				m_nCurPos = bytecode.m_nNumberString;//здесь мы сохраняли предыдущее значение
				break;
			}
			else if (bytecode.m_nData == KEY_REGION) {
				if (!IsWord()) {
					SetError(ERROR_IDENTIFIER_REGION, m_nCurPos);
				}

				/*wxString sName = */GetWord();

				PrepareFromCurrent(LEXEM_ADD);

				if (!IsWord()) {
					SetError(ERROR_USE_ENDREGION, m_nCurPos);
				}

				wxString sWord = GetWord();

				//Требуем #endregion
				if (IsKeyWord(sWord) != KEY_ENDREGION) {
					SetError(ERROR_USE_ENDREGION, m_nCurPos);
				}

				continue;
			}
			else if (bytecode.m_nData == KEY_ENDREGION)
			{
				m_nCurPos = bytecode.m_nNumberString;//здесь мы сохраняли предыдущее значение
				break;
			}
		}

		m_aLexemList.push_back(bytecode);
	}

	for (auto translateModule : m_aTranslateModules) {

		if (!translateModule->PrepareLexem()) {
			return false;
		}

		for (auto lex : translateModule->GetLexems()) {
			if (lex.m_nType != ENDPROGRAM) {
				m_aLexemList.push_back(lex);
			}
		}

		translateModule->m_nCurPos = 0;
		translateModule->m_nCurLine = 0;
	}

	lexem_t bytecode;

	bytecode.m_nType = ENDPROGRAM;
	bytecode.m_nData = 0;
	bytecode.m_nNumberString = m_nCurPos;

	bytecode.m_sModuleName = m_sModuleName;
	bytecode.m_sDocPath = m_sDocPath;
	bytecode.m_sFileName = m_sFileName;

	m_aLexemList.push_back(bytecode);

	return true;
}

CDefList::~CDefList()
{
	m_aDefList.clear();
}

void CDefList::RemoveDef(const wxString& sName)
{
	m_aDefList.erase(StringUtils::MakeUpper(sName));
}

bool CDefList::HasDef(const wxString& sName) const
{
	wxString m_csName = StringUtils::MakeUpper(sName);
	auto m_find_val = m_aDefList.find(m_csName);
	if (m_find_val != m_aDefList.end()) return true;

	static int nLevel = 0;

	nLevel++;

	if (nLevel > MAX_OBJECTS_LEVEL) {
		CTranslateError::Error("Рекурсивный вызов модулей (#3)");
	}

	//ищем в родителях
	bool bRes = false;
	if (m_pParent)
		bRes = m_pParent->HasDef(sName);
	nLevel--;
	return bRes;
}

CLexemList* CDefList::GetDef(const wxString& sName)
{
	auto itDefList = m_aDefList.find(StringUtils::MakeUpper(sName));
	if (itDefList != m_aDefList.end())
		return itDefList->second;

	//ищем в родителях
	if (m_pParent && m_pParent->HasDef(sName))
		return m_pParent->GetDef(sName);

	CLexemList* m_lexList = new CLexemList();
	m_aDefList[StringUtils::MakeUpper(sName)] = m_lexList;
	return m_lexList;
}

void CDefList::SetDef(const wxString& sName, CLexemList* pDef)
{
	CLexemList* pList = GetDef(sName);
	pList->clear();
	if (pDef != NULL) {
		for (unsigned int i = 0; i < pDef->size(); i++) {
			pList->push_back(*pDef[i].data());
		}
	}
}

void CDefList::SetDef(const wxString& sName, const wxString &sValue) 
{
	CLexemList List;
	if (sValue.length() > 0) {
		lexem_t Lex;
		Lex.m_nType = CONSTANT;
		if (sValue[0] == '-' || sValue[0] == '+' || (sValue[0] >= '0' && sValue[0] <= '9'))//число
			Lex.m_vData.SetNumber(sValue);
		else
			Lex.m_vData.SetString(sValue);
		List.push_back(Lex);
		SetDef(StringUtils::MakeUpper(sName), &List);
	}
	else {
		SetDef(StringUtils::MakeUpper(sName), NULL);
	}
}

/**
 * создание лексем начиная с текущей позиции
 */
void CTranslateModule::PrepareFromCurrent(int nMode, const wxString& sName)
{
	CTranslateModule translate;
	translate.m_aDefList = m_aDefList;
	translate.m_nModePreparing = nMode;
	translate.m_sModuleName = m_sModuleName;
	translate.Load(m_sBuffer);

	//начальный номер строки
	translate.m_nCurLine = m_nCurLine;
	translate.m_nCurPos = m_nCurPos;

	//конечный номер строки
	if (nMode == LEXEM_ADDDEF) {
		GetStrToEndLine();
		translate.m_nSizeText = m_nCurPos;
	}

	translate.PrepareLexem();

	if (nMode == LEXEM_ADDDEF) {
		m_aDefList->SetDef(sName, &translate.m_aLexemList);
		m_nCurLine = translate.m_nCurLine;
	}
	else if (nMode == LEXEM_ADD) {
		for (unsigned int i = 0; i < translate.m_aLexemList.size() - 1; i++) {//без учета ENDPROGRAM
			m_aLexemList.push_back(translate.m_aLexemList[i]);
		}

		m_nCurPos = translate.m_nCurPos;
		m_nCurLine = translate.m_nCurLine;
	}
	else {
		m_nCurPos = translate.m_nCurPos;
		m_nCurLine = translate.m_nCurLine;
	}
}

void CTranslateModule::AppendModule(CTranslateModule* module)
{
	auto foundedIt = std::find(m_aTranslateModules.begin(), m_aTranslateModules.end(), module);
	if (foundedIt == m_aTranslateModules.end()) {
		m_aTranslateModules.push_back(module);
	}
}

void CTranslateModule::RemoveModule(CTranslateModule* module)
{
	auto foundedIt = std::find(m_aTranslateModules.begin(), m_aTranslateModules.end(), module);
	if (foundedIt != m_aTranslateModules.end()) {
		m_aTranslateModules.erase(foundedIt);
	}
}

void CTranslateModule::OnSetParent(CTranslateModule* setParent)
{
	if (!m_aDefList) {
		m_aDefList = new CDefList;
		m_aDefList->SetParent(&glDefList);
		m_bAutoDeleteDefList = true;//признак автоудаления
	}

	if (setParent) {
		m_aDefList->SetParent(setParent->m_aDefList);
	}
	else {
		m_aDefList->SetParent(&glDefList);
	}
}

class CAutoLoader
{
public:

	//старт программы
	CAutoLoader() {
		CTranslateModule::LoadKeyWords();
	}

	//завершение программы
	~CAutoLoader() {}

} m_autoLoader;