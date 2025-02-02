////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : parser for autocomplete 
////////////////////////////////////////////////////////////////////////////

#include "codeEditorParser.h"

#pragma warning(disable : 4018)

CParserModule::CParserModule() : CTranslateCode(), m_nCurrentCompile(wxNOT_FOUND)
{
}

bool CParserModule::ParseModule(const wxString& sModule)
{
	m_aContentModule.clear();

	//1. Затем разбиваем на лексемы
	Load(sModule);

	try {
		PrepareLexem();
	}
	catch (const CBackendException*)
	{
		return false;
	};

	lexem_t lex;

	while ((lex = GETLexem()).m_nType != ERRORTYPE)
	{
		//пропускаем условие
		if (lex.m_nType == KEYWORD && lex.m_nData == KEY_IF)
		{
			while (m_nCurrentCompile + 1 < m_listLexem.size())
			{
				lex = GETLexem();
				if (lex.m_nType == KEYWORD && lex.m_nData == KEY_THEN) break;
			}

			lex = GETLexem();
		}

		//пропускаем заголовок циклов WHILE
		if (lex.m_nType == KEYWORD && lex.m_nData == KEY_WHILE)
		{
			while (m_nCurrentCompile + 1 < m_listLexem.size())
			{
				lex = GETLexem();
				if (lex.m_nType == KEYWORD && lex.m_nData == KEY_DO) break;
			}
			lex = GETLexem();
		}

		//пропускаем тернарное выражение
		if (lex.m_nType == DELIMITER && lex.m_nData == '?')
		{
			while (m_nCurrentCompile + 1 < m_listLexem.size())
			{
				lex = GETLexem();
				if (lex.m_nType == DELIMITER && lex.m_nData == ';') break;
			}
			lex = GETLexem();
		}

		//Объявление переменных
		if (lex.m_nType == KEYWORD && lex.m_nData == KEY_VAR) {
			while (m_nCurrentCompile + 1 < m_listLexem.size()) {
				wxString strName = GETIdentifier(true);
				int nArrayCount = -1;
				if (IsNextDelimeter('[')) { //это объявление массива
					nArrayCount = 0;
					GETDelimeter('[');
					if (!IsNextDelimeter(']')) {
						CValue vConst = GETConstant();
						if (vConst.GetType() != eValueTypes::TYPE_NUMBER || vConst.GetNumber() < 0)
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

				if (IsNextDelimeter('='))//начальная инициализация - работает только внутри текста модулей (но не пере объявл. процедур и функций)
				{
					if (nArrayCount >= 0) GETDelimeter(',');//Error!
					GETDelimeter('=');
				}

				moduleElement_t data;
				data.strName = strName;
				data.nLineStart = lex.m_nNumberLine;
				data.nLineEnd = lex.m_nNumberLine;
				data.nImage = 358;

				if (bExport)
					data.eType = eContentType::eExportVariable;
				else data.eType = eContentType::eVariable;

				m_aContentModule.push_back(data);

				if (!IsNextDelimeter(',')) break;
				GETDelimeter(',');
			}
		}

		//Объявление функций и процедур 
		if (lex.m_nType == KEYWORD && (lex.m_nData == KEY_FUNCTION || lex.m_nData == KEY_PROCEDURE)) {
			bool isFunction = lex.m_nData == KEY_FUNCTION;
			//вытаскиваем текст объявления функции
			lex = PreviewGetLexem();
			wxString strShortDescription;
			int m_nNumberLine = lex.m_nNumberLine;
			int nRes = m_strBuffer.find('\n', lex.m_nNumberString);
			if (nRes >= 0) {
				strShortDescription = m_strBuffer.Mid(lex.m_nNumberString, nRes - lex.m_nNumberString - 1);
				nRes = strShortDescription.find_first_of('/');
				if (nRes > 0)
				{
					if (strShortDescription[nRes - 1] == '/') {//итак - это комментарий
						strShortDescription = strShortDescription.Mid(nRes + 1);
					}
				}
				else
				{
					nRes = strShortDescription.find_first_of(')');
					strShortDescription = strShortDescription.Left(nRes + 1);
				}
			}

			wxString strFuncName = GETIdentifier(true);

			//компилируем список формальных параметров + регистрируем их как локальные
			GETDelimeter('(');
			if (!IsNextDelimeter(')'))
			{
				while (m_nCurrentCompile + 1 < m_listLexem.size())
				{
					if (IsNextKeyWord(KEY_VAL))
					{
						GETKeyWord(KEY_VAL);
					}

					/*wxString strName =*/ (void)GETIdentifier(true);

					if (IsNextDelimeter('['))//это массив
					{
						GETDelimeter('[');
						GETDelimeter(']');
					}
					else if (IsNextDelimeter('='))
					{
						GETDelimeter('=');
						CValue vConstant = GETConstant();
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

			moduleElement_t data;
			data.strName = strFuncName;
			data.strShortDescription = strShortDescription;
			data.nLineStart = lex.m_nNumberLine;
			data.nLineEnd = lex.m_nNumberLine;

			if (isFunction) {
				data.nImage = 353;
				if (bExport) {
					data.eType = eContentType::eExportFunction;
				}
				else {
					data.eType = eContentType::eFunction;
				}
			}
			else {
				data.nImage = 352;
				if (bExport) { data.eType = eContentType::eExportProcedure; }
				else { data.eType = eContentType::eProcedure; }
			}

			while (m_nCurrentCompile < (m_listLexem.size() - 1)) {

				if (IsNextKeyWord(KEY_ENDFUNCTION)) {
					data.nLineEnd = m_listLexem[m_nCurrentCompile + 1].m_nNumberLine;
					GETKeyWord(KEY_ENDFUNCTION); break;
				}
				else if (IsNextKeyWord(KEY_ENDPROCEDURE)) {
					data.nLineEnd = m_listLexem[m_nCurrentCompile + 1].m_nNumberLine;
					GETKeyWord(KEY_ENDPROCEDURE); break;
				}

				lex = GETLexem();
			}

			m_aContentModule.push_back(data);
		}
	}

	if (m_nCurrentCompile + 1 < m_listLexem.size() - 1)
		return false;
	return true;
}

//variables
wxArrayString CParserModule::GetVariables(bool bOnlyExport)
{
	wxArrayString aVariables;
	for (auto code : m_aContentModule) {
		if (bOnlyExport && code.eType == eContentType::eExportVariable) {
			aVariables.push_back(code.strName);
		}
		else if (!bOnlyExport && (code.eType == eContentType::eExportVariable ||
			code.eType == eContentType::eVariable)) {
			aVariables.push_back(code.strName);
		}
	}
	return aVariables;
}

//functions & procedures 
wxArrayString CParserModule::GetFunctions(bool bOnlyExport)
{
	wxArrayString aFunctions;
	for (auto code : m_aContentModule) {
		if (bOnlyExport && code.eType == eContentType::eExportFunction) {
			aFunctions.push_back(code.strName);
		}
		else if (!bOnlyExport && (code.eType == eContentType::eExportFunction ||
			code.eType == eContentType::eFunction)) {
			aFunctions.push_back(code.strName);
		}
	}
	return aFunctions;
}

wxArrayString CParserModule::GetProcedures(bool bOnlyExport)
{
	wxArrayString aProcedures;

	for (auto code : m_aContentModule) {
		if (bOnlyExport && code.eType == eContentType::eExportProcedure) {
			aProcedures.push_back(code.strName);
		}
		else if (!bOnlyExport && (code.eType == eContentType::eExportProcedure ||
			code.eType == eContentType::eProcedure)) {
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
lexem_t CParserModule::GetLexem()
{
	lexem_t lex;
	if (m_nCurrentCompile + 1 < m_listLexem.size()) {
		lex = m_listLexem[++m_nCurrentCompile];
	}
	return lex;
}

//Получить следующую лексему из списка байт кода без увеличения счетчика текущей позиции
lexem_t CParserModule::PreviewGetLexem()
{
	lexem_t lex;
	while (true) {
		lex = GetLexem();
		if (!(lex.m_nType == DELIMITER && (lex.m_nData == ';' || lex.m_nData == '\n')))
			break;
	}
	m_nCurrentCompile--;
	return lex;
}

/**
 * GETLexem
 * Назначение:
 * Получить следующую лексему из списка байт кода и увеличть счетчик текущей позиции на 1
 * Возвращаемое значение:
 * нет (в случае неудачи генерится исключение)
 */
lexem_t CParserModule::GETLexem()
{
	const lexem_t& lex = GetLexem();
	if (lex.m_nType == ERRORTYPE) {}
	return lex;
}
/**
 * GETDelimeter
 * Назначение:
 * Получить следующую лексему как заданный разделитель
 * Возвращаемое значение:
 * нет (в случае неудачи генерится исключение)
 */
void CParserModule::GETDelimeter(const wxUniChar& c)
{
	lexem_t lex = GETLexem();
	while (!(lex.m_nType == DELIMITER && c == lex.m_nData)) {
		if (m_nCurrentCompile + 1 >= m_listLexem.size())
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
bool CParserModule::IsNextDelimeter(const wxUniChar& c)
{
	if (m_nCurrentCompile + 1 < m_listLexem.size()) {
		const lexem_t& lex = m_listLexem[m_nCurrentCompile + 1];
		if (lex.m_nType == DELIMITER && c == lex.m_nData)
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
bool CParserModule::IsNextKeyWord(int nKey)
{
	if (m_nCurrentCompile + 1 < m_listLexem.size()) {
		const lexem_t& lex = m_listLexem[m_nCurrentCompile + 1];
		if (lex.m_nType == KEYWORD && lex.m_nData == nKey)
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
void CParserModule::GETKeyWord(int nKey)
{
	lexem_t lex = GETLexem();
	while (!(lex.m_nType == KEYWORD && lex.m_nData == nKey)) {
		if (m_nCurrentCompile + 1 >= m_listLexem.size())
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
wxString CParserModule::GETIdentifier(bool strRealName)
{
	const lexem_t& lex = GETLexem();

	if (lex.m_nType != IDENTIFIER) {
		if (strRealName && lex.m_nType == KEYWORD)
			return lex.m_strData;
		return wxEmptyString;
	}

	if (strRealName)
		return lex.m_vData.m_sData;
	else
		return lex.m_strData;
}

/**
 * GETConstant
 * Получить следующую лексему как константу
 * Возвращаемое значение:
 * константа
 */
CValue CParserModule::GETConstant()
{
	lexem_t lex;
	int iNumRequire = 0;
	if (IsNextDelimeter('-') || IsNextDelimeter('+')) {
		iNumRequire = 1;
		if (IsNextDelimeter('-'))
			iNumRequire = -1;
		lex = GETLexem();
	}

	lex = GETLexem();

	if (lex.m_nType != CONSTANT)
		return lex.m_vData;

	if (iNumRequire) {
		//проверка на то чтобы константа имела числовой тип	
		if (lex.m_vData.GetType() != eValueTypes::TYPE_NUMBER)
			return lex.m_vData;
		//меняем знак при минусе
		if (iNumRequire == -1)
			lex.m_vData.m_fData = -lex.m_vData.m_fData;
	}
	return lex.m_vData;
}

