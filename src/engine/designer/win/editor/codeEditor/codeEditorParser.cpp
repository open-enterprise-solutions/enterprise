////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : parser for autocomplete 
////////////////////////////////////////////////////////////////////////////

#include "codeEditorParser.h"

#pragma warning(disable : 4018)

ibParserModule::ibParserModule() : ibTranslateCode(), m_numCurrentCompile(wxNOT_FOUND)
{
}

bool ibParserModule::ParseModule(const wxString& sModule)
{
	m_aContentModule.clear();

	//1. Затем разбиваем на лексемы
	Load(sModule);

	try {
		PrepareLexem();
	}
	catch (const ibBackendException*)
	{
		return false;
	};

	ibLexem lex;

	while ((lex = GETLexem()).m_lexType != ERRORTYPE)
	{
		//пропускаем условие
		if (lex.m_lexType == KEYWORD && lex.m_numData == KEY_IF)
		{
			while (m_numCurrentCompile + 1 < m_listLexem.size())
			{
				lex = GETLexem();
				if (lex.m_lexType == KEYWORD && lex.m_numData == KEY_THEN) break;
			}

			lex = GETLexem();
		}

		//пропускаем заголовок циклов WHILE
		if (lex.m_lexType == KEYWORD && lex.m_numData == KEY_WHILE)
		{
			while (m_numCurrentCompile + 1 < m_listLexem.size())
			{
				lex = GETLexem();
				if (lex.m_lexType == KEYWORD && lex.m_numData == KEY_DO) break;
			}
			lex = GETLexem();
		}

		//пропускаем тернарное выражение
		if (lex.m_lexType == DELIMITER && lex.m_numData == '?')
		{
			while (m_numCurrentCompile + 1 < m_listLexem.size())
			{
				lex = GETLexem();
				if (lex.m_lexType == DELIMITER && lex.m_numData == ';') break;
			}
			lex = GETLexem();
		}

		//Объявление переменных
		if (lex.m_lexType == KEYWORD && lex.m_numData == KEY_VAR) {
			while (m_numCurrentCompile + 1 < m_listLexem.size()) {
				wxString strName = GETIdentifier(true);
				int nArrayCount = -1;
				if (IsNextDelimeter('[')) { // this is an array declaration
					nArrayCount = 0;
					GETDelimeter('[');
					if (!IsNextDelimeter(']')) {
						ibValue vConst = GETConstant();
						if (vConst.GetType() != ibValueTypes::TYPE_NUMBER || vConst.GetNumber() < 0)
							continue;
						nArrayCount = vConst.GetInteger();
					}
					GETDelimeter(']');
				}

				bool bExport = false;

				if (IsNextKeyWord(KEY_EXPORT))
				{
					GETKeyWord(KEY_EXPORT);
					bExport = true;
				}

				if (IsNextDelimeter('='))// initial initialization - works only inside the text of modules (but not re-declaring procedures and functions)
				{
					if (nArrayCount >= 0) GETDelimeter(',');//Error!
					GETDelimeter('=');
				}

				ibModuleElement data;
				data.strName = strName;
				data.nLineStart = lex.m_numLine;
				data.nLineEnd = lex.m_numLine;
				data.nImage = 358;

				if (bExport)
					data.eType = ibContentType::eExportVariable;
				else data.eType = ibContentType::eVariable;

				m_aContentModule.push_back(data);

				if (!IsNextDelimeter(',')) break;
				GETDelimeter(',');
			}
		}

		//Объявление функций и процедур 
		if (lex.m_lexType == KEYWORD && (lex.m_numData == KEY_FUNCTION || lex.m_numData == KEY_PROCEDURE)) {
			bool isFunction = lex.m_numData == KEY_FUNCTION;
			// pull out the text of the function declaration
			lex = PreviewGetLexem();
			wxString strShortDescription;
			int m_numLine = lex.m_numLine;
			int nRes = m_strBuffer.find('\n', lex.m_numString);
			if (nRes >= 0) {
				strShortDescription = m_strBuffer.substr(lex.m_numString, nRes - lex.m_numString - 1);
				nRes = strShortDescription.find_first_of('/');
				if (nRes > 0)
				{
					if (strShortDescription[nRes - 1] == '/') {// so this is a comment
						strShortDescription = strShortDescription.substr(nRes + 1);
					}
				}
				else
				{
					nRes = strShortDescription.find_first_of(')');
					strShortDescription = strShortDescription.substr(0, nRes + 1);
				}
			}

			wxString strFuncName = GETIdentifier(true);

			// compile the list of formal parameters + register them as local
			GETDelimeter('(');
			if (!IsNextDelimeter(')'))
			{
				while (m_numCurrentCompile + 1 < m_listLexem.size())
				{
					if (IsNextKeyWord(KEY_VAL))
					{
						GETKeyWord(KEY_VAL);
					}

					/*wxString strName =*/ (void)GETIdentifier(true);

					if (IsNextDelimeter('['))// this is an array
					{
						GETDelimeter('[');
						GETDelimeter(']');
					}
					else if (IsNextDelimeter('='))
					{
						GETDelimeter('=');
						ibValue vConstant = GETConstant();
					}

					if (IsNextDelimeter(')')) break;

					GETDelimeter(',');
				}
			}

			GETDelimeter(')');

			bool bExport = false;

			if (IsNextKeyWord(KEY_EXPORT))
			{
				GETKeyWord(KEY_EXPORT); bExport = true;
			}

			ibModuleElement data;
			data.strName = strFuncName;
			data.strShortDescription = strShortDescription;
			data.nLineStart = lex.m_numLine;
			data.nLineEnd = lex.m_numLine;

			if (isFunction) {
				data.nImage = 353;
				if (bExport) {
					data.eType = ibContentType::eExportFunction;
				}
				else {
					data.eType = ibContentType::eFunction;
				}
			}
			else {
				data.nImage = 352;
				if (bExport) { data.eType = ibContentType::eExportProcedure; }
				else { data.eType = ibContentType::eProcedure; }
			}

			while (m_numCurrentCompile < (m_listLexem.size() - 1)) {

				if (IsNextKeyWord(KEY_ENDFUNCTION)) {
					data.nLineEnd = m_listLexem[m_numCurrentCompile + 1].m_numLine;
					GETKeyWord(KEY_ENDFUNCTION); break;
				}
				else if (IsNextKeyWord(KEY_ENDPROCEDURE)) {
					data.nLineEnd = m_listLexem[m_numCurrentCompile + 1].m_numLine;
					GETKeyWord(KEY_ENDPROCEDURE); break;
				}

				lex = GETLexem();
			}

			m_aContentModule.push_back(data);
		}
	}

	if (m_numCurrentCompile + 1 < m_listLexem.size() - 1)
		return false;
	return true;
}

//variables
wxArrayString ibParserModule::GetVariables(bool bOnlyExport)
{
	wxArrayString aVariables;
	for (auto code : m_aContentModule) {
		if (bOnlyExport && code.eType == ibContentType::eExportVariable) {
			aVariables.push_back(code.strName);
		}
		else if (!bOnlyExport && (code.eType == ibContentType::eExportVariable ||
			code.eType == ibContentType::eVariable)) {
			aVariables.push_back(code.strName);
		}
	}
	return aVariables;
}

//functions & procedures 
wxArrayString ibParserModule::GetFunctions(bool bOnlyExport)
{
	wxArrayString aFunctions;
	for (auto code : m_aContentModule) {
		if (bOnlyExport && code.eType == ibContentType::eExportFunction) {
			aFunctions.push_back(code.strName);
		}
		else if (!bOnlyExport && (code.eType == ibContentType::eExportFunction ||
			code.eType == ibContentType::eFunction)) {
			aFunctions.push_back(code.strName);
		}
	}
	return aFunctions;
}

wxArrayString ibParserModule::GetProcedures(bool bOnlyExport)
{
	wxArrayString aProcedures;

	for (auto code : m_aContentModule) {
		if (bOnlyExport && code.eType == ibContentType::eExportProcedure) {
			aProcedures.push_back(code.strName);
		}
		else if (!bOnlyExport && (code.eType == ibContentType::eExportProcedure ||
			code.eType == ibContentType::eProcedure)) {
			aProcedures.push_back(code.strName);
		}
	}

	return aProcedures;
}

/**
 * GetLexem
 * Назначение:
 * Получить следующую лексему из списка байт кода и увеличть счетчик текущей позиции на 1
 * Возвращаемое значение:
 * 0 или указатель на лексему
 */
ibLexem ibParserModule::GetLexem()
{
	ibLexem lex;
	if (m_numCurrentCompile + 1 < m_listLexem.size()) {
		lex = m_listLexem[++m_numCurrentCompile];
	}
	return lex;
}

//Получить следующую лексему из списка байт кода без увеличения счетчика текущей позиции
ibLexem ibParserModule::PreviewGetLexem()
{
	ibLexem lex;
	while (true) {
		lex = GetLexem();
		if (!(lex.m_lexType == DELIMITER && (lex.m_numData == ';' || lex.m_numData == '\n')))
			break;
	}
	m_numCurrentCompile--;
	return lex;
}

/**
 * GETLexem
 * Назначение:
 * Получить следующую лексему из списка байт кода и увеличть счетчик текущей позиции на 1
 * Возвращаемое значение:
 * нет (в случае неудачи генерится исключение)
 */
ibLexem ibParserModule::GETLexem()
{
	const ibLexem& lex = GetLexem();
	if (lex.m_lexType == ERRORTYPE) {}
	return lex;
}
/**
 * GETDelimeter
 * Назначение:
 * Получить следующую лексему как заданный разделитель
 * Возвращаемое значение:
 * нет (в случае неудачи генерится исключение)
 */
void ibParserModule::GETDelimeter(const wxUniChar& c)
{
	ibLexem lex = GETLexem();
	while (!(lex.m_lexType == DELIMITER && c == lex.m_numData)) {
		if (m_numCurrentCompile + 1 >= m_listLexem.size())
			break;
		lex = GETLexem();
	}
}
/**
 * IsNextDelimeter
 * Назначение:
 * Проверить является ли следующая лексема байт-кода заданным разделителем
 * Возвращаемое значение:
 * true,false
 */
bool ibParserModule::IsNextDelimeter(const wxUniChar& c)
{
	if (m_numCurrentCompile + 1 < m_listLexem.size()) {
		const ibLexem& lex = m_listLexem[m_numCurrentCompile + 1];
		if (lex.m_lexType == DELIMITER && c == lex.m_numData)
			return true;
	}
	return false;
}

/**
 * IsNextKeyWord
 * Назначение:
 * Проверить является ли следующая лексема байт-кода заданным ключевым словом
 * Возвращаемое значение:
 * true,false
 */
bool ibParserModule::IsNextKeyWord(int nKey)
{
	if (m_numCurrentCompile + 1 < m_listLexem.size()) {
		const ibLexem& lex = m_listLexem[m_numCurrentCompile + 1];
		if (lex.m_lexType == KEYWORD && lex.m_numData == nKey)
			return true;
	}
	return false;
}

/**
 * GETKeyWord
 * Получить следующую лексему как заданное ключевое слово
 * Возвращаемое значение:
 * нет (в случае неудачи генерится исключение)
 */
void ibParserModule::GETKeyWord(int nKey)
{
	ibLexem lex = GETLexem();
	while (!(lex.m_lexType == KEYWORD && lex.m_numData == nKey)) {
		if (m_numCurrentCompile + 1 >= m_listLexem.size())
			break;
		lex = GETLexem();
	}
}

/**
 * GETIdentifier
 * Получить следующую лексему как заданное ключевое слово
 * Возвращаемое значение:
 * строка-идентификатор
 */
wxString ibParserModule::GETIdentifier(bool strRealName)
{
	const ibLexem& lex = GETLexem();

	if (lex.m_lexType != IDENTIFIER) {
		if (strRealName && lex.m_lexType == KEYWORD)
			return lex.m_strData;
		return wxEmptyString;
	}

	if (strRealName)
		return lex.m_valData.m_sData;
	else
		return lex.m_strData;
}

/**
 * GETConstant
 * Получить следующую лексему как константу
 * Возвращаемое значение:
 * константа
 */
ibValue ibParserModule::GETConstant()
{
	ibLexem lex;
	int iNumRequire = 0;
	if (IsNextDelimeter('-') || IsNextDelimeter('+')) {
		iNumRequire = 1;
		if (IsNextDelimeter('-'))
			iNumRequire = -1;
		lex = GETLexem();
	}

	lex = GETLexem();

	if (lex.m_lexType != CONSTANT)
		return lex.m_valData;

	if (iNumRequire) {
		// check that the constant is of numeric type	
		if (lex.m_valData.GetType() != ibValueTypes::TYPE_NUMBER)
			return lex.m_valData;
		// change sign for minus
		if (iNumRequire == -1)
			lex.m_valData.m_fData = -lex.m_valData.m_fData;
	}
	return lex.m_valData;
}

