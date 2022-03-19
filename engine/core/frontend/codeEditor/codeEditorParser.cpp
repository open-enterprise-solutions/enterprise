////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : parser for autocomplete 
////////////////////////////////////////////////////////////////////////////

#include "codeEditorParser.h"
#include "compiler/definition.h"

#pragma warning(disable : 4018)

CParserModule::CParserModule() : CTranslateModule(), m_nCurrentCompile(wxNOT_FOUND)
{
}

bool CParserModule::ParseModule(const wxString &sModule)
{
	m_aContentModule.clear();

	//1. Затем разбиваем на лексемы
	Load(sModule);

	try
	{
		PrepareLexem();
	}
	catch (const CTranslateError *)
	{
		return false;
	};

	CLexem lex;

	while ((lex = GETLexem()).m_nType != ERRORTYPE)
	{
		//пропускаем условие
		if (lex.m_nType == KEYWORD && lex.m_nData == KEY_IF)
		{
			while (m_nCurrentCompile + 1 < m_aLexemList.size())
			{
				lex = GETLexem();
				if (lex.m_nType == KEYWORD && lex.m_nData == KEY_THEN) break;
			}

			lex = GETLexem();
		}

		//пропускаем заголовок циклов WHILE
		if (lex.m_nType == KEYWORD && lex.m_nData == KEY_WHILE)
		{
			while (m_nCurrentCompile + 1 < m_aLexemList.size())
			{
				lex = GETLexem();
				if (lex.m_nType == KEYWORD && lex.m_nData == KEY_DO) break;
			}
			lex = GETLexem();
		}

		//пропускаем тернарное выражение
		if (lex.m_nType == DELIMITER && lex.m_nData == '?')
		{
			while (m_nCurrentCompile + 1 < m_aLexemList.size())
			{
				lex = GETLexem();
				if (lex.m_nType == DELIMITER && lex.m_nData == ';') break;
			}
			lex = GETLexem();
		}

		//Объявление переменных
		if (lex.m_nType == KEYWORD && lex.m_nData == KEY_VAR)
		{
			while (m_nCurrentCompile + 1 < m_aLexemList.size())
			{
				wxString sName = GETIdentifier(true);

				int nArrayCount = -1;
				if (IsNextDelimeter('['))//это объявление массива
				{
					nArrayCount = 0;

					GETDelimeter('[');
					if (!IsNextDelimeter(']'))
					{
						CValue vConst = GETConstant();
						if (vConst.GetType() != eValueTypes::TYPE_NUMBER || vConst.GetNumber() < 0) continue;
						nArrayCount = vConst.ToInt();
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

				CModuleElementInfo data;
				data.sName = sName;
				data.nLineStart = lex.m_nNumberLine;
				data.nLineEnd = lex.m_nNumberLine;
				data.nImage = 358;

				if (bExport) data.eType = eContentType::eExportVariable;
				else data.eType = eContentType::eVariable;

				m_aContentModule.push_back(data);

				if (!IsNextDelimeter(',')) break;
				GETDelimeter(',');
			}
		}

		//Объявление функций и процедур 
		if (lex.m_nType == KEYWORD && (lex.m_nData == KEY_FUNCTION || lex.m_nData == KEY_PROCEDURE))
		{
			bool isFunction = lex.m_nData == KEY_FUNCTION;

			//вытаскиваем текст объявления функции
			lex = PreviewGetLexem();
			wxString sShortDescription;
			int m_nNumberLine = lex.m_nNumberLine;
			int nRes = m_sBuffer.find('\n', lex.m_nNumberString);
			if (nRes >= 0)
			{
				sShortDescription = m_sBuffer.Mid(lex.m_nNumberString, nRes - lex.m_nNumberString - 1);
				nRes = sShortDescription.find_first_of('/');
				if (nRes > 0)
				{
					if (sShortDescription[nRes - 1] == '/')//итак - это комментарий
					{
						sShortDescription = sShortDescription.Mid(nRes + 1);
					}
				}
				else
				{
					nRes = sShortDescription.find_first_of(')');
					sShortDescription = sShortDescription.Left(nRes + 1);
				}
			}

			wxString sFuncName = GETIdentifier(true);

			//компилируем список формальных параметров + регистрируем их как локальные
			GETDelimeter('(');
			if (!IsNextDelimeter(')'))
			{
				while (m_nCurrentCompile + 1 < m_aLexemList.size())
				{
					if (IsNextKeyWord(KEY_VAL))
					{
						GETKeyWord(KEY_VAL);
					}

					/*wxString sName =*/ (void)GETIdentifier(true);

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

			CModuleElementInfo data;
			data.sName = sFuncName;
			data.sShortDescription = sShortDescription;
			data.nLineStart = lex.m_nNumberLine;
			data.nLineEnd = lex.m_nNumberLine;

			if (isFunction) {
				data.nImage = 353;
				if (bExport) { data.eType = eContentType::eExportFunction; }
				else { data.eType = eContentType::eFunction; }
			}
			else {
				data.nImage = 352;
				if (bExport) { data.eType = eContentType::eExportProcedure; }
				else { data.eType = eContentType::eProcedure; }
			}

			while (m_nCurrentCompile < (m_aLexemList.size() - 1)) {

				if (IsNextKeyWord(KEY_ENDFUNCTION)) {
					data.nLineEnd = m_aLexemList[m_nCurrentCompile + 1].m_nNumberLine;
					GETKeyWord(KEY_ENDFUNCTION); break; 
				}
				else if (IsNextKeyWord(KEY_ENDPROCEDURE)) {
					data.nLineEnd = m_aLexemList[m_nCurrentCompile + 1].m_nNumberLine;
					GETKeyWord(KEY_ENDPROCEDURE); break;
				}

				lex = GETLexem();
			}

			m_aContentModule.push_back(data);
		}
	}

	if (m_nCurrentCompile + 1 < m_aLexemList.size() - 1) return false;
	return true;
}

//variables
wxArrayString CParserModule::GetVariables(bool bOnlyExport)
{
	wxArrayString aVariables;

	for (auto code : m_aContentModule)
	{
		if (bOnlyExport && code.eType == eContentType::eExportVariable)
		{
			aVariables.push_back(code.sName);
		}
		else if (!bOnlyExport && (code.eType == eContentType::eExportVariable || code.eType == eContentType::eVariable))
		{
			aVariables.push_back(code.sName);
		}
	}

	return aVariables;
}

//functions & procedures 
wxArrayString CParserModule::GetFunctions(bool bOnlyExport)
{
	wxArrayString aFunctions;

	for (auto code : m_aContentModule)
	{
		if (bOnlyExport && code.eType == eContentType::eExportFunction)
		{
			aFunctions.push_back(code.sName);
		}
		else if (!bOnlyExport && (code.eType == eContentType::eExportFunction || code.eType == eContentType::eFunction))
		{
			aFunctions.push_back(code.sName);
		}
	}

	return aFunctions;
}

wxArrayString CParserModule::GetProcedures(bool bOnlyExport)
{
	wxArrayString aProcedures;

	for (auto code : m_aContentModule)
	{
		if (bOnlyExport && code.eType == eContentType::eExportProcedure)
		{
			aProcedures.push_back(code.sName);
		}
		else if (!bOnlyExport && (code.eType == eContentType::eExportProcedure || code.eType == eContentType::eProcedure))
		{
			aProcedures.push_back(code.sName);
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
CLexem CParserModule::GetLexem()
{
	CLexem lex;
	if (m_nCurrentCompile + 1 < m_aLexemList.size())
	{
		lex = m_aLexemList[++m_nCurrentCompile];
	}
	return lex;
}

//Получить следующую лексему из списка байт кода без увеличения счетчика текущей позиции
CLexem CParserModule::PreviewGetLexem()
{
	CLexem lex;
	while (true)
	{
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
CLexem CParserModule::GETLexem()
{
	CLexem lex = GetLexem();
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
void CParserModule::GETDelimeter(char c)
{
	CLexem lex = GETLexem();
	while (!(lex.m_nType == DELIMITER && lex.m_nData == c)) {
		if (m_nCurrentCompile + 1 >= m_aLexemList.size()) break;
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
bool CParserModule::IsNextDelimeter(char c)
{
	if (m_nCurrentCompile + 1 < m_aLexemList.size())
	{
		CLexem lex = m_aLexemList[m_nCurrentCompile + 1];
		if (lex.m_nType == DELIMITER && lex.m_nData == c) return true;
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
	if (m_nCurrentCompile + 1 < m_aLexemList.size())
	{
		CLexem lex = m_aLexemList[m_nCurrentCompile + 1];
		if (lex.m_nType == KEYWORD && lex.m_nData == nKey) return true;
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
	CLexem lex = GETLexem();
	while (!(lex.m_nType == KEYWORD && lex.m_nData == nKey)) {
		if (m_nCurrentCompile + 1 >= m_aLexemList.size()) break;
		lex = GETLexem();
	}
}

/**
 * GETIdentifier
 * Получить следующую лексему как заданное ключевое слово
 * Возвращаемое значение:
 * строка-идентификатор
 */
wxString CParserModule::GETIdentifier(bool realName)
{
	CLexem lex = GETLexem();

	if (lex.m_nType != IDENTIFIER)
	{
		if (realName && lex.m_nType == KEYWORD) return lex.m_sData;
		return wxEmptyString;
	}

	if (realName) return lex.m_vData.m_sData;
	else return lex.m_sData;
}

/**
 * GETConstant
 * Получить следующую лексему как константу
 * Возвращаемое значение:
 * константа
 */
CValue CParserModule::GETConstant()
{
	CLexem lex;
	int iNumRequire = 0;
	if (IsNextDelimeter('-') || IsNextDelimeter('+'))
	{
		iNumRequire = 1;
		if (IsNextDelimeter('-'))
			iNumRequire = -1;
		lex = GETLexem();
	}

	lex = GETLexem();

	if (lex.m_nType != CONSTANT) return lex.m_vData;

	if (iNumRequire)
	{
		//проверка на то чтобы константа имела числовой тип	
		if (lex.m_vData.GetType() != eValueTypes::TYPE_NUMBER) { return lex.m_vData; }
		//меняем знак при минусе
		if (iNumRequire == -1) lex.m_vData.m_fData = -lex.m_vData.m_fData;
	}
	return lex.m_vData;
}

