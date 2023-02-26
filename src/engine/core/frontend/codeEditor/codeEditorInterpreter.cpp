////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : calculate compile value
////////////////////////////////////////////////////////////////////////////

#include "codeEditorInterpreter.h"
#include "core/compiler/definition.h"
#include "core/metadata/metadata.h"
#include "utils/stringUtils.h"

#pragma warning(push)
#pragma warning(disable : 4018)

//Массив приоритетов математических операций
static std::array<int, 256> s_aPriority = { 0 };

CPrecompileModule::CPrecompileModule(CMetaModuleObject* moduleObject) :
	CTranslateModule(moduleObject->GetFullName(), moduleObject->GetDocPath()),
	m_moduleObject(moduleObject), m_pContext(NULL), m_pCurrentContext(NULL),
	m_nCurrentCompile(wxNOT_FOUND), m_nCurrentPos(0), nLastPosition(0),
	m_bCalcValue(false)
{
	if (!s_aPriority[s_aPriority.size() - 1]) {

		s_aPriority['+'] = 10;
		s_aPriority['-'] = 10;
		s_aPriority['*'] = 30;
		s_aPriority['/'] = 30;
		s_aPriority['%'] = 30;
		s_aPriority['!'] = 50;

		s_aPriority[KEY_OR] = 1;
		s_aPriority[KEY_AND] = 2;

		s_aPriority['>'] = 3;
		s_aPriority['<'] = 3;
		s_aPriority['='] = 3;

		s_aPriority[s_aPriority.size() - 1] = true;
	}

	m_sModuleName = m_moduleObject->GetFullName();
	m_sDocPath = m_moduleObject->GetDocPath();
	m_sFileName = m_moduleObject->GetFileName();

	Load(m_moduleObject->GetModuleText());
}

CPrecompileModule::~CPrecompileModule() {}

void CPrecompileModule::Clear() //Сброс данных для повторного использования объекта
{
	m_pCurrentContext = NULL;

	for (auto function : cContext.cFunctions)
	{
		CPrecompileFunction* pFunction = static_cast<CPrecompileFunction*>(function.second);
		if (pFunction) delete pFunction;
	}

	m_nCurrentCompile = wxNOT_FOUND;

	cContext.cVariables.clear();
	cContext.cFunctions.clear();

	m_valObject.Reset();
}

#include "core/compiler/enumFactory.h"
#include "codeEditorParser.h"

void CPrecompileModule::PrepareModuleData()
{
	IModuleInfo* contextVariable = NULL;

	if (m_moduleObject) {
		IMetadata* metaData = m_moduleObject->GetMetadata();
		wxASSERT(metaData);
		IModuleManager* moduleManager = metaData->GetModuleManager();
		wxASSERT(moduleManager);
		if (!moduleManager->FindCompileModule(m_moduleObject, contextVariable)) {
			wxASSERT_MSG(false, "CPrecompileModule::PrepareModuleData");
		}
		for (auto pair : moduleManager->GetContextVariables()) {
			//добавляем переменные из менеджера
			CValue* managerVariable = pair.second;
			for (unsigned int i = 0; i < managerVariable->GetNProps(); i++)
			{
				wxString sAttributeName = managerVariable->GetPropName(i);

				//определяем номер и тип переменной
				CPrecompileVariable cVariables;
				cVariables.sName = sAttributeName;
				cVariables.sRealName = sAttributeName;

				cVariables.nNumber = i;
				cVariables.bContext = true;
				cVariables.bExport = true;

				cVariables.m_valContext = managerVariable;

				GetContext()->cVariables[StringUtils::MakeUpper(sAttributeName)] = cVariables;
			}
			//добавляем методы из менеджера
			for (unsigned int i = 0; i < managerVariable->GetNMethods(); i++) {
				wxString sMethodName = managerVariable->GetMethodName(i);
				CPrecompileFunction* pFunction = new CPrecompileFunction(sMethodName);
				pFunction->sRealName = sMethodName;
				pFunction->sShortDescription = managerVariable->GetMethodHelper(i);
				pFunction->nStart = i;
				pFunction->bContext = true;
				pFunction->bExport = true;

				pFunction->m_valContext = managerVariable;

				//проверка на типизированность
				GetContext()->cFunctions[StringUtils::MakeUpper(sMethodName)] = pFunction;
			}
		}
		unsigned int nNumberAttr = 0;
		unsigned int nNumberFunc = 0;
		for (auto module : moduleManager->GetCommonModules()) {
			if (module->IsGlobalModule()) {
				CParserModule cParser;
				if (cParser.ParseModule(module->GetModuleText())) {
					for (auto code : cParser.GetAllContent()) {
						if (code.eType == eExportVariable) {
							wxString sAttributeName = code.sName;
							if (cContext.FindVariable(sAttributeName))
								continue;
							//определяем номер и тип переменной
							CPrecompileVariable cVariables;
							cVariables.sName = sAttributeName;
							cVariables.sRealName = sAttributeName;

							cVariables.nNumber = nNumberAttr;
							cVariables.bContext = true;
							cVariables.bExport = true;

							cVariables.m_valContext = module;

							GetContext()->cVariables[StringUtils::MakeUpper(sAttributeName)] = cVariables;	nNumberAttr++;
						}
						else if (code.eType == eExportProcedure) {
							wxString sMethodName = code.sName;
							if (cContext.FindFunction(sMethodName))
								continue;
							CPrecompileContext* procContext = new CPrecompileContext(GetContext());//создаем новый контекст, в котором будем компилировать тело функции
							procContext->SetModule(this);
							procContext->nReturn = RETURN_PROCEDURE;

							CPrecompileFunction* pFunction = new CPrecompileFunction(sMethodName, procContext);
							pFunction->sRealName = sMethodName;
							pFunction->sShortDescription = code.sShortDescription;

							pFunction->nStart = nNumberFunc;
							pFunction->bContext = true;
							pFunction->bExport = true;

							pFunction->m_valContext = module;

							//проверка на типизированность
							GetContext()->cFunctions[StringUtils::MakeUpper(sMethodName)] = pFunction; nNumberFunc++;
						}
						else if (code.eType == eExportFunction) {
							wxString sMethodName = code.sName;
							if (cContext.FindFunction(sMethodName)) continue;

							CPrecompileContext* procContext = new CPrecompileContext(GetContext());//создаем новый контекст, в котором будем компилировать тело функции
							procContext->SetModule(this);
							procContext->nReturn = RETURN_FUNCTION;

							CPrecompileFunction* pFunction = new CPrecompileFunction(sMethodName, procContext);
							pFunction->sRealName = sMethodName;
							pFunction->sShortDescription = code.sShortDescription;

							pFunction->nStart = nNumberFunc;
							pFunction->bContext = true;
							pFunction->bExport = true;

							pFunction->m_valContext = module;

							//проверка на типизированность
							GetContext()->cFunctions[StringUtils::MakeUpper(sMethodName)] = pFunction; nNumberFunc++;
						}
					}
				}
			}
		}
	}

	if (contextVariable != NULL) {
		CValue* pRefData = NULL;
		CCompileModule* compileModule = contextVariable->GetCompileModule();
		while (compileModule != NULL) {
			CMetaModuleObject* moduleObject = compileModule->GetModuleObject();
			if (moduleObject != NULL) {
				IMetadata* metaData = moduleObject->GetMetadata();
				wxASSERT(metaData);
				IModuleManager* moduleManager = metaData->GetModuleManager();
				if (moduleManager->FindCompileModule(moduleObject, pRefData)) {
					//добавляем переменные из контекста
					for (long i = 0; i < pRefData->GetNProps(); i++) {
						wxString sAttributeName = pRefData->GetPropName(i);
						if (cContext.FindVariable(sAttributeName))
							continue;

						//определяем номер и тип переменной
						CPrecompileVariable cVariables;
						cVariables.sName = sAttributeName;
						cVariables.sRealName = sAttributeName;

						cVariables.nNumber = i;
						cVariables.bContext = true;
						cVariables.bExport = true;

						cVariables.m_valContext = pRefData;

						GetContext()->cVariables[StringUtils::MakeUpper(sAttributeName)] = cVariables;
					}

					//добавляем методы из контекста
					for (long i = 0; i < pRefData->GetNMethods(); i++) {
						wxString sMethodName = pRefData->GetMethodName(i);
						if (cContext.FindFunction(sMethodName))
							continue;

						CPrecompileContext* procContext = new CPrecompileContext(GetContext());//создаем новый контекст, в котором будем компилировать тело функции
						procContext->SetModule(this);

						if (pRefData->HasRetVal(i))
							procContext->nReturn = RETURN_FUNCTION;
						else
							procContext->nReturn = RETURN_PROCEDURE;

						CPrecompileFunction* pFunction = new CPrecompileFunction(sMethodName, procContext);
						pFunction->sRealName = sMethodName;
						pFunction->sShortDescription = pRefData->GetMethodHelper(i);
						pFunction->nStart = i;
						pFunction->bContext = true;
						pFunction->bExport = true;

						pFunction->m_valContext = pRefData;

						//проверка на типизированность
						GetContext()->cFunctions[StringUtils::MakeUpper(sMethodName)] = pFunction;
					}

					if (moduleObject != NULL) {
						CParserModule cParser;
						if (cParser.ParseModule(moduleObject->GetModuleText())) {
							unsigned int nNumberAttr = pRefData->GetNProps() + 1;
							unsigned int nNumberFunc = pRefData->GetNMethods() + 1;
							for (auto code : cParser.GetAllContent()) {
								if (code.eType == eExportVariable) {
									wxString sAttributeName = code.sName;
									if (cContext.FindVariable(sAttributeName))
										continue;
									//определяем номер и тип переменной
									CPrecompileVariable cVariables;
									cVariables.sName = sAttributeName;
									cVariables.sRealName = sAttributeName;

									cVariables.nNumber = nNumberAttr;
									cVariables.bContext = true;
									cVariables.bExport = true;

									cVariables.m_valContext = pRefData;

									GetContext()->cVariables[StringUtils::MakeUpper(sAttributeName)] = cVariables;	nNumberAttr++;
								}
								else if (code.eType == eExportProcedure) {
									wxString sMethodName = code.sName;
									if (cContext.FindFunction(sMethodName))
										continue;

									CPrecompileContext* procContext = new CPrecompileContext(GetContext());//создаем новый контекст, в котором будем компилировать тело функции
									procContext->SetModule(this);
									procContext->nReturn = RETURN_PROCEDURE;

									CPrecompileFunction* pFunction = new CPrecompileFunction(sMethodName, procContext);
									pFunction->sRealName = sMethodName;
									pFunction->sShortDescription = code.sShortDescription;

									pFunction->nStart = nNumberFunc;
									pFunction->bContext = true;
									pFunction->bExport = true;

									pFunction->m_valContext = pRefData;

									//проверка на типизированность
									GetContext()->cFunctions[StringUtils::MakeUpper(sMethodName)] = pFunction; nNumberFunc++;
								}
								else if (code.eType == eExportFunction) {
									wxString sMethodName = code.sName;
									if (cContext.FindFunction(sMethodName))
										continue;

									CPrecompileContext* procContext = new CPrecompileContext(GetContext());//создаем новый контекст, в котором будем компилировать тело функции
									procContext->SetModule(this);
									procContext->nReturn = RETURN_FUNCTION;

									CPrecompileFunction* pFunction = new CPrecompileFunction(sMethodName, procContext);
									pFunction->sRealName = sMethodName;
									pFunction->sShortDescription = code.sShortDescription;

									pFunction->nStart = nNumberFunc;
									pFunction->bContext = true;
									pFunction->bExport = true;

									pFunction->m_valContext = pRefData;

									//проверка на типизированность
									GetContext()->cFunctions[StringUtils::MakeUpper(sMethodName)] = pFunction; nNumberFunc++;
								}
							}
						}
					}
				}
			}
			compileModule = compileModule->GetParent();
		}
	}
}

bool CPrecompileModule::PrepareLexem()
{
	wxString s;
	m_aLexemList.clear();

	while (!IsEnd())
	{
		lexem_t bytecode;
		bytecode.m_nNumberLine = m_nCurLine;
		bytecode.m_nNumberString = m_nCurPos;//если в дальнейшем произойдет ошибка, то именно эту строку нужно выдать пользователю
		bytecode.m_sModuleName = m_sModuleName;

		if (IsWord())
		{
			wxString sOrig;
			s = GetWord(false, false, &sOrig);

			//undefined
			if (s.Lower() == wxT("undefined"))
			{
				bytecode.m_nType = CONSTANT;
				bytecode.m_vData.SetType(eValueTypes::TYPE_EMPTY);
			}
			//boolean
			else if (s.Lower() == wxT("true") || s.Lower() == wxT("false"))
			{
				bytecode.m_nType = CONSTANT;
				bytecode.m_vData.SetBoolean(s);
			}
			//null
			else if (s.Lower() == wxT("null"))
			{
				bytecode.m_nType = CONSTANT;
				bytecode.m_vData.SetType(eValueTypes::TYPE_NULL);
			}

			if (bytecode.m_nType != CONSTANT)
			{
				int n = IsKeyWord(s);

				bytecode.m_vData = sOrig;

				if (n >= 0)
				{
					bytecode.m_nType = KEYWORD;
					bytecode.m_nData = n;
				}
				else
				{
					bytecode.m_nType = IDENTIFIER;
				}
			}
		}
		else if (IsNumber() || IsString() || IsDate())
		{
			bytecode.m_nType = CONSTANT;

			if (IsNumber())
			{
				bytecode.m_vData.SetNumber(GetNumber());

				int n = m_aLexemList.size() - 1;

				if (n >= 0)
				{
					if (m_aLexemList[n].m_nType == DELIMITER && (m_aLexemList[n].m_nData == '-' || m_aLexemList[n].m_nData == '+'))
					{
						n--;
						if (n >= 0)
						{
							if (m_aLexemList[n].m_nType == DELIMITER && (m_aLexemList[n].m_nData == '[' || m_aLexemList[n].m_nData == '(' || m_aLexemList[n].m_nData == ',' || m_aLexemList[n].m_nData == '<' || m_aLexemList[n].m_nData == '>' || m_aLexemList[n].m_nData == '='))
							{
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
			else
			{
				if (IsString())
				{
					bytecode.m_vData.SetString(GetString());
				}
				else if (IsDate())
				{
					bytecode.m_vData.SetDate(GetDate());
				}
			}

			m_aLexemList.push_back(bytecode);
			continue;
		}
		else if (IsByte('~'))
		{
			s.clear();

			GetByte();//пропускаем разделитель и вспомог. символ метки (как лишние)
			continue;
		}
		else
		{
			s.clear();

			bytecode.m_nType = DELIMITER;
			bytecode.m_nData = GetByte();

			if (bytecode.m_nData <= 13)
			{
				continue;
			}
		}

		bytecode.m_sData = s;

		if (bytecode.m_nType == KEYWORD)
		{
			if (bytecode.m_nData == KEY_DEFINE)continue; //задание произвольного идентификатора
			else if (bytecode.m_nData == KEY_UNDEF) continue; //удаление идентификатора
			else if (bytecode.m_nData == KEY_IFDEF || bytecode.m_nData == KEY_IFNDEF) continue; //условное компилирование
			else if (bytecode.m_nData == KEY_ENDIFDEF) continue; //конец условного компилирования
			else if (bytecode.m_nData == KEY_ELSEDEF) continue; //"Иначе" условного компилирования
			else if (bytecode.m_nData == KEY_REGION) continue;
			else if (bytecode.m_nData == KEY_ENDREGION) continue;
		}

		m_aLexemList.push_back(bytecode);
	}

	lexem_t bytecode;
	bytecode.m_nType = ENDPROGRAM;
	bytecode.m_nData = 0;
	bytecode.m_nNumberString = m_nCurPos;
	m_aLexemList.push_back(bytecode);

	return true;
}

void CPrecompileModule::PatchLexem(unsigned int line, int offsetLine, unsigned int offsetString, unsigned int modFlags)
{
	unsigned int nLexPos = m_aLexemList.size() > 1 ? m_aLexemList.size() - 2 : 0;

	for (unsigned int i = 0; i <= m_aLexemList.size() - 1; i++)
	{
		if (m_aLexemList[i].m_nNumberLine >= line) { nLexPos = i; break; }
	}

	for (unsigned int i = nLexPos; i < m_aLexemList.size() - 1; i++)
	{
		if ((modFlags & wxSTC_MOD_BEFOREINSERT) != 0 && m_aLexemList[i].m_nNumberLine <= line)
		{
			if (m_aLexemList[i].m_nNumberLine != line && m_aLexemList[i + 1].m_nType == ENDPROGRAM)
			{
				m_nCurLine = m_aLexemList[i].m_nNumberLine;
				m_nCurPos = m_aLexemList[i].m_nNumberString;
			}

			m_aLexemList.erase(m_aLexemList.begin() + i); i--;
		}
		else if ((modFlags & wxSTC_MOD_BEFOREDELETE) != 0 && m_aLexemList[i].m_nNumberLine <= (line - offsetLine))
		{
			m_aLexemList.erase(m_aLexemList.begin() + i); i--;
		}
		else break;
	}

	while (!IsEnd())
	{
		if ((modFlags & wxSTC_MOD_BEFOREINSERT) != 0 && m_nCurLine > (line + offsetLine)) break;
		else if ((modFlags & wxSTC_MOD_BEFOREDELETE) != 0 && (m_nCurLine > line)) break;

		lexem_t bytecode;
		bytecode.m_nNumberLine = m_nCurLine;
		bytecode.m_nNumberString = m_nCurPos; //если в дальнейшем произойдет ошибка, то именно эту строку нужно выдать пользователю
		bytecode.m_sModuleName = m_sModuleName;

		wxString s;

		if (IsWord())
		{
			wxString sOrig;
			s = GetWord(false, false, &sOrig);

			//undefined
			if (s.Lower() == wxT("undefined"))
			{
				bytecode.m_nType = CONSTANT;
				bytecode.m_vData.SetType(eValueTypes::TYPE_EMPTY);
			}
			//boolean
			else if (s.Lower() == wxT("true") || s.Lower() == wxT("false"))
			{
				bytecode.m_nType = CONSTANT;
				bytecode.m_vData.SetBoolean(s);
			}
			//null
			else if (s.Lower() == wxT("null"))
			{
				bytecode.m_nType = CONSTANT;
				bytecode.m_vData.SetType(eValueTypes::TYPE_NULL);
			}

			if (bytecode.m_nType != CONSTANT)
			{
				int n = IsKeyWord(s);

				bytecode.m_vData = sOrig;

				if (n >= 0)
				{
					bytecode.m_nType = KEYWORD;
					bytecode.m_nData = n;
				}
				else
				{
					bytecode.m_nType = IDENTIFIER;
				}
			}
		}
		else if (IsNumber() || IsString() || IsDate())
		{
			bytecode.m_nType = CONSTANT;

			if (IsNumber())
			{
				bytecode.m_vData.SetNumber(GetNumber());

				int n = nLexPos;

				if (n >= 0)
				{
					if (m_aLexemList[n].m_nType == DELIMITER && (m_aLexemList[n].m_nData == '-' || m_aLexemList[n].m_nData == '+'))
					{
						n--;
						if (n >= 0)
						{
							if (m_aLexemList[n].m_nType == DELIMITER && (m_aLexemList[n].m_nData == '[' || m_aLexemList[n].m_nData == '(' || m_aLexemList[n].m_nData == ',' || m_aLexemList[n].m_nData == '<' || m_aLexemList[n].m_nData == '>' || m_aLexemList[n].m_nData == '='))
							{
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
			else
			{
				if (IsString())
				{
					bytecode.m_vData.SetString(GetString());
				}
				else if (IsDate())
				{
					bytecode.m_vData.SetDate(GetDate());
				}
			}

			m_aLexemList.insert(m_aLexemList.begin() + nLexPos, bytecode);
			nLexPos++;

			continue;
		}
		else if (IsByte('~'))
		{
			s.clear();

			GetByte();//пропускаем разделитель и вспомог. символ метки (как лишние)
			nLexPos++;

			continue;
		}
		else
		{
			s.clear();

			bytecode.m_nType = DELIMITER;
			bytecode.m_nData = GetByte();

			if (bytecode.m_nData <= 13)
			{
				nLexPos++;
				continue;
			}
		}

		bytecode.m_sData = s;

		if (bytecode.m_nType == KEYWORD)
		{
			if (
				bytecode.m_nData == KEY_DEFINE //задание произвольного идентификатора
				|| bytecode.m_nData == KEY_UNDEF //удаление идентификатора
				|| (bytecode.m_nData == KEY_IFDEF || bytecode.m_nData == KEY_IFNDEF)  //условное компилирование
				|| bytecode.m_nData == KEY_ENDIFDEF  //конец условного компилирования
				|| bytecode.m_nData == KEY_ELSEDEF  //"Иначе" условного компилирования
				|| bytecode.m_nData == KEY_REGION
				|| bytecode.m_nData == KEY_ENDREGION
				)
			{
				continue;
			}
		}

		m_aLexemList.insert(m_aLexemList.begin() + nLexPos, bytecode); nLexPos++;
	}

	for (unsigned int i = nLexPos; i < m_aLexemList.size() - 1; i++)
	{
		m_aLexemList[i].m_nNumberLine += offsetLine;

		if ((modFlags & wxSTC_MOD_BEFOREINSERT) != 0)
		{
			m_aLexemList[i].m_nNumberString += offsetString;
		}
		else
		{
			m_aLexemList[i].m_nNumberString -= offsetString;
		}
	}

	if ((modFlags & wxSTC_MOD_BEFOREINSERT) != 0)
	{
		m_aLexemList[m_aLexemList.size() - 1].m_nNumberString += offsetString;
	}
	else
	{
		m_aLexemList[m_aLexemList.size() - 1].m_nNumberString -= offsetString;
	}
}

bool CPrecompileModule::Compile()
{
	Clear();

	//Добавление глобальных констант
	IMetadata* metaData = m_moduleObject->GetMetadata();
	wxASSERT(metaData);
	IModuleManager* moduleManager = metaData->GetModuleManager();
	wxASSERT(moduleManager);

	for (auto variable : moduleManager->GetGlobalVariables()) {
		AddVariable(variable.first, variable.second);
	}

	PrepareModuleData();

	return CompileModule();
}

bool CPrecompileModule::CompileModule()
{
	m_pContext = GetContext();//контекст самого модуля

	lexem_t lex;

	while ((lex = PreviewGetLexem()).m_nType != ERRORTYPE)
	{
		if ((KEYWORD == lex.m_nType && KEY_VAR == lex.m_nData) || (IDENTIFIER == lex.m_nType && IsTypeVar(lex.m_sData)))
		{
			CompileDeclaration();//загружаем объявление переменных
		}
		else if (KEYWORD == lex.m_nType && (KEY_PROCEDURE == lex.m_nData || KEY_FUNCTION == lex.m_nData))
		{
			CompileFunction();//загружаем объявление функций
			//не забываем восстанавливать текущий контекст модуля (если это нужно)...
		}
		else
		{
			break;
		}
	}

	int nStartContext = m_nCurrentCompile >= 0 ? m_aLexemList[m_nCurrentCompile].m_nNumberString : 0;

	//загружаем исполняемое тело модуля
	m_pContext = GetContext();//контекст самого модуля

	CompileBlock();

	if (m_nCurrentCompile + 1 < m_aLexemList.size() - 1) return false;

	if (m_nCurrentPos >= nStartContext && m_nCurrentPos <= m_aLexemList[m_nCurrentCompile].m_nNumberString)
	{
		m_pCurrentContext = m_pContext;
	}

	return true;
}

bool CPrecompileModule::CompileFunction()
{
	//сейчас мы на уровне лексемы, где задано ключевое слово FUNCTION или PROCEDURE
	lexem_t lex;
	if (IsNextKeyWord(KEY_FUNCTION))
	{
		GETKeyWord(KEY_FUNCTION);

		m_pContext = new CPrecompileContext(GetContext());//создаем новый контекст, в котором будем компилировать тело функции
		m_pContext->SetModule(this);
		m_pContext->nReturn = RETURN_FUNCTION;
	}
	else if (IsNextKeyWord(KEY_PROCEDURE))
	{
		GETKeyWord(KEY_PROCEDURE);

		m_pContext = new CPrecompileContext(GetContext());//создаем новый контекст, в котором будем компилировать тело процедуры
		m_pContext->SetModule(this);
		m_pContext->nReturn = RETURN_PROCEDURE;
	}
	else
	{
		/*SetError(ERROR_FUNC_DEFINE);*/
		return false;
	}

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

	//получаем имя функции
	wxString csFuncName0 = GETIdentifier(true);
	wxString sFuncName = StringUtils::MakeUpper(csFuncName0);
	int nError = m_nCurrentCompile;

	CPrecompileFunction* pFunction = new CPrecompileFunction(sFuncName, m_pContext);

	pFunction->sRealName = csFuncName0;
	pFunction->sShortDescription = sShortDescription;
	pFunction->nNumberLine = m_nNumberLine;

	//компилируем список формальных параметров + регистрируем их как локальные
	GETDelimeter('(');
	while (m_nCurrentCompile + 1 < m_aLexemList.size()
		&& !IsNextDelimeter(')'))
	{
		while (m_nCurrentCompile + 1 < m_aLexemList.size())
		{
			wxString sType;
			//проверка на типизированность
			if (IsTypeVar())
			{
				sType = GetTypeVar();
			}

			SParamValue sVariable;

			if (IsNextKeyWord(KEY_VAL))
			{
				GETKeyWord(KEY_VAL);
			}

			wxString sRealName = GETIdentifier(true);
			sVariable.m_paramName = StringUtils::MakeUpper(sRealName);

			//регистрируем эту переменную как локальную
			if (m_pContext->FindVariable(sVariable.m_paramName)) return false;//было объявление + повторное объявление = ошибка

			if (IsNextDelimeter('[')) { //это массив
				GETDelimeter('[');
				GETDelimeter(']');
			}
			else if (IsNextDelimeter('=')) {
				GETDelimeter('=');
				GETConstant();
			}

			CValue valObject;

			if (!sType.IsEmpty()) {
				try {
					valObject = CValue::CreateObject(sType);
				}
				catch (...)
				{
				}
			}

			m_pContext->AddVariable(sRealName, sType, false, false, valObject);
			sVariable.m_paramType = sType;

			pFunction->aParamList.push_back(sVariable);

			if (IsNextDelimeter(')'))
				break;

			GETDelimeter(',');
		}
	}

	GETDelimeter(')');

	if (IsNextKeyWord(KEY_EXPORT)) {
		GETKeyWord(KEY_EXPORT);
		pFunction->bExport = true;
	}

	//проверка на типизированность
	GetContext()->cFunctions[sFuncName] = pFunction;

	int nStartContext = m_aLexemList[m_nCurrentCompile].m_nNumberString;

	GetContext()->sCurFuncName = sFuncName;
	CompileBlock();
	GetContext()->sCurFuncName = wxEmptyString;

	if (m_pContext->nReturn == RETURN_FUNCTION) GETKeyWord(KEY_ENDFUNCTION);
	else GETKeyWord(KEY_ENDPROCEDURE);

	if (m_nCurrentPos >= nStartContext && m_nCurrentPos <= m_aLexemList[m_nCurrentCompile].m_nNumberString) m_pCurrentContext = m_pContext;
	return true;
}

bool CPrecompileModule::CompileDeclaration()
{
	wxString sType;
	const lexem_t& lex = PreviewGetLexem();

	if (IDENTIFIER == lex.m_nType) sType = GetTypeVar(); //типизированное задание переменных
	else GETKeyWord(KEY_VAR);

	while (m_nCurrentCompile + 1 < m_aLexemList.size())
	{
		wxString sName = GETIdentifier(true);

		int nArrayCount = wxNOT_FOUND;
		if (IsNextDelimeter('['))//это объявление массива
		{
			nArrayCount = 0;
			GETDelimeter('[');
			if (!IsNextDelimeter(']')) {
				CValue vConst = GETConstant();
				if (vConst.GetType() != eValueTypes::TYPE_NUMBER || vConst.GetNumber() < 0)
					return false;
				nArrayCount = vConst.GetInteger();
			}
			GETDelimeter(']');
		}

		bool bExport = false;

		if (IsNextKeyWord(KEY_EXPORT)) {
			if (bExport) break;//было объявление Экспорт
			GETKeyWord(KEY_EXPORT);
			bExport = true;
		}

		//не было еще объявления переменной - добавляем
		m_pContext->AddVariable(sName, sType, bExport);

		if (IsNextDelimeter('='))//начальная инициализация - работает только внутри текста модулей (но не пере объявл. процедур и функций)
		{
			if (nArrayCount >= 0) GETDelimeter(',');//Error!
			GETDelimeter('=');
		}

		if (!IsNextDelimeter(','))
			break;

		GETDelimeter(',');
	}

	return true;
}

bool CPrecompileModule::CompileBlock()
{
	lexem_t lex;

	while ((lex = PreviewGetLexem()).m_nType != ERRORTYPE)
	{
		if (IDENTIFIER == lex.m_nType && IsTypeVar(lex.m_sData)) CompileDeclaration();

		if (KEYWORD == lex.m_nType)
		{
			switch (lex.m_nData)
			{
			case KEY_VAR://задание переменных и массивов
				CompileDeclaration();
				break;
			case KEY_NEW:
				CompileNewObject();
				break;
			case KEY_IF:
				CompileIf();
				break;
			case KEY_WHILE:
				CompileWhile();
				break;
			case KEY_FOREACH:
				CompileForeach();
				break;
			case KEY_FOR:
				CompileFor();
				break;
			case KEY_GOTO:
				CompileGoto();
				break;
			case KEY_RETURN:
			{
				GETKeyWord(KEY_RETURN);

				if (m_pContext->nReturn == RETURN_NONE)
					return false;

				if (m_pContext->nReturn == RETURN_FUNCTION)//возвращается какое-то значение
				{
					if (IsNextDelimeter(';')) return false;

					SParamValue returnValue = GetExpression();

					if (!cContext.sCurFuncName.IsEmpty())
					{
						CPrecompileFunction* m_precompile = static_cast<CPrecompileFunction*>(cContext.cFunctions[cContext.sCurFuncName]);
						m_precompile->RealRetValue = returnValue;
					}
				}
				break;
			}
			case KEY_TRY:
			{
				GETKeyWord(KEY_TRY);
				CompileBlock();

				GETKeyWord(KEY_EXCEPT);
				CompileBlock();
				GETKeyWord(KEY_ENDTRY);

				break;
			}

			case KEY_RAISE: GETKeyWord(KEY_RAISE); break;
			case KEY_CONTINUE: GETKeyWord(KEY_CONTINUE); break;
			case KEY_BREAK: GETKeyWord(KEY_BREAK); break;

			case KEY_FUNCTION:
			case KEY_PROCEDURE: GetLexem(); break;

			default: return true;//значит встретилась завершающая данный блок операторная скобка (например КОНЕЦЕСЛИ, КОНЕЦЦИКЛА, КОНЕЦФУНКЦИИ и т.п.)		
			}
		}
		else
		{
			lex = GetLexem();
			if (IDENTIFIER == lex.m_nType)
			{
				if (IsNextDelimeter(':'))//это встретилось задание метки
				{
					//записываем адрес перехода:
					GETDelimeter(':');
				}
				else//здесь обрабатываются вызовы функций, методов, присваиваение выражений
				{
					m_nCurrentCompile--;//шаг назад

					int nSet = 1;
					SParamValue sVariable = GetCurrentIdentifier(nSet);//получаем левую часть выражения (до знака '=')
					if (nSet)//если есть правая часть, т.е. знак '='
					{
						GETDelimeter('=');//это присваивание переменной какого-то выражения

						SParamValue sExpression = GetExpression();
						sVariable.m_paramType = sExpression.m_paramType;
						sVariable.m_paramObject = sExpression.m_paramObject;

						if (m_pContext->FindVariable(sVariable.m_paramName)) {
							m_pContext->cVariables[sVariable.m_paramName].m_valObject = sExpression.m_paramObject;
						}
						else
						{
							m_pContext->AddVariable(sVariable.m_paramName, sExpression.m_paramType, false, false, sExpression.m_paramObject);
						}
					}
				}
			}
			else if (DELIMITER == lex.m_nType && ';' == lex.m_nData) break;
			else if (ENDPROGRAM == lex.m_nType) break;
			else return false;
		}
	}//while

	return true;
}

bool CPrecompileModule::CompileNewObject()
{
	GETKeyWord(KEY_NEW);

	wxString objectName = GETIdentifier(true);
	int nNumber = GetConstString(objectName);

	std::vector <SParamValue> aParamList;

	if (IsNextDelimeter('('))//это вызов метода
	{
		GETDelimeter('(');

		while (m_nCurrentCompile + 1 < m_aLexemList.size()
			&& !IsNextDelimeter(')'))
		{
			if (IsNextDelimeter(','))
			{
				SParamValue data; //пропущенный параметр
				aParamList.push_back(data);
			}
			else
			{
				aParamList.emplace_back(GetExpression());

				if (IsNextDelimeter(')')) break;
			}
			GETDelimeter(',');
		}

		GETDelimeter(')');
	}

	return true;
}

bool CPrecompileModule::CompileGoto()
{
	GETKeyWord(KEY_GOTO);
	return true;
}

bool CPrecompileModule::CompileIf()
{
	GETKeyWord(KEY_IF);

	GetExpression();

	GETKeyWord(KEY_THEN);
	CompileBlock();

	while (IsNextKeyWord(KEY_ELSEIF))
	{
		//Записываем выход из всех проверок для предыдущего блока
		GETKeyWord(KEY_ELSEIF);

		GetExpression();

		GETKeyWord(KEY_THEN);
		CompileBlock();
	}

	if (IsNextKeyWord(KEY_ELSE))
	{
		//Записываем выход из всех проверок для предыдущего блока
		GETKeyWord(KEY_ELSE);
		CompileBlock();
	}

	GETKeyWord(KEY_ENDIF);
	return true;
}

bool CPrecompileModule::CompileWhile()
{
	GETKeyWord(KEY_WHILE);

	GetExpression();

	GETKeyWord(KEY_DO);
	CompileBlock();
	GETKeyWord(KEY_ENDDO);

	return true;
}

bool CPrecompileModule::CompileFor()
{
	GETKeyWord(KEY_FOR);

	int nStartPos = m_aLexemList[m_nCurrentCompile].m_nNumberString;

	wxString sRealName = GETIdentifier(true);
	//wxString sName = StringUtils::MakeUpper(sRealName);

	SParamValue sVariable = GetVariable(sRealName);

	if (IsNextDelimeter('='))
		GETDelimeter('=');

	GetExpression();

	GETKeyWord(KEY_TO);
	SParamValue VariableTo = GetExpression();

	GETKeyWord(KEY_DO);
	CompileBlock();
	GETKeyWord(KEY_ENDDO);

	if (!(nStartPos < m_nCurrentPos && m_aLexemList[m_nCurrentCompile].m_nNumberString > m_nCurrentPos))
		m_pContext->RemoveVariable(sRealName);

	return true;
}

bool CPrecompileModule::CompileForeach()
{
	GETKeyWord(KEY_FOREACH);

	int nStartPos = m_aLexemList[m_nCurrentCompile].m_nNumberString;

	wxString sRealName = GETIdentifier(true);
	wxString sName = StringUtils::MakeUpper(sRealName);

	SParamValue sVariable = GetVariable(sRealName);

	GETKeyWord(KEY_IN);

	SParamValue VariableTo = GetExpression();
	m_pContext->cVariables[sName].m_valObject = VariableTo.m_paramObject.GetItEmpty();

	GETKeyWord(KEY_DO);
	CompileBlock();
	GETKeyWord(KEY_ENDDO);

	if (!(nStartPos < m_nCurrentPos && m_aLexemList[m_nCurrentCompile].m_nNumberString > m_nCurrentPos))
		m_pContext->RemoveVariable(sRealName);

	return true;
}

//////////////////////////////////////////////////////////////////////
// Compiling
//////////////////////////////////////////////////////////////////////

/**
 * GetLexem
 * Назначение:
 * Получить следующую лексему из списка байт кода и увеличть счетчик текущей позиции на 1
 * Возвращаемое значение:
 * 0 или указатель на лексему
 */
lexem_t CPrecompileModule::GetLexem()
{
	lexem_t lex;
	if (m_nCurrentCompile + 1 < m_aLexemList.size()) {
		lex = m_aLexemList[++m_nCurrentCompile];
	}
	return lex;
}

//Получить следующую лексему из списка байт кода без увеличения счетчика текущей позиции
lexem_t CPrecompileModule::PreviewGetLexem()
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
lexem_t CPrecompileModule::GETLexem()
{
	const lexem_t& lex = GetLexem();
	if (lex.m_nType == ERRORTYPE) {}
	return lex;
}
/**
 * GETDELIMITER
 * Назначение:
 * Получить следующую лексему как заданный разделитель
 * Возвращаемое значение:
 * нет (в случае неудачи генерится исключение)
 */
void CPrecompileModule::GETDelimeter(char c)
{
	lexem_t lex = GETLexem();

	if (lex.m_nType == DELIMITER && lex.m_nData == c)
		sLastExpression += c;

	while (!(lex.m_nType == DELIMITER && lex.m_nData == c)) {
		if (m_nCurrentCompile + 1 >= m_aLexemList.size()) break;
		lex = GETLexem();
	}
}
/**
 * IsNextDELIMITER
 * Назначение:
 * Проверить является ли следующая лексема байт-кода заданным разделителем
 * Возвращаемое значение:
 * true,false
 */
bool CPrecompileModule::IsNextDelimeter(char c)
{
	if (m_nCurrentCompile + 1 < m_aLexemList.size()) {
		lexem_t lex = m_aLexemList[m_nCurrentCompile + 1];
		if (lex.m_nType == DELIMITER && lex.m_nData == c)
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
bool CPrecompileModule::IsNextKeyWord(int nKey)
{
	if (m_nCurrentCompile + 1 < m_aLexemList.size()) {
		const lexem_t& lex = m_aLexemList[m_nCurrentCompile + 1];
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
void CPrecompileModule::GETKeyWord(int nKey)
{
	lexem_t lex = GETLexem();
	while (!(lex.m_nType == KEYWORD && lex.m_nData == nKey)) {
		if (m_nCurrentCompile + 1 >= m_aLexemList.size())
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
wxString CPrecompileModule::GETIdentifier(bool realName)
{
	const lexem_t& lex = GETLexem();
	if (lex.m_nType != IDENTIFIER) {
		if (realName && lex.m_nType == KEYWORD)
			return lex.m_sData;
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
CValue CPrecompileModule::GETConstant()
{
	lexem_t lex;
	int iNumRequire = 0;
	if (IsNextDelimeter('-') || IsNextDelimeter('+')) {
		iNumRequire = 1;
		if (IsNextDelimeter('-'))
			iNumRequire = wxNOT_FOUND;
		lex = GETLexem();
	}

	lex = GETLexem();

	if (iNumRequire) {
		//проверка на то чтобы константа имела числовой тип	
		if (lex.m_vData.GetType() != eValueTypes::TYPE_NUMBER) {}
		//меняем знак при минусе
		if (iNumRequire == wxNOT_FOUND)
			lex.m_vData.m_fData = -lex.m_vData.m_fData;
	}
	return lex.m_vData;
}

//получение номера константой строки (для определения номера метода)
int CPrecompileModule::GetConstString(const wxString& sMethod)
{
	if (!m_aHashConstList[sMethod])
	{
		m_aHashConstList[sMethod] = m_aHashConstList.size();
	}
	return ((int)m_aHashConstList[sMethod]) - 1;
}

int CPrecompileModule::IsTypeVar(const wxString& sType)
{
	if (!sType.IsEmpty()) {
		if (CValue::IsRegisterObject(sType, eObjectType::eObjectType_simple))
			return true;
	}
	else {
		const lexem_t& lex = PreviewGetLexem();
		if (CValue::IsRegisterObject(lex.m_sData, eObjectType::eObjectType_simple))
			return true;
	}

	return false;
}

wxString CPrecompileModule::GetTypeVar(const wxString& sType)
{
	if (!sType.IsEmpty()) {
		if (CValue::IsRegisterObject(sType, eObjectType::eObjectType_simple))
			return sType.Upper();
	}
	else {
		const lexem_t& lex = GETLexem();
		if (CValue::IsRegisterObject(lex.m_sData, eObjectType::eObjectType_simple))
			return lex.m_sData.Upper();
	}

	return wxEmptyString;
}

#define SetOper(x) nOper=x;

/**
 * Компиляция произвольного выражения (служебные вызовы из самой функции)
 */
SParamValue CPrecompileModule::GetExpression(int nPriority)
{
	SParamValue sVariable;
	lexem_t lex = GETLexem();

	//Сначала обрабатываем Левые операторы
	if ((lex.m_nType == KEYWORD && lex.m_nData == KEY_NOT) ||
		(lex.m_nType == DELIMITER && lex.m_nData == '!')) {
		sVariable = GetVariable();
		SParamValue sVariable2 = GetExpression(s_aPriority['!']);
		sVariable.m_paramType = wxT("NUMBER");
	}
	else if ((lex.m_nType == KEYWORD && lex.m_nData == KEY_NEW)) {

		const wxString& objectName = GETIdentifier();
		std::vector <SParamValue> aParamList;


		if (IsNextDelimeter('(')) { //это вызов метода	
			GETDelimeter('(');
			while (m_nCurrentCompile + 1 < m_aLexemList.size()
				&& !IsNextDelimeter(')')) {
				if (IsNextDelimeter(',')) {
					SParamValue data;
					//data.nArray = DEF_VAR_SKIP;//пропущенный параметр
					//data.nIndex = DEF_VAR_SKIP;
					aParamList.push_back(data);
				}
				else {
					aParamList.emplace_back(GetExpression());
					if (IsNextDelimeter(')')) break;
				}
				GETDelimeter(',');
			}
			GETDelimeter(')');
		}

		CValue** pRefLocVars = aParamList.size() ? new CValue * [aParamList.size()] : NULL;
		for (unsigned int i = 0; i < aParamList.size(); i++) {
			pRefLocVars[i] = &aParamList[i].m_paramObject;
		}

		try {
			sVariable.m_paramObject = CValue::CreateObject(objectName,
				pRefLocVars, aParamList.size()
			);
		}
		catch (...) {
		}

		if (pRefLocVars != NULL)
			delete[]pRefLocVars;

		return sVariable;
	}
	else if (lex.m_nType == DELIMITER && lex.m_nData == '(')
	{
		sVariable = GetExpression();
		GETDelimeter(')');
	}
	else if (lex.m_nType == DELIMITER && lex.m_nData == '?')
	{
		sVariable = GetVariable();
		//byteRaw_t code;
		//AddLineInfo(code);
		//code.nOper = OPER_ITER;
		/*code.Param1 = sVariable;*/
		GETDelimeter('(');
		/*code.Param2 =*/ GetExpression();
		GETDelimeter(',');
		/*code.Param3 *=*/ GetExpression();
		GETDelimeter(',');
		/*code.Param4 = */GetExpression();
		GETDelimeter(')');
		//cByteCode.CodeList.push_back(code);
	}
	else if (lex.m_nType == IDENTIFIER)
	{
		m_nCurrentCompile--;//шаг назад
		int nSet = 0;
		sVariable = GetCurrentIdentifier(nSet);
	}
	else if (lex.m_nType == CONSTANT)
	{
		sVariable = FindConst(lex.m_vData);
	}
	else if ((lex.m_nType == DELIMITER && lex.m_nData == '+') || (lex.m_nType == DELIMITER && lex.m_nData == '-'))
	{
		//проверяем допустимость такого задания
		int nCurPriority = s_aPriority[lex.m_nData];

		if (nPriority >= nCurPriority)
			return sVariable; //сравниваем приоритеты левой (предыдущей операции) и текущей выполняемой операции

		//Это задание пользователем знака выражения
		if (lex.m_nData == '+')//ничего не делаем (игнорируем)
		{
			sVariable = GetExpression(nPriority);
			sVariable.m_paramType = wxT("NUMBER");
			return sVariable;
		}
		else
		{
			sVariable = GetExpression(100);//сверх высокий приоритет!
			sVariable = GetVariable();
			sVariable.m_paramType = wxT("NUMBER");
		}
	}

	//Теперь обрабатываем Правые операторы
	//итак в sVariable имеем первый индекс переменной выражения

MOperation:

	lex = PreviewGetLexem();

	if (lex.m_nType == DELIMITER && lex.m_nData == ')') return sVariable;

	//смотрим есть ли далее операторы выполнения действий над данной переменной
	if ((lex.m_nType == DELIMITER && lex.m_nData != ';') || (lex.m_nType == KEYWORD && lex.m_nData == KEY_AND) || (lex.m_nType == KEYWORD && lex.m_nData == KEY_OR))
	{
		if (lex.m_nData >= 0 && lex.m_nData <= 255)
		{
			int nCurPriority = s_aPriority[lex.m_nData]; int nOper = 0;

			if (nPriority < nCurPriority)//сравниваем приоритеты левой (предыдущей операции) и текущей выполняемой операции
			{
				lex = GetLexem();

				if (lex.m_nData == '*')
				{
					SetOper(OPER_MULT);
				}
				else if (lex.m_nData == '/')
				{
					SetOper(OPER_DIV);
				}
				else if (lex.m_nData == '+')
				{
					SetOper(OPER_ADD);
				}
				else if (lex.m_nData == '-')
				{
					SetOper(OPER_SUB);
				}
				else if (lex.m_nData == '%')
				{
					SetOper(OPER_MOD);
				}
				else if (lex.m_nData == KEY_AND)
				{
					SetOper(OPER_AND);
				}
				else if (lex.m_nData == KEY_OR)
				{
					SetOper(OPER_OR);
				}
				else if (lex.m_nData == '>')
				{
					SetOper(OPER_GT);

					if (IsNextDelimeter('='))
					{
						GETDelimeter('=');
						SetOper(OPER_GE);
					}
				}
				else if (lex.m_nData == '<')
				{
					SetOper(OPER_LS);
					if (IsNextDelimeter('='))
					{
						GETDelimeter('=');
						SetOper(OPER_LE);
					}
					else if (IsNextDelimeter('>'))
					{
						GETDelimeter('>');
						SetOper(OPER_NE);
					}

				}
				else if (lex.m_nData == '=')
				{
					SetOper(OPER_EQ);
				}
				else return sVariable;

				SParamValue sVariable1 = GetVariable();
				SParamValue sVariable2 = sVariable;
				SParamValue sVariable3 = GetExpression(nCurPriority);

				//доп. проверка на запрещенные операции
				if (sVariable2.m_paramType == wxT("STRING")) {
					if (OPER_DIV == nOper ||
						OPER_MOD == nOper ||
						OPER_MULT == nOper ||
						OPER_AND == nOper ||
						OPER_OR == nOper)
						return sVariable;
				}

				sVariable1.m_paramType = sVariable2.m_paramType;

				if (nOper >= OPER_GT && nOper <= OPER_NE) {
					sVariable1.m_paramType = wxT("NUMBER");
				}

				sVariable = sVariable1;
				goto MOperation;
			}
		}
	}
	return sVariable;
}

/*
 * GetCurrentIdentifier
 * Назначение:
 * Компилирование идентификатора (определение его типа как переменной,атрибута или функции,метода)
 * nIsSet - на входе:  1 - признак того что возможно ожидается присваивание выражения (если встретится знак '=')
 * Возвращаемое значение:
 * nIsSet - на выходе: 1 - признак того что точно ожидается присваивание выражения (т.е. должен встретиться знак '=')
 * номер идекса переменной, где лежит значение идентификатора
*/
SParamValue CPrecompileModule::GetCurrentIdentifier(int& nIsSet)
{
	int nPrevSet = nIsSet;

	SParamValue sVariable = GetVariable();

	wxString sRealName = GETIdentifier(true);
	wxString sName = StringUtils::MakeUpper(sRealName);

	int nStartPos = m_aLexemList[m_nCurrentCompile].m_nNumberString;

	if (!m_bCalcValue && (nStartPos + sRealName.length() == m_nCurrentPos ||
		nStartPos + sRealName.length() == m_nCurrentPos - 1)) {
		unsigned int endContext = 0;
		for (unsigned int i = m_nCurrentCompile; i < m_aLexemList.size(); i++) {
			if (m_aLexemList[i].m_nType == KEYWORD && (m_aLexemList[i].m_nData == KEY_ENDPROCEDURE || m_aLexemList[i].m_nData == KEY_ENDFUNCTION))
				endContext = i;
			if (m_aLexemList[i].m_nType == ENDPROGRAM)
				endContext = i;
		}
		nIsSet = 0; m_nCurrentCompile = endContext; return sVariable;
	}

	sLastExpression = sRealName;

	if (IsNextDelimeter('('))//это вызов функции
	{
		CValue valContext;
		if (cContext.FindFunction(sRealName, valContext, true))
		{
			std::vector <SParamValue> aParamList;
			GETDelimeter('(');
			while (m_nCurrentCompile + 1 < m_aLexemList.size()
				&& !IsNextDelimeter(')'))
			{
				if (IsNextDelimeter(','))
				{
					SParamValue data;
					aParamList.push_back(data);
				}
				else
				{
					aParamList.emplace_back(GetExpression());
					if (IsNextDelimeter(')')) break;
				}
				GETDelimeter(',');
			}

			GETDelimeter(')');

			const long lMethodNum = valContext.FindMethod(sName);
			if (lMethodNum != wxNOT_FOUND && valContext.HasRetVal(lMethodNum)) {
				CValue** pRefLocVars = new CValue * [aParamList.size() ? aParamList.size() : 1];
				for (unsigned int i = 0; i < aParamList.size(); i++) {
					pRefLocVars[i] = &aParamList[i].m_paramObject;
				}
				if (aParamList.size() == 0) {
					CValue cValue = eValueTypes::TYPE_EMPTY;
					pRefLocVars[0] = &cValue;
				}
				try {
					valContext.CallAsFunc(lMethodNum, sVariable.m_paramObject, pRefLocVars, aParamList.size());
				}
				catch (...) {
				}
				SetVariable(sVariable.m_paramName, sVariable.m_paramObject);
				delete[]pRefLocVars;
			}
		}
		else
		{
			sVariable = GetCallFunction(sName);
		}

		if (IsTypeVar(sName)) { //это приведение типов
			sVariable.m_paramObject = GetTypeVar(sName);
		}

		nIsSet = 0;
	}
	else//это вызов переменной
	{
		sLastParentKeyword = sRealName;

		bool bCheckError = !nPrevSet;

		if (IsNextDelimeter('.'))//эта переменная содержит вызов метода
			bCheckError = true;

		CValue valContext;
		if (cContext.FindVariable(sRealName, valContext, true)) {
			nIsSet = 0;
			if (IsNextDelimeter('=') && nPrevSet == 1) {
				GETDelimeter('=');
				SParamValue sParam = GetExpression();
				sVariable.m_paramObject = sParam.m_paramObject;
				return sVariable;
			}
			else {
				const long lPropNum = valContext.FindProp(sName);
				if (lPropNum != wxNOT_FOUND) {
					try {
						valContext.GetPropVal(lPropNum, sVariable.m_paramObject);
					}
					catch (...) {
					}
					SetVariable(sVariable.m_paramName, sVariable.m_paramObject);
				}
			}
		}
		else {
			nIsSet = 1;
			sVariable = GetVariable(sRealName, bCheckError);
		}
	}

MLabel:

	if (IsNextDelimeter('['))//это массив
	{
		GETDelimeter('[');
		SParamValue sKey = GetExpression();
		GETDelimeter(']');

		//определяем тип вызова (т.е. это установка значения массива или получение)
		//Пример:
		//Мас[10]=12; - Set
		//А=Мас[10]; - Get
		//Мас[10][2]=12; - Get,Set

		nIsSet = 0;

		if (IsNextDelimeter('[')) {}//проверки типа переменной массива (поддержка многомерных массивов)

		if (IsNextDelimeter('=') && nPrevSet == 1)
		{
			GETDelimeter('=');

			SParamValue sData = GetExpression();
			return sVariable;
		}
		else sVariable = GetVariable();

		goto MLabel;
	}

	if (IsNextDelimeter('.'))//это вызов метода или атрибута агрегатного объекта
	{
		wxString sTempExpression = sLastExpression;

		GETDelimeter('.');

		wxString sRealMethod = GETIdentifier(true);
		wxString sMethod = StringUtils::MakeUpper(sRealMethod);

		if (m_aLexemList[m_nCurrentCompile].m_nNumberString > m_nCurrentPos
			|| m_aLexemList[m_nCurrentCompile].m_nType == KEYWORD) {
			sRealMethod = sMethod = wxEmptyString;
		}

		sLastExpression += sRealMethod;

		if (m_aLexemList[m_nCurrentCompile].m_nNumberString > (m_nCurrentPos - sRealMethod.length() - 1))
		{
			sLastExpression = sTempExpression; nLastPosition = m_nCurrentCompile; sLastKeyword = sRealMethod;
			m_valObject = sVariable.m_paramObject; m_nCurrentCompile = m_aLexemList.size() - 1; nIsSet = 0;
			return sVariable;
		}
		else if (m_aLexemList[m_nCurrentCompile].m_nType == ENDPROGRAM)
		{
			sLastExpression = sTempExpression; nLastPosition = m_nCurrentCompile; sLastKeyword = sRealMethod;
			m_valObject = sVariable.m_paramObject; m_nCurrentCompile = m_aLexemList.size() - 1; nIsSet = 0;
			return sVariable;
		}

		if (IsNextDelimeter('('))//это вызов метода
		{
			std::vector <SParamValue> aParamList;
			GETDelimeter('(');
			while (m_nCurrentCompile + 1 < m_aLexemList.size()
				&& !IsNextDelimeter(')'))
			{
				if (IsNextDelimeter(','))
				{
					SParamValue data;
					//data.nArray = DEF_VAR_SKIP;//пропущенный параметр
					//data.nIndex = DEF_VAR_SKIP;
					aParamList.push_back(data);
				}
				else
				{
					aParamList.emplace_back(GetExpression());
					if (IsNextDelimeter(')')) break;
				}
				GETDelimeter(',');
			}

			GETDelimeter(')');

			CValue parentValue = sVariable.m_paramObject;
			sVariable = GetVariable();

			const long lMethodNum = parentValue.FindMethod(sMethod);
			if (lMethodNum != wxNOT_FOUND && parentValue.HasRetVal(lMethodNum)) {
				CValue** paParams = new CValue * [aParamList.size() ? aParamList.size() : 1];
				if (aParamList.size() == 0) {
					CValue cValue = eValueTypes::TYPE_EMPTY;
					paParams[0] = &cValue;
				}
				for (unsigned int i = 0; i < aParamList.size(); i++) {
					paParams[i] = &aParamList[i].m_paramObject;
				}

				try {
					parentValue.CallAsFunc(lMethodNum, sVariable.m_paramObject, paParams, aParamList.size());
				}
				catch (...) {
				}
				SetVariable(sVariable.m_paramName, sVariable.m_paramObject);
				delete[]paParams;
			}

			nIsSet = 0;
		}
		else//иначе - вызов атрибута
		{
			//определяем тип вызова (т.е. это установка атрибута или получение)
			//Пример:
			//А=Спр.Товар; - Get
			//Спр.Товар=0; - Set
			//Спр.Товар.Код=0;  - Get,Set

			nIsSet = 0;

			if (IsNextDelimeter('=') && nPrevSet == 1) {
				GETDelimeter('=');
				CValue parentValue = sVariable.m_paramObject;
				SParamValue sParam = GetExpression();
				const long lPropNum = parentValue.FindProp(sRealMethod);
				if (lPropNum != wxNOT_FOUND) {
					try {
						parentValue.SetPropVal(lPropNum, sParam.m_paramObject);
					}
					catch (...) {
					}
				}
				return sVariable;
			}
			else {
				CValue parentValue = sVariable.m_paramObject;
				sVariable = GetVariable();
				const long lPropNum = parentValue.FindProp(sMethod);
				if (lPropNum != wxNOT_FOUND) {
					try {
						parentValue.GetPropVal(lPropNum, sVariable.m_paramObject);
					}
					catch (...)
					{
					}
				}
				SetVariable(sVariable.m_paramName, sVariable.m_paramObject);
			}
		}
		goto MLabel;
	}

	return sVariable;
}//GetCurrentIdentifier

/**
 * обработка вызова функции или процедуры
 */
SParamValue CPrecompileModule::GetCallFunction(const wxString& sName)
{
	std::vector<SParamValue> aParamList;

	GETDelimeter('(');

	while (m_nCurrentCompile + 1 < m_aLexemList.size()
		&& !IsNextDelimeter(')'))
	{
		if (IsNextDelimeter(','))
		{
			SParamValue data;
			//data.nArray = DEF_VAR_SKIP;//пропущенный параметр
			//data.nIndex = DEF_VAR_SKIP;
			aParamList.push_back(data);
		}
		else
		{
			aParamList.emplace_back(GetExpression());

			if (IsNextDelimeter(')')) break;
		}
		GETDelimeter(',');
	}
	GETDelimeter(')');

	CValue retValue;

	if (cContext.cFunctions.find(sName) != cContext.cFunctions.end())
	{
		CPrecompileFunction* pDefFunction = cContext.cFunctions[sName];
		pDefFunction->aParamList = aParamList;
		retValue = pDefFunction->RealRetValue.m_paramObject;
	}

	SParamValue sVariable = GetVariable();
	sVariable.m_paramObject = retValue;
	return sVariable;
}

/**
 * AddVariable
 * Назначение:
 * Добавить имя и адрес внешней перменной в специальный массив для дальнейшего использования
 */
void CPrecompileModule::AddVariable(const wxString& varName, const CValue& varVal)
{
	if (varName.IsEmpty())
		return;

	//учитываем внешние переменные при компиляции
	cContext.GetVariable(varName, false, false, varVal);
}

/**
 * Функция возвращает номер переменной по строковому имени
 */
SParamValue CPrecompileModule::GetVariable(const wxString& sName, bool bCheckError)
{
	return m_pContext->GetVariable(sName, true, bCheckError);
}

/**
 * Cоздаем новый идентификатор переменной
 */
SParamValue CPrecompileModule::GetVariable()
{
	const wxString& varName = wxString::Format("@%d", m_pContext->nTempVar); //@ - для гарантии уникальности имени
	SParamValue sVariable = m_pContext->GetVariable(varName, false);//временную переменную ищем только в локальном контексте
	m_pContext->nTempVar++;
	return sVariable;
}

void CPrecompileModule::SetVariable(const wxString& varName, const CValue& varVal)
{
	m_pContext->SetVariable(varName, varVal);
}

/**
 * Получает номер константы из уникального списка значений
 * (если такого значения в списке нет, то оно создается)
 */
SParamValue CPrecompileModule::FindConst(CValue& vData)
{
	SParamValue Const;
	wxString sType = vData.GetTypeString();
	Const.m_paramType = GetTypeVar(sType);
	Const.m_paramObject = vData;
	return Const;
}


#pragma warning(pop)