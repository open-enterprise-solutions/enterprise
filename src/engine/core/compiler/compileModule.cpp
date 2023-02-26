////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, 2С-team
//	Description : compile module 
////////////////////////////////////////////////////////////////////////////

#include "compileModule.h"
#include "definition.h"
#include "systemObjects.h"
#include "core/metadata/metaObjects/metaModuleObject.h"
#include "utils/stringUtils.h"

#pragma warning(push)
#pragma warning(disable : 4018)

//////////////////////////////////////////////////////////////////////
//                           Constants
//////////////////////////////////////////////////////////////////////

std::map<wxString, void*> aHelpDescription;//описание ключевых слов и системных функций
std::map<wxString, void*> aHashKeywordList;

//Массив приоритетов математических операций
static std::array<int, 256> s_aPriority = { 0 };

////////////////////////////////////////////////////////////////////////
// byteCode_t byteCode_t byteCode_t byteCode_t						  //
////////////////////////////////////////////////////////////////////////

long byteCode_t::FindMethod(const wxString& methodName) const
{
	auto foundedFunction = std::find_if(m_aFuncList.begin(), m_aFuncList.end(),
		[methodName](const std::pair<const wxString, byteMethod_t>& pair) {
			return StringUtils::CompareString(methodName, pair.first);
		}
	);
	if (foundedFunction != m_aFuncList.end())
		return (long)foundedFunction->second - 1;
	return wxNOT_FOUND;
}

long byteCode_t::FindExportMethod(const wxString& methodName) const
{
	auto foundedFunction = std::find_if(m_aExportFuncList.begin(), m_aExportFuncList.end(),
		[methodName](const std::pair<const wxString, byteMethod_t>& pair) {
			return StringUtils::CompareString(methodName, pair.first);
		}
	);
	if (foundedFunction != m_aExportFuncList.end())
		return (long)foundedFunction->second - 1;
	return wxNOT_FOUND;
}

long byteCode_t::FindVariable(const wxString& varName) const
{
	auto foundedFunction = std::find_if(m_aVarList.begin(), m_aVarList.end(),
		[varName](const std::pair<const wxString, long>& pair) {
			return StringUtils::CompareString(varName, pair.first);
		}
	);
	if (foundedFunction != m_aVarList.end())
		return (long)foundedFunction->second - 1;
	return wxNOT_FOUND;
}

long byteCode_t::FindExportVariable(const wxString& varName) const
{
	auto foundedFunction = std::find_if(m_aExportVarList.begin(), m_aExportVarList.end(),
		[varName](const std::pair<const wxString, long>& pair) {
			return StringUtils::CompareString(varName, pair.first);
		}
	);
	if (foundedFunction != m_aExportVarList.end())
		return (long)foundedFunction->second - 1;
	return wxNOT_FOUND;
}

////////////////////////////////////////////////////////////////////////
// compileContext_t compileContext_t compileContext_t compileContext_t//
////////////////////////////////////////////////////////////////////////

/**
 * Добавляет новую переменную в список
 * Возвращает добавленную переменную в виде param_t
 */
param_t compileContext_t::AddVariable(const wxString& varName,
	const wxString& typeVar, bool exportVar, bool contextVar, bool tempVar)
{
	variable_t* foundedVariable = NULL;
	if (FindVariable(varName, foundedVariable))//было объявление + повторное объявление = ошибка
		m_compileModule->SetError(ERROR_IDENTIFIER_DUPLICATE, varName);

	unsigned int nCountVar = m_cVariables.size();

	variable_t cCurrentVariable;
	cCurrentVariable.m_sName = StringUtils::MakeUpper(varName);
	cCurrentVariable.m_sRealName = varName;
	cCurrentVariable.m_bExport = exportVar;
	cCurrentVariable.m_bContext = contextVar;
	cCurrentVariable.m_bTempVar = tempVar;
	cCurrentVariable.m_sType = typeVar;
	cCurrentVariable.m_nNumber = nCountVar;

	m_cVariables.insert_or_assign(
		StringUtils::MakeUpper(varName), cCurrentVariable
	);

	param_t cRet;
	cRet.m_nArray = 0;
	cRet.m_sType = typeVar;
	cRet.m_nIndex = nCountVar;
	return cRet;
}

/**
 * Функция возвращает номер переменной по строковому имени
 * Поиск определения переменной, начиная с текущего контекста до всех родительских
 * Если требуемой переменной нет, то создается новое определение переменной
 */
param_t compileContext_t::GetVariable(const wxString& varName, bool bFindInParent, bool bCheckError, bool contextVar, bool tempVar)
{
	int nCanUseLocalInParent = m_nFindLocalInParent;
	variable_t* currentVariable = NULL;
	if (!FindVariable(varName, currentVariable)) {
		//ищем в родительских контекстах(модулях)
		if (bFindInParent) {
			int nParentNumber = 0;
			compileContext_t* pCurContext = m_parentContext;
			while (pCurContext) {
				nParentNumber++;
				if (nParentNumber > MAX_OBJECTS_LEVEL) {
					CSystemObjects::Message(pCurContext->m_compileModule->GetModuleName());
					if (nParentNumber > 2 * MAX_OBJECTS_LEVEL) {
						CTranslateError::Error("Recursive call of modules!");
					}
				}
				if (pCurContext->FindVariable(varName, currentVariable)) { //нашли
					//смотрим это экспортная переменная или нет (если m_nFindLocalInParent=true, то можно взять локальные переменные родителя)	
					if (nCanUseLocalInParent > 0 ||
						currentVariable->m_bExport) {
						param_t variable;
						//определяем номер переменной
						variable.m_nArray = nParentNumber;
						variable.m_nIndex = currentVariable->m_nNumber;
						variable.m_sType = currentVariable->m_sType;
						return variable;
					}
				}
				nCanUseLocalInParent--;
				pCurContext = pCurContext->m_parentContext;
			}
		}

		if (bCheckError)
			m_compileModule->SetError(ERROR_VAR_NOT_FOUND, varName); //выводим сообщение об ошибке

		//не было еще объявления переменной - добавляем
		return AddVariable(varName, wxEmptyString, contextVar, contextVar, tempVar);
	}

	wxASSERT(currentVariable);

	param_t variable;
	//определяем номер и тип переменной
	variable.m_nArray = 0;
	variable.m_nIndex = currentVariable->m_nNumber;
	variable.m_sType = currentVariable->m_sType;
	return variable;
}

/**
 * Поиск переменной в хэш массиве
 * Возвращает 1 - если переменная найдена
 */
bool compileContext_t::FindVariable(const wxString& varName, variable_t*& foundedVar, bool contextVar)
{
	auto itFounded = std::find_if(m_cVariables.begin(), m_cVariables.end(),
		[varName](std::pair < const wxString, variable_t>& pair) {
			return StringUtils::CompareString(varName, pair.first); }
	);

	if (contextVar) {
		if (itFounded != m_cVariables.end()) {
			foundedVar = &itFounded->second;
			return itFounded->second.m_bContext;
		}
		if (m_parentContext && m_parentContext->FindVariable(varName, foundedVar, contextVar))
			return true;
		foundedVar = NULL;
		return false;
	}
	else if (itFounded != m_cVariables.end()) {
		foundedVar = &itFounded->second;
		return true;
	}
	foundedVar = NULL;
	return false;
}

/**
 * Поиск переменной в хэш массиве
 * Возвращает 1 - если переменная найдена
 */
bool compileContext_t::FindFunction(const wxString& funcName, function_t*& foundedFunc, bool contextVar) {
	auto itFounded = std::find_if(m_cFunctions.begin(), m_cFunctions.end(),
		[funcName](std::pair < const wxString, function_t*>& pair) {
			return StringUtils::CompareString(funcName, pair.first); }
	);
	if (contextVar) {
		if (itFounded != m_cFunctions.end() && itFounded->second) {
			foundedFunc = itFounded->second;
			return itFounded->second->m_bContext;
		}
		if (m_parentContext && m_parentContext->FindFunction(funcName, foundedFunc, contextVar))
			return true;
		foundedFunc = NULL;
		return false;
	}
	else if (itFounded != m_cFunctions.end() && itFounded->second) {
		foundedFunc = itFounded->second;
		return true;
	}
	foundedFunc = NULL;
	return false;
}

/**
 * Связываение операторов GOTO с метками
 */
void compileContext_t::DoLabels()
{
	wxASSERT(m_compileModule != NULL);
	for (unsigned int i = 0; i < m_cLabels.size(); i++) {
		wxString name = m_cLabels[i].m_sName;
		int m_nLine = m_cLabels[i].m_nLine;
		//ищем такую метку в списке объявленных меток
		unsigned int hLine = m_cLabelsDef[name];
		if (!hLine) {
			m_compileModule->m_nCurrentCompile = m_cLabels[i].m_nError;
			m_compileModule->SetError(ERROR_LABEL_DEFINE, name);//произошло дублированное определения меток
		}
		//записываем адрес перехода:
		m_compileModule->m_cByteCode.m_aCodeList[m_nLine].m_param1.m_nIndex = hLine + 1;
	}
};

compileContext_t::~compileContext_t()
{
	for (auto it = m_cFunctions.begin(); it != m_cFunctions.end(); it++) {
		function_t* pFunction = static_cast<function_t*>(it->second);
		if (pFunction)
			delete pFunction;
	}

	m_cFunctions.clear();
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction CCompileModule
//////////////////////////////////////////////////////////////////////

CCompileModule::CCompileModule() :
	CTranslateModule(),
	m_pContext(GetContext()),
	m_moduleObject(NULL), m_parent(NULL),
	m_bExpressionOnly(false), m_bNeedRecompile(false),
	m_bCommonModule(false)
{
	InitializeCompileModule();

	//у родительских контекстов локальные переменные не ищем!
	m_cContext.m_nFindLocalInParent = 0;
}

CCompileModule::CCompileModule(CMetaModuleObject* moduleObject, bool commonModule) :
	CTranslateModule(moduleObject->GetFullName(), moduleObject->GetDocPath()),
	m_pContext(GetContext()),
	m_moduleObject(moduleObject), m_parent(NULL),
	m_bExpressionOnly(false), m_bNeedRecompile(false),
	m_bCommonModule(commonModule)
{
	InitializeCompileModule();

	m_cByteCode.m_sModuleName = m_moduleObject->GetFullName();

	m_sModuleName = m_moduleObject->GetFullName();
	m_sDocPath = m_moduleObject->GetDocPath();
	m_sFileName = m_moduleObject->GetFileName();

	Load(m_moduleObject->GetModuleText());

	//у родительских контекстов локальные переменные не ищем!
	m_cContext.m_nFindLocalInParent = 0;
}

CCompileModule::~CCompileModule()
{
	Reset();

	m_aExternValues.clear();
	m_aContextValues.clear();
}

void CCompileModule::InitializeCompileModule()
{
	if (s_aPriority[s_aPriority.size() - 1])
		return;

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

void CCompileModule::Reset()
{
	m_pContext = NULL;

	m_cContext.m_nDoNumber = 0;
	m_cContext.m_nReturn = 0;
	m_cContext.m_nTempVar = 0;
	m_cContext.m_nFindLocalInParent = 1;

	m_cContext.aContinueList.clear();
	m_cContext.aBreakList.clear();

	m_cContext.m_cLabels.clear();
	m_cContext.m_cLabelsDef.clear();

	//clear functions & variables 
	for (auto function : m_cContext.m_cFunctions) {
		function_t* pFunction = static_cast<function_t*>(function.second);
		if (pFunction) delete pFunction;
	}

	m_cContext.m_cVariables.clear();
	m_cContext.m_cFunctions.clear();

	for (unsigned int i = 0; i < m_apCallFunctions.size(); i++) {
		delete m_apCallFunctions[i];
	}

	m_apCallFunctions.clear();
}

void CCompileModule::PrepareModuleData()
{
	for (auto externValue : m_aExternValues) {
		m_cContext.AddVariable(externValue.first, wxEmptyString, true);
		m_cByteCode.m_aExternValues.push_back(externValue.second);
	}
	for (auto contextValue : m_aContextValues) {
		m_cContext.AddVariable(contextValue.first, wxEmptyString, true);
		m_cByteCode.m_aExternValues.push_back(contextValue.second);
	}
	for (auto pair : m_aContextValues) {

		CValue* contextValue = pair.second;

		//добавляем переменные из контекста
		for (unsigned int i = 0; i < contextValue->GetNProps(); i++) {

			const wxString& propName = contextValue->GetPropName(i);

			//определяем номер и тип переменной
			variable_t cVariables(propName);
			cVariables.m_sContextVar = pair.first;
			cVariables.m_bContext = true;
			cVariables.m_bExport = true;
			cVariables.m_nNumber = i;

			GetContext()->m_cVariables.insert_or_assign(
				StringUtils::MakeUpper(propName), cVariables
			);
		}

		//добавляем методы из контекста
		for (long i = 0; i < contextValue->GetNMethods(); i++) {

			const wxString& methodName = contextValue->GetMethodName(i);

			compileContext_t* compileContext = new compileContext_t(GetContext());
			compileContext->SetModule(this);

			if (contextValue->HasRetVal(i))
				compileContext->m_nReturn = RETURN_FUNCTION;
			else
				compileContext->m_nReturn = RETURN_PROCEDURE;

			//определяем номер и тип функции
			function_t* pFunction = new function_t(methodName, compileContext);
			pFunction->m_nStart = i;
			pFunction->m_bContext = true;
			pFunction->m_bExport = true;
			pFunction->m_sContextVar = pair.first;

			paramVariable_t contextVariable;
			contextVariable.m_vData.m_nArray = DEF_VAR_DEFAULT;
			contextVariable.m_vData.m_nIndex = DEF_VAR_DEFAULT;
			for (int n = 0; n < contextValue->GetNParams(i); n++) {
				pFunction->m_aParamList.push_back(contextVariable);
			}

			pFunction->m_sRealName = methodName;
			pFunction->m_sShortDescription = contextValue->GetMethodHelper(i);

			//проверка на типизированность
			GetContext()->m_cFunctions.insert_or_assign(
				StringUtils::MakeUpper(methodName), pFunction
			);
		}
	}
}

/**
 * SetError
 * Назначение:
 * Запомнить ошибку трансляции и вызвать исключение
 * Возвращаемое значение:
 * Метод не возвращает управление!
 */
void CCompileModule::SetError(int codeError, const wxString& errorDesc)
{
	wxString fileName, moduleName, docPath; int currPos = 0, currLine = 0;

	if (m_nCurrentCompile >= m_aLexemList.size()) {
		m_nCurrentCompile = m_aLexemList.size() - 1;
	}

	if (m_nCurrentCompile > 0 &&
		m_nCurrentCompile < m_aLexemList.size()) {
		fileName = m_aLexemList.at(m_nCurrentCompile - 1).m_sFileName;
		moduleName = m_aLexemList.at(m_nCurrentCompile - 1).m_sModuleName;
		docPath = m_aLexemList.at(m_nCurrentCompile - 1).m_sDocPath;

		currPos = m_aLexemList.at(m_nCurrentCompile - 1).m_nNumberString;
		currLine = m_aLexemList.at(m_nCurrentCompile - 1).m_nNumberLine + 1;
	}
	else if (m_nCurrentCompile < m_aLexemList.size()) {
		fileName = m_aLexemList.at(m_nCurrentCompile).m_sFileName;
		moduleName = m_aLexemList.at(m_nCurrentCompile).m_sModuleName;
		docPath = m_aLexemList.at(m_nCurrentCompile).m_sDocPath;

		currPos = m_aLexemList.at(m_nCurrentCompile).m_nNumberString;
		currLine = m_aLexemList.at(m_nCurrentCompile).m_nNumberLine;
	}

	CTranslateModule::SetError(codeError,
		fileName, moduleName, docPath,
		currPos, currLine,
		errorDesc);
}

/**
 * другой вариант функции
 */
void CCompileModule::SetError(int nErr, char c)
{
	SetError(nErr, wxString::Format(wxT("%c"), c));
}

//////////////////////////////////////////////////////////////////////
// Compiling
//////////////////////////////////////////////////////////////////////

/**
 *добавление в байт код информации о текущей строке
 */
void CCompileModule::AddLineInfo(byteRaw_t& code)
{
	code.m_sModuleName = m_sModuleName;
	code.m_sDocPath = m_sDocPath;
	code.m_sFileName = m_sFileName;

	if (m_nCurrentCompile >= 0) {
		if (m_nCurrentCompile < m_aLexemList.size()) {
			if (m_aLexemList[m_nCurrentCompile].m_nType != ENDPROGRAM) {
				code.m_sModuleName = m_aLexemList[m_nCurrentCompile].m_sModuleName;
				code.m_sDocPath = m_aLexemList[m_nCurrentCompile].m_sDocPath;
				code.m_sFileName = m_aLexemList[m_nCurrentCompile].m_sFileName;
			}
			code.m_nNumberString = m_aLexemList[m_nCurrentCompile].m_nNumberString;
			code.m_nNumberLine = m_aLexemList[m_nCurrentCompile].m_nNumberLine;
		}
	}
}

/**
 * GetLexem
 * Назначение:
 * Получить следующую лексему из списка байт кода и увеличть счетчик текущей позиции на 1
 * Возвращаемое значение:
 * 0 или указатель на лексему
 */
lexem_t CCompileModule::GetLexem()
{
	lexem_t lex;
	if (m_nCurrentCompile + 1 < m_aLexemList.size()) {
		lex = m_aLexemList[++m_nCurrentCompile];
	}
	return lex;
}

//Получить следующую лексему из списка байт кода без увеличения счетчика текущей позиции
lexem_t CCompileModule::PreviewGetLexem()
{
	lexem_t lex;
	while (true)
	{
		lex = GetLexem();
		if (!(lex.m_nType == DELIMITER && lex.m_nData == ';')) break;
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
lexem_t CCompileModule::GETLexem()
{
	lexem_t lex = GetLexem();
	if (lex.m_nType == ERRORTYPE) {
		SetError(ERROR_CODE_DEFINE);
	}
	return lex;
}
/**
 * GETDelimeter
 * Назначение:
 * Получить следующую лексему как заданный разделитель
 * Возвращаемое значение:
 * нет (в случае неудачи генерится исключение)
 */
void CCompileModule::GETDelimeter(char c)
{
	lexem_t lex = GETLexem();
	if (!(lex.m_nType == DELIMITER && lex.m_nData == c)) {
		SetError(ERROR_DELIMETER, c);
	}
}

/**
 * IsKeyWord
 * Назначение:
 * Проверить является ли текущая лексема байт-кода заданным ключевым словом
 * Возвращаемое значение:
 * true, false
 */
bool CCompileModule::IsKeyWord(int nKey)
{
	if (m_nCurrentCompile + 1 < m_aLexemList.size()) {
		const lexem_t& lex = m_aLexemList[m_nCurrentCompile];
		if (lex.m_nType == KEYWORD && lex.m_nData == nKey)
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
bool CCompileModule::IsNextKeyWord(int nKey)
{
	if (m_nCurrentCompile + 1 < m_aLexemList.size()) {
		const lexem_t& lex = m_aLexemList[m_nCurrentCompile + 1];
		if (lex.m_nType == KEYWORD && lex.m_nData == nKey)
			return true;
	}
	return false;
}

/**
 * IsDelimeter
 * Назначение:
 * Проверить является ли текущая лексема байт-кода заданным разделителем
 * Возвращаемое значение:
 * true,false
 */
bool CCompileModule::IsDelimeter(char c)
{
	if (m_nCurrentCompile + 1 < m_aLexemList.size()) {
		const lexem_t& lex = m_aLexemList[m_nCurrentCompile];
		if (lex.m_nType == DELIMITER && lex.m_nData == c)
			return true;
	}
	return false;
}

/**
 * IsNextDelimeter
 * Назначение:
 * Проверить является ли следующая лексема байт-кода заданным разделителем
 * Возвращаемое значение:
 * true,false
 */
bool CCompileModule::IsNextDelimeter(char c)
{
	if (m_nCurrentCompile + 1 < m_aLexemList.size()) {
		const lexem_t& lex = m_aLexemList[m_nCurrentCompile + 1];
		if (lex.m_nType == DELIMITER && lex.m_nData == c)
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
void CCompileModule::GETKeyWord(int nKey)
{
	const lexem_t& lex = GETLexem();
	if (!(lex.m_nType == KEYWORD && lex.m_nData == nKey)) {
		SetError(ERROR_KEYWORD,
			wxString::Format(wxT("%s"), s_aKeyWords[nKey].Eng)
		);
	}
}

/**
 * GETIdentifier
 * Получить следующую лексему как заданное ключевое слово
 * Возвращаемое значение:
 * строка-идентификатор
 */
wxString CCompileModule::GETIdentifier(bool realName)
{
	const lexem_t& lex = GETLexem();
	if (lex.m_nType != IDENTIFIER) {
		if (realName && lex.m_nType == KEYWORD) {
			return lex.m_sData;
		}
		SetError(ERROR_IDENTIFIER_DEFINE);
	}

	if (realName) {
		return lex.m_vData.m_sData;
	}

	return lex.m_sData;
}

/**
 * GETConstant
 * Получить следующую лексему как константу
 * Возвращаемое значение:
 * константа
 */
CValue CCompileModule::GETConstant()
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
		SetError(ERROR_CONST_DEFINE);

	if (iNumRequire) {
		//проверка на то чтобы константа имела числовой тип
		if (lex.m_vData.GetType() != eValueTypes::TYPE_NUMBER)
			SetError(ERROR_CONST_DEFINE);
		//меняем знак при минусе
		if (iNumRequire == -1)
			lex.m_vData.m_fData = -lex.m_vData.m_fData;
	}
	return lex.m_vData;
}

//получение номера константой строки (для определения номера метода)
int CCompileModule::GetConstString(const wxString& methodName)
{
	auto foundedIt = std::find_if(m_aHashConstList.begin(), m_aHashConstList.end(),
		[methodName](std::pair <const wxString, unsigned int>& pair) {
			return StringUtils::CompareString(methodName, pair.first);
		}
	);

	if (foundedIt != m_aHashConstList.end())
		return foundedIt->second - 1;

	m_cByteCode.m_aConstList.push_back(methodName);
	m_aHashConstList.insert_or_assign(methodName, m_cByteCode.m_aConstList.size());

	return m_cByteCode.m_aConstList.size() - 1;
}

/**
 * AddVariable
 * Назначение:
 * Добавить имя и адрес внешней переменной в специальный массив для дальнейшего использования
 */
void CCompileModule::AddVariable(const wxString& varName, const CValue& vObject)
{
	if (varName.IsEmpty())
		return;

	//учитываем внешние переменные при компиляции
	m_aExternValues[varName.Upper()] = vObject.m_typeClass == eValueTypes::TYPE_REFFER
		? vObject.GetRef() : const_cast<CValue*>(&vObject);

	//ставим флаг для перекомпиляции
	m_bNeedRecompile = true;
}

/**
 * AddVariable
 * Назначение:
 * Добавить имя и адрес внешней перменной в специальный массив для дальнейшего использования
 */
void CCompileModule::AddVariable(const wxString& varName, CValue* pValue)
{
	if (varName.IsEmpty())
		return;

	//учитываем внешние переменные при компиляции
	m_aExternValues[varName.Upper()] = pValue;

	//ставим флаг для перекомпиляции
	m_bNeedRecompile = true;
}

/**
 * AddContextVariable
 * Назначение:
 * Добавить имя и адрес внешней перменной в специальный массив для дальнейшего использования
 */
void CCompileModule::AddContextVariable(const wxString& varName, const CValue& vObject)
{
	if (varName.IsEmpty())
		return;

	//добавляем переменные из контекста
	m_aContextValues[varName.Upper()] = vObject.m_typeClass == eValueTypes::TYPE_REFFER ? vObject.GetRef() : const_cast<CValue*>(&vObject);

	//ставим флаг для перекомпиляции
	m_bNeedRecompile = true;
}

/**
 * AddContextVariable
 * Назначение:
 * Добавить имя и адрес внешней перменной в специальный массив для дальнейшего использования
 */
void CCompileModule::AddContextVariable(const wxString& varName, CValue* pValue)
{
	if (varName.IsEmpty())
		return;

	//добавляем переменные из контекста
	m_aContextValues[varName.Upper()] = pValue;

	//ставим флаг для перекомпиляции
	m_bNeedRecompile = true;
}

/**
 * RemoveVariable
 * Назначение:
 * Удалить имя и адрес внешней перменной
 */
void CCompileModule::RemoveVariable(const wxString& varName)
{
	if (varName.IsEmpty())
		return;

	m_aExternValues.erase(varName.Upper());
	m_aContextValues.erase(varName.Upper());

	//ставим флаг для перекомпиляции
	m_bNeedRecompile = true;
}

/**
 * Recompile
 * Назначение:
 * Трасляция и перекомпиляция текущего исходного кода в байт-код (объектный код)
 * Возвращаемое значение:
 * true,false
 */
bool CCompileModule::Recompile()
{
	//clear functions & variables 
	Reset();

	if (m_parent != NULL) {
		if (m_moduleObject &&
			m_moduleObject->IsGlobalModule()) {

			m_sModuleName = m_moduleObject->GetFullName();
			m_sDocPath = m_moduleObject->GetDocPath();
			m_sFileName = m_moduleObject->GetFileName();

			m_bNeedRecompile = false;

			Load(m_moduleObject->GetModuleText());

			return m_parent ?
				m_parent->Compile() : true;
		}
	}

	//контекст самого модуля
	m_pContext = GetContext();

	if (m_moduleObject) {
		m_cByteCode.m_sModuleName = m_moduleObject->GetFullName();

		if (m_parent) {
			m_cByteCode.m_parent = &m_parent->m_cByteCode;
			m_cContext.m_parentContext = &m_parent->m_cContext;
		}

		m_sModuleName = m_moduleObject->GetFullName();
		m_sDocPath = m_moduleObject->GetDocPath();
		m_sFileName = m_moduleObject->GetFileName();

		Load(m_moduleObject->GetModuleText());
	}

	//prepare lexem 
	if (!PrepareLexem()) {
		return false;
	}

	//подготовить контекстные переменные 
	PrepareModuleData();

	//Компиляция 
	if (CompileModule()) {
		m_bNeedRecompile = false;
		return true;
	}

	m_bNeedRecompile = true;
	return false;
}

/**
 * Compile
 * Назначение:
 * Трасляция и компиляция исходного кода в байт-код (объектный код)
 * Возвращаемое значение:
 * true,false
 */
bool CCompileModule::Compile()
{
	//clear functions & variables 
	Reset();

	if (m_parent) {
		if (m_moduleObject &&
			m_moduleObject->IsGlobalModule()) {

			m_sModuleName = m_moduleObject->GetFullName();
			m_sDocPath = m_moduleObject->GetDocPath();
			m_sFileName = m_moduleObject->GetFileName();

			m_bNeedRecompile = false;

			Load(m_moduleObject->GetModuleText());

			return m_parent ?
				m_parent->Compile() : true;
		}
	}

	//контекст самого модуля
	m_pContext = GetContext();

	//рекурсивно компилируем модули на случай каких-либо изменений 
	if (m_parent) {

		std::stack<CCompileModule*> aCompileModules;

		CCompileModule* parentModule = m_parent; bool needRecompile = false;

		while (parentModule)
		{
			if (parentModule->m_bNeedRecompile) {
				needRecompile = true;
			}

			if (needRecompile) {
				aCompileModules.push(parentModule);
			}

			parentModule = parentModule->GetParent();
		}

		while (!aCompileModules.empty())
		{
			CCompileModule* compileModule = aCompileModules.top();

			if (!compileModule->Recompile()) {
				return false;
			}

			aCompileModules.pop();
		}
	}

	if (m_moduleObject) {

		m_cByteCode.m_sModuleName = m_moduleObject->GetFullName();

		if (m_parent) {
			m_cByteCode.m_parent = &m_parent->m_cByteCode;
			m_cContext.m_parentContext = &m_parent->m_cContext;
		}

		m_sModuleName = m_moduleObject->GetFullName();
		m_sDocPath = m_moduleObject->GetDocPath();
		m_sFileName = m_moduleObject->GetFileName();

		Load(m_moduleObject->GetModuleText());
	}

	//prepare lexem 
	if (!PrepareLexem()) {
		return false;
	}

	//подготовить контекстные переменные 
	PrepareModuleData();

	//Компиляция 
	if (CompileModule()) {
		m_bNeedRecompile = false;
		return true;
	}

	m_bNeedRecompile = true;
	return false;
}

bool CCompileModule::IsTypeVar(const wxString& typeVar)
{
	if (!typeVar.IsEmpty()) {
		if (CValue::IsRegisterObject(typeVar, eObjectType::eObjectType_simple))
			return true;
	}
	else {
		const lexem_t& lex = PreviewGetLexem();
		if (CValue::IsRegisterObject(lex.m_sData, eObjectType::eObjectType_simple))
			return true;
	}

	return false;
}

wxString CCompileModule::GetTypeVar(const wxString& sType)
{
	if (!sType.IsEmpty()) {
		if (!CValue::IsRegisterObject(sType, eObjectType::eObjectType_simple))
			SetError(ERROR_TYPE_DEF);
		return sType.Upper();
	}
	else {
		const lexem_t& lex = GETLexem();
		if (!CValue::IsRegisterObject(lex.m_sData, eObjectType::eObjectType_simple))
			SetError(ERROR_TYPE_DEF);
		return lex.m_sData.Upper();
	}
}

/**
 * CompileDeclaration
 * Назначение:
 * Компиляция явного объявления переменных
 * Возвращаемое значение:
 * true,false
 */
bool CCompileModule::CompileDeclaration()
{
	const lexem_t& lex = PreviewGetLexem(); wxString sType;
	if (IDENTIFIER == lex.m_nType) {
		sType = GetTypeVar(); //типизированное задание переменных
	}
	else {
		GETKeyWord(KEY_VAR);
	}

	while (true) {
		wxString sRealName = GETIdentifier(true);
		wxString sName = StringUtils::MakeUpper(sRealName);
		int nParentNumber = 0;
		compileContext_t* pCurContext = GetContext();
		while (pCurContext) {
			nParentNumber++;
			if (nParentNumber > MAX_OBJECTS_LEVEL) {
				CSystemObjects::Message(pCurContext->m_compileModule->GetModuleName());
				if (nParentNumber > 2 * MAX_OBJECTS_LEVEL) {
					CTranslateError::Error("Recursive call of modules!");
				}
			}
			variable_t* currentVariable = NULL;
			if (pCurContext->FindVariable(sName, currentVariable)) { //нашли
				if (currentVariable->m_bExport ||
					pCurContext->m_compileModule == this) {
					SetError(ERROR_DEF_VARIABLE, sRealName);
				}
			}
			pCurContext = pCurContext->m_parentContext;
		}

		int nArrayCount = -1;
		if (IsNextDelimeter('[')) { //это объявление массива
			nArrayCount = 0;
			GETDelimeter('[');
			if (!IsNextDelimeter(']'))
			{
				CValue vConst = GETConstant();
				if (vConst.GetType() != eValueTypes::TYPE_NUMBER ||
					vConst.GetNumber() < 0) {
					SetError(ERROR_ARRAY_SIZE_CONST);
				}
				nArrayCount = vConst.GetInteger();
			}
			GETDelimeter(']');
		}

		bool bExport = false;

		if (IsNextKeyWord(KEY_EXPORT)) {
			if (bExport)//было объявление Экспорт
				break;
			GETKeyWord(KEY_EXPORT);
			bExport = true;
		}

		//не было еще объявления переменной - добавляем
		param_t variable =
			m_pContext->AddVariable(sRealName, sType, bExport);

		if (nArrayCount >= 0) {//записываем информацию о массивах
			byteRaw_t code;
			AddLineInfo(code);
			code.m_nOper = OPER_SET_ARRAY_SIZE;
			code.m_param1 = variable;
			code.m_param2.m_nArray = nArrayCount;//число элементов в массиве
			m_cByteCode.m_aCodeList.push_back(code);
		}

		AddTypeSet(variable);

		if (IsNextDelimeter('=')) { //начальная инициализация - работает только внутри текста модулей (но не пере объявл. процедур и функций)
			if (nArrayCount >= 0) {
				GETDelimeter(',');//Error!
			}

			GETDelimeter('=');

			byteRaw_t code;
			AddLineInfo(code);
			code.m_nOper = OPER_LET;
			code.m_param1 = variable;
			code.m_param2 = GetExpression();
			m_cByteCode.m_aCodeList.push_back(code);
		}

		if (!IsNextDelimeter(','))
			break;

		GETDelimeter(',');
	}

	return true;
}

/**
 * CompileModule
 * Назначение:
 * Компиляция всего байт-кода (создание из набора лексем объектного кода)
 * Возвращаемое значение:
 * true,false
*/
bool CCompileModule::CompileModule()
{
	lexem_t lex;
	//устанавливаем курсор на начало массива лексем
	m_nCurrentCompile = -1;
	m_pContext = GetContext();//контекст самого модуля
	while ((lex = PreviewGetLexem()).m_nType != ERRORTYPE) {
		if ((KEYWORD == lex.m_nType && KEY_VAR == lex.m_nData) || (IDENTIFIER == lex.m_nType && IsTypeVar(lex.m_sData))) {
			if (!m_bCommonModule) {
				m_pContext = GetContext();
				CompileDeclaration();//загружаем объявление переменных
			}
			else {
				SetError(ERROR_ONLY_FUNCTION);
			}
		}
		else if (KEYWORD == lex.m_nType && (KEY_PROCEDURE == lex.m_nData || KEY_FUNCTION == lex.m_nData)) {
			CompileFunction();//загружаем объявление функций
			//не забываем восстанавливать текущий контекст модуля (если это нужно)...
		}
		else {
			break;
		}
	}

	//загружаем исполняемое тело модуля
	m_pContext = GetContext();//контекст самого модуля
	m_cByteCode.m_lStartModule = 0;//m_cByteCode.m_aCodeList.size() - 1;
	CompileBlock();
	m_pContext->DoLabels();

	//ставим признак конца программы
	byteRaw_t code;
	AddLineInfo(code);
	code.m_nOper = OPER_END;
	m_cByteCode.m_aCodeList.push_back(code);
	m_cByteCode.m_lVarCount = m_pContext->m_cVariables.size();

	m_pContext = GetContext();

	//дообрабатываем процедуры и функции, вызовы которых были до их объявления
	//для это в конце массива байт-кодов добавляем новый код по вызову таких функций,
	//а для корректной работы в места раннего вызова вставляем операторы GOTO
	for (unsigned int i = 0; i < m_apCallFunctions.size(); i++) {
		m_cByteCode.m_aCodeList[m_apCallFunctions[i]->m_nAddLine].m_param1.m_nIndex =
			m_cByteCode.m_aCodeList.size(); //переход на вызов функции
		if (AddCallFunction(m_apCallFunctions[i])) {
			//корректрируем переходы
			byteRaw_t code;
			AddLineInfo(code);
			code.m_nOper = OPER_GOTO;
			code.m_nNumberLine = m_apCallFunctions[i]->m_nNumberLine;
			code.m_nNumberString = m_apCallFunctions[i]->m_nNumberString;
			code.m_param1.m_nIndex = m_apCallFunctions[i]->m_nAddLine + 1; //после вызова функции возвращаемся назад
			m_cByteCode.m_aCodeList.push_back(code);
		}
	}

	m_pContext = GetContext();

	//Получаем список переменных
	for (auto it : m_pContext->m_cVariables) {
		if (it.second.m_bTempVar ||
			it.second.m_bContext)
			continue;
		m_cByteCode.m_aVarList[it.first] = it.second.m_nNumber;
		if (it.second.m_bExport) {
			m_cByteCode.m_aExportVarList[it.first] = it.second.m_nNumber;
		}
	}

	if (m_nCurrentCompile + 1 < m_aLexemList.size() - 1) {
		SetError(ERROR_END_PROGRAM);
	}

	m_cByteCode.SetModule(this);
	//компиляция завершена успешно
	m_cByteCode.m_bCompile = true;

	return true;
}

//поиск определения функции в текущем модуле и во всех родительских
function_t* CCompileModule::GetFunction(const wxString& name, int* pNumber)
{
	int nCanUseLocalInParent = m_cContext.m_nFindLocalInParent - 1;
	int nNumber = 0;
	//ищем в текущем модуле
	function_t* foundedFunction = NULL;
	if (!GetContext()->FindFunction(name, foundedFunction)) {
		CCompileModule* pCurModule = m_parent;
		while (pCurModule != NULL) {
			nNumber++;
			foundedFunction = pCurModule->m_pContext->m_cFunctions[name];
			if (foundedFunction != NULL) { //нашли
				//смотрим это экспортная функция или нет
				if (nCanUseLocalInParent > 0 ||
					foundedFunction->m_bExport)
					break;//ок
				foundedFunction = NULL;
			}
			nCanUseLocalInParent--;
			pCurModule = pCurModule->m_parent;
		}
	}
	if (pNumber)
		*pNumber = nNumber;
	return foundedFunction;
}

//Добавление в массив байт-кода вызова функции
bool CCompileModule::AddCallFunction(сallFunction_t* pRealCall)
{
	int nModuleNumber = 0;
	//находим определение функции
	function_t* foundedFunction = GetFunction(pRealCall->m_sName, &nModuleNumber);
	if (foundedFunction == NULL) {
		m_nCurrentCompile = pRealCall->m_nError;
		SetError(ERROR_CALL_FUNCTION, pRealCall->m_sRealName);//нет такой функции в модуле
		return false;
	}

	//проверяем соответствие количество переданных и объявленных параметров
	unsigned int nRealCount = pRealCall->m_aParamList.size();
	unsigned int nDefCount = foundedFunction->m_aParamList.size();

	if (nRealCount > nDefCount) {
		m_nCurrentCompile = pRealCall->m_nError;
		SetError(ERROR_MANY_PARAMS);//Слишком много фактических параметров
		return false;
	}

	byteRaw_t code;
	AddLineInfo(code);

	code.m_nNumberString = pRealCall->m_nNumberString;
	code.m_nNumberLine = pRealCall->m_nNumberLine;
	code.m_sModuleName = pRealCall->m_sModuleName;

	if (foundedFunction->m_bContext) {//виртуальная функция - вызов заменям на конструкцию Контекст.ИмяФункции(...)
		code.m_nOper = OPER_CALL_M;
		code.m_param1 = pRealCall->m_sRetValue;//переменная, в которую возвращается значение
		code.m_param2 = pRealCall->m_sContextVal;//переменная у которой вызывается метод
		code.m_param3.m_nIndex = GetConstString(pRealCall->m_sName);//номер вызываемого метода из списка встретившихся методов
		code.m_param3.m_nArray = nDefCount;//число параметров
	}
	else {
		code.m_nOper = OPER_CALL;
		code.m_param1 = pRealCall->m_sRetValue;//переменная, в которую возвращается значение
		code.m_param2.m_nArray = nModuleNumber;//номер модуля
		code.m_param2.m_nIndex = foundedFunction->m_nStart;//стартовая позиция
		code.m_param3.m_nArray = nDefCount;//число параметров
		code.m_param3.m_nIndex = foundedFunction->m_lVarCount;//число локальных переменных
		code.m_param4 = pRealCall->m_sContextVal;//контекстная переменная
	}

	m_cByteCode.m_aCodeList.push_back(code);

	for (unsigned int i = 0; i < nDefCount; i++) {
		byteRaw_t code;
		AddLineInfo(code);
		code.m_nOper = OPER_SET;//идет передача параметров
		bool defaultValue = false;
		if (i < nRealCount) {
			code.m_param1 = pRealCall->m_aParamList[i];
			if (code.m_param1.m_nArray == DEF_VAR_SKIP) { //нужно подставить значение по умолчанию
				defaultValue = true;
			}
			else {  //тип передачи значений
				code.m_param2.m_nIndex = foundedFunction->m_aParamList[i].m_bByRef;
			}
		}
		else {
			defaultValue = true;
		}
		if (defaultValue) {
			if (foundedFunction->m_aParamList[i].m_vData.m_nArray == DEF_VAR_SKIP) {
				m_nCurrentCompile = pRealCall->m_nError;
				SetError(ERROR_FEW_PARAMS);//Недостаточно фактических параметров
			}
			code.m_nOper = OPER_SETCONST;//значения по умолчанию
			code.m_param1 = foundedFunction->m_aParamList[i].m_vData;
		}
		m_cByteCode.m_aCodeList.push_back(code);
	}

	return true;
}

/**
 * CompileFunction
 * Назначение:
 * Создание объектного кода для одной функции (процедуры)
 * Алгоритм:
 * -Определить число формальных параметров
 * -Определить способы вызова формальных параметров (по ссылке или по значению)
 * -Определить значения по умолчанию
 * -Определить число локальных переменных
 * -Определить возвращает ли функция значение
 *
 * Возвращаемое значение:
 * true,false
 */
bool CCompileModule::CompileFunction()
{
	//сейчас мы на уровне лексемы, где задано ключевое слово FUNCTION или PROCEDURE
	lexem_t lex;

	if (IsNextKeyWord(KEY_FUNCTION)) {
		GETKeyWord(KEY_FUNCTION);
		m_pContext = new compileContext_t(GetContext());//создаем новый контекст, в котором будем компилировать тело функции
		m_pContext->SetModule(this);
		m_pContext->m_nReturn = RETURN_FUNCTION;
	}
	else if (IsNextKeyWord(KEY_PROCEDURE)) {
		GETKeyWord(KEY_PROCEDURE);
		m_pContext = new compileContext_t(GetContext());//создаем новый контекст, в котором будем компилировать тело процедуры
		m_pContext->SetModule(this);
		m_pContext->m_nReturn = RETURN_PROCEDURE;
	}
	else {
		SetError(ERROR_FUNC_DEFINE);
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
		if (nRes > 0) {
			if (sShortDescription[nRes - 1] == '/') { //итак - это комментарий
				sShortDescription = sShortDescription.Mid(nRes + 1);
			}
		}
		else {
			nRes = sShortDescription.find_first_of(')');
			sShortDescription = sShortDescription.Left(nRes + 1);
		}
	}

	//получаем имя функции
	wxString sFuncRealName = GETIdentifier(true);
	wxString sFuncName = StringUtils::MakeUpper(sFuncRealName);

	int errorPlace = m_nCurrentCompile;

	function_t* pFunction = new function_t(sFuncName, m_pContext);
	pFunction->m_sRealName = sFuncRealName;
	pFunction->m_sShortDescription = sShortDescription;
	pFunction->m_nNumberLine = m_nNumberLine;

	//компилируем список формальных параметров + регистрируем их как локальные
	GETDelimeter('(');
	if (!IsNextDelimeter(')')) {
		while (true) {
			wxString typeVar = wxEmptyString;
			//проверка на типизированность
			if (IsTypeVar())
				typeVar = GetTypeVar();
			paramVariable_t cVariable;
			if (IsNextKeyWord(KEY_VAL)) {
				GETKeyWord(KEY_VAL);
				cVariable.m_bByRef = true;
			}
			const wxString& realName = GETIdentifier(true);
			cVariable.m_sName = realName;
			cVariable.m_sType = typeVar;
			variable_t* foundedVar = NULL;
			//регистрируем эту переменную как локальную
			if (m_pContext->FindVariable(realName, foundedVar)) { //было объявление + повторное объявление = ошибка
				SetError(ERROR_IDENTIFIER_DUPLICATE, realName);
			}
			if (IsNextDelimeter('[')) {//это массив
				GETDelimeter('[');
				GETDelimeter(']');
			}
			else if (IsNextDelimeter('=')) {
				GETDelimeter('=');
				cVariable.m_vData = FindConst(GETConstant());
			}
			m_pContext->AddVariable(realName, typeVar);
			pFunction->m_aParamList.push_back(cVariable);
			if (IsNextDelimeter(')'))
				break;
			GETDelimeter(',');
		}
	}
	GETDelimeter(')');
	if (IsNextKeyWord(KEY_EXPORT)) {
		GETKeyWord(KEY_EXPORT);
		pFunction->m_bExport = true;
	}

	int nParentNumber = 0;
	compileContext_t* pCurContext = GetContext();
	while (pCurContext) {
		nParentNumber++;
		if (nParentNumber > MAX_OBJECTS_LEVEL) {
			CSystemObjects::Message(pCurContext->m_compileModule->GetModuleName());
			if (nParentNumber > 2 * MAX_OBJECTS_LEVEL)
				CTranslateError::Error("Recursive call of modules!");
		}
		function_t* foundedFunc = NULL;
		if (pCurContext->FindFunction(sFuncName, foundedFunc)) { //нашли
			if (foundedFunc->m_bExport ||
				pCurContext->m_compileModule == this) {
				m_nCurrentCompile = errorPlace;
				SetError(ERROR_DEF_FUNCTION, sFuncRealName);
			}
		}
		pCurContext = pCurContext->m_parentContext;
	}

	//проверка на типизированность
	GetContext()->m_cFunctions[sFuncName] = pFunction;

	//вставляем информацию о функции в массив байт-кодов:
	byteRaw_t code0;
	AddLineInfo(code0);
	code0.m_nOper = OPER_FUNC;
#if defined(_LP64) || defined(__LP64__) || defined(__arch64__) || defined(_WIN64)
	code0.m_param1.m_nArray = reinterpret_cast<wxLongLong_t>(m_pContext);
#else
	code0.m_param1.m_nArray = reinterpret_cast<int>(m_pContext);
#endif
	m_cByteCode.m_aCodeList.push_back(code0);

	const long lAddres = pFunction->m_nStart = m_cByteCode.m_aCodeList.size() - 1;

	m_cByteCode.m_aFuncList[sFuncName] = {
		(long)pFunction->m_aParamList.size(),
		lAddres + 1,
		m_pContext->m_nReturn == RETURN_FUNCTION
	};

	if (pFunction->m_bExport) {
		m_cByteCode.m_aExportFuncList[sFuncName] = {
			(long)pFunction->m_aParamList.size(),
			lAddres + 1,
			m_pContext->m_nReturn == RETURN_FUNCTION
		};
	}

	for (unsigned int i = 0; i < pFunction->m_aParamList.size(); i++) {
		//add set oper
		byteRaw_t code;
		AddLineInfo(code);
		if (pFunction->m_aParamList[i].m_vData.m_nArray == DEF_VAR_CONST) {
			code.m_nOper = OPER_SETCONST;//идет передача параметров
		}
		else {
			code.m_nOper = OPER_SET;//идет передача параметров
		}
		code.m_param1 = pFunction->m_aParamList[i].m_vData;
		code.m_param2.m_nIndex = pFunction->m_aParamList[i].m_bByRef;
		m_cByteCode.m_aCodeList.push_back(code);

		//Set type variable
		param_t variable;
		variable.m_sType = pFunction->m_aParamList[i].m_sType;
		variable.m_nArray = 0;
		variable.m_nIndex = i;//индекс совпадает с номером
		AddTypeSet(variable);
	}

	GetContext()->m_sCurFuncName = sFuncName;
	CompileBlock();
	m_pContext->DoLabels();
	GetContext()->m_sCurFuncName = wxEmptyString;

	if (m_pContext->m_nReturn == RETURN_FUNCTION) {
		GETKeyWord(KEY_ENDFUNCTION);
	}
	else {
		GETKeyWord(KEY_ENDPROCEDURE);
	}

	byteRaw_t code;
	AddLineInfo(code);
	code.m_nOper = OPER_ENDFUNC;
	m_cByteCode.m_aCodeList.push_back(code);

	pFunction->m_nFinish = m_cByteCode.m_aCodeList.size() - 1;
	pFunction->m_lVarCount = m_pContext->m_cVariables.size();

	m_cByteCode.m_aCodeList[lAddres].m_param3.m_nIndex = pFunction->m_lVarCount;//число локальных переменных
	m_cByteCode.m_aCodeList[lAddres].m_param3.m_nArray = pFunction->m_aParamList.size();//число формальных параметров

	m_pContext->SetFunction(pFunction);
	return true;
}

/**
 * записываем информацию о типе переменной
 */
void CCompileModule::AddTypeSet(const param_t& sVariable)
{
	const wxString &typeName = sVariable.m_sType;
	if (!typeName.IsEmpty()) {
		byteRaw_t code;
		AddLineInfo(code);
		code.m_nOper = OPER_SET_TYPE;
		code.m_param1 = sVariable;
		code.m_param2.m_nArray = CValue::GetIDObjectFromString(typeName);
		m_cByteCode.m_aCodeList.push_back(code);
	}
}

//Макрос проверки переменной Var типу Str
#define CheckTypeDef(var,type) if(wxStrlen(type) > 0)\
	{\
		if(var.m_sType!=type)\
		{\
			if (CValue::CompareObjectName(type, eValueTypes::TYPE_BOOLEAN)) SetError(ERROR_BAD_TYPE_EXPRESSION_B);\
			else if (CValue::CompareObjectName(type, eValueTypes::TYPE_NUMBER)) SetError(ERROR_BAD_TYPE_EXPRESSION_N);\
			else if (CValue::CompareObjectName(type, eValueTypes::TYPE_STRING))  SetError(ERROR_BAD_TYPE_EXPRESSION_S);\
			else if (CValue::CompareObjectName(type, eValueTypes::TYPE_DATE)) SetError(ERROR_BAD_TYPE_EXPRESSION_D);\
			else SetError(ERROR_BAD_TYPE_EXPRESSION);\
		}\
		if (CValue::CompareObjectName(type, eValueTypes::TYPE_NUMBER)) code.m_nOper+=TYPE_DELTA1;\
		else if (CValue::CompareObjectName(type, eValueTypes::TYPE_STRING)) code.m_nOper+=TYPE_DELTA2;\
		else if (CValue::CompareObjectName(type, eValueTypes::TYPE_DATE)) code.m_nOper+=TYPE_DELTA3;\
        else if (CValue::CompareObjectName(type, eValueTypes::TYPE_BOOLEAN)) code.m_nOper+=TYPE_DELTA4;\
	}


//Макрос корректировки операции по типу переменной
//если она типизированна, то будет выполняться типизированная операция
#define CorrectTypeDef(sKey)\
if(!sKey.m_sType.IsEmpty())\
{\
	if (CValue::CompareObjectName(sKey.m_sType, eValueTypes::TYPE_NUMBER)) code.m_nOper+=TYPE_DELTA1;\
	else if (CValue::CompareObjectName(sKey.m_sType, eValueTypes::TYPE_STRING)) code.m_nOper+=TYPE_DELTA2;\
	else if (CValue::CompareObjectName(sKey.m_sType, eValueTypes::TYPE_DATE)) code.m_nOper+=TYPE_DELTA3;\
    else if (CValue::CompareObjectName(sKey.m_sType, eValueTypes::TYPE_BOOLEAN))  code.m_nOper+=TYPE_DELTA4;\
	else SetError(ERROR_BAD_TYPE_EXPRESSION);\
}

/**
 * CompileBlock
 * Назначение:
 * Создание объектного кода для одного блока (куска кода между какими-либо
 * операторными скобками типа ЦИКЛ...КОНЕЦИКЛА, ЕСЛИ...КОНЕЦЕСЛИ и т.п.
 * nIterNumber - номер вложенного блока
 * Возвращаемое значение:
 * true,false
 */
bool CCompileModule::CompileBlock()
{
	lexem_t lex;
	while ((lex = PreviewGetLexem()).m_nType != ERRORTYPE)
	{
		/*if (IDENTIFIER == lex.m_nType && IsTypeVar(lex.m_sData))
		{
			CompileDeclaration();
		}
		else*/
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

				if (m_pContext->m_nReturn == RETURN_NONE) {
					SetError(ERROR_USE_RETURN); //Оператор Return (Возврат) не может употребляться вне процедуры или функции
				}

				byteRaw_t code;
				AddLineInfo(code);
				code.m_nOper = OPER_RET;

				if (m_pContext->m_nReturn == RETURN_FUNCTION) { //возвращается какое-то значение

					if (IsNextDelimeter(';')) {
						SetError(ERROR_EXPRESSION_REQUIRE);
					}

					code.m_param1 = GetExpression();
				}
				else {
					code.m_param1.m_nArray = DEF_VAR_NORET;
					code.m_param1.m_nIndex = DEF_VAR_NORET;
				}

				m_cByteCode.m_aCodeList.push_back(code);
				break;
			}
			case KEY_TRY:
			{
				GETKeyWord(KEY_TRY);
				byteRaw_t code;
				AddLineInfo(code);
				code.m_nOper = OPER_TRY;
				m_cByteCode.m_aCodeList.push_back(code);
				int nLineTry = m_cByteCode.m_aCodeList.size() - 1;

				CompileBlock();
				code.m_nOper = OPER_ENDTRY;
				m_cByteCode.m_aCodeList.push_back(code);
				int nAddrLine = m_cByteCode.m_aCodeList.size() - 1;

				m_cByteCode.m_aCodeList[nLineTry].m_param1.m_nIndex = m_cByteCode.m_aCodeList.size();

				GETKeyWord(KEY_EXCEPT);
				CompileBlock();
				GETKeyWord(KEY_ENDTRY);

				m_cByteCode.m_aCodeList[nAddrLine].m_param1.m_nIndex = m_cByteCode.m_aCodeList.size();
				break;
			}
			case KEY_RAISE:
			{
				GETKeyWord(KEY_RAISE);
				byteRaw_t code;
				AddLineInfo(code);
				if (IsNextDelimeter('(')) {
					code.m_nOper = OPER_RAISE_T;
					GETDelimeter('(');
					code.m_param1 = GetExpression();
					GETDelimeter(')');
				}
				else {
					code.m_nOper = OPER_RAISE;
				}
				m_cByteCode.m_aCodeList.push_back(code);
				break;
			}
			case KEY_CONTINUE:
			{
				GETKeyWord(KEY_CONTINUE);
				if (m_pContext->aContinueList[m_pContext->m_nDoNumber]) {
					byteRaw_t code;
					AddLineInfo(code);
					code.m_nOper = OPER_GOTO;
					m_cByteCode.m_aCodeList.push_back(code);
					int nAddrLine = m_cByteCode.m_aCodeList.size() - 1;
					CDefIntList* pList = m_pContext->aContinueList[m_pContext->m_nDoNumber];
					pList->push_back(nAddrLine);
				}
				else {
					SetError(ERROR_USE_CONTINUE);//Опереатор Continue (Продолжить)  может употребляться только внутри цикла		
				}
				break;
			}
			case KEY_BREAK:
			{
				GETKeyWord(KEY_BREAK);
				if (m_pContext->aBreakList[m_pContext->m_nDoNumber])
				{
					byteRaw_t code;
					AddLineInfo(code);
					code.m_nOper = OPER_GOTO;
					m_cByteCode.m_aCodeList.push_back(code);
					int nAddrLine = m_cByteCode.m_aCodeList.size() - 1;
					CDefIntList* pList = m_pContext->aBreakList[m_pContext->m_nDoNumber];
					pList->push_back(nAddrLine);
				}
				else SetError(ERROR_USE_BREAK);//Опереатор Break(Прервать)  может употребляться только внутри цикла
				break;
			}
			case KEY_FUNCTION:
			case KEY_PROCEDURE:
			{
				GetLexem();
				SetError(ERROR_USE_BLOCK);
				break;
			}
			default: return true;//значит встретилась завершающая данный блок операторная скобка (например КОНЕЦЕСЛИ, КОНЕЦЦИКЛА, КОНЕЦФУНКЦИИ и т.п.)		
			}
		}
		else
		{
			lex = GetLexem();

			if (IDENTIFIER == lex.m_nType) {
				m_pContext->m_nTempVar = 0;
				if (IsNextDelimeter(':')) {//это встретилось задание метки
					unsigned int pLabel = m_pContext->m_cLabelsDef[lex.m_sData];
					if (pLabel > 0)
						SetError(ERROR_IDENTIFIER_DUPLICATE, lex.m_sData);//произошло дублированное определения меток
					//записываем адрес перехода:
					m_pContext->m_cLabelsDef[lex.m_sData] = m_cByteCode.m_aCodeList.size() - 1;
					GETDelimeter(':');
				}
				else { //здесь обрабатываются вызовы функций, методов, присваиваение выражений
					m_nCurrentCompile--;//шаг назад
					int nSet = 1;
					if (m_bCommonModule && m_pContext == GetContext()) SetError(ERROR_ONLY_FUNCTION);
					param_t variable = GetCurrentIdentifier(nSet);//получаем левую часть выражения (до знака '=')
					if (nSet) { //если есть правая часть, т.е. знак '='
						GETDelimeter('=');//это присваивание переменной какого-то выражения
						param_t sExpression = GetExpression();
						byteRaw_t code;
						code.m_nOper = OPER_LET;
						AddLineInfo(code);

						CheckTypeDef(sExpression, variable.m_sType);
						variable.m_sType = sExpression.m_sType;

						bool bShortLet = false; int n = 0;

						if (DEF_VAR_TEMP == sExpression.m_nArray) { //сокращаем только временные переменные
							n = m_cByteCode.m_aCodeList.size() - 1;
							if (n >= 0) {
								int nOperation = m_cByteCode.m_aCodeList[n].m_nOper;
								nOperation = nOperation % TYPE_DELTA1;
								if (OPER_MULT == nOperation ||
									OPER_DIV == nOperation ||
									OPER_ADD == nOperation ||
									OPER_SUB == nOperation ||
									OPER_MOD == nOperation ||
									OPER_GT == nOperation ||
									OPER_GE == nOperation ||
									OPER_LS == nOperation ||
									OPER_LE == nOperation ||
									OPER_NE == nOperation ||
									OPER_EQ == nOperation
									)
								{
									bShortLet = true;//сокращаем одно присваивание
								}
							}
						}

						if (bShortLet) {
							m_cByteCode.m_aCodeList[n].m_param1 = variable;
						}
						else {
							code.m_param1 = variable;
							code.m_param2 = sExpression;
							m_cByteCode.m_aCodeList.push_back(code);
						}
					}
				}
			}
			else {
				if (DELIMITER == lex.m_nType
					&& ';' == lex.m_nData)
				{
				}
				else if (ENDPROGRAM == lex.m_nType) {
					break;
				}
				else {
					SetError(ERROR_CODE);
				}
			}
		}
	}//while
	return true;
}//CompileBlock

bool CCompileModule::CompileNewObject()
{
	GETKeyWord(KEY_NEW);

	wxString sObjectName = GETIdentifier(true);
	int nNumber = GetConstString(sObjectName);

	std::vector <param_t> aParamList;

	if (IsNextDelimeter('('))//это вызов метода
	{
		GETDelimeter('(');

		while (!IsNextDelimeter(')'))
		{
			if (IsNextDelimeter(','))
			{
				param_t data;
				data.m_nArray = DEF_VAR_SKIP;//пропущенный параметр
				data.m_nIndex = DEF_VAR_SKIP;
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

	if (!CValue::IsRegisterObject(sObjectName, eObjectType::eObjectType_object))
		SetError(ERROR_CALL_CONSTRUCTOR, sObjectName);

	byteRaw_t code;
	AddLineInfo(code);
	
	code.m_nOper = OPER_NEW;
	code.m_param2.m_nIndex = nNumber;//номер вызываемого метода из списка встретившихся методов
	code.m_param2.m_nArray = aParamList.size();//число параметров

	param_t variable = GetVariable();
	code.m_param1 = variable;//переменная, в которую возвращается значение
	m_cByteCode.m_aCodeList.push_back(code);

	for (unsigned int i = 0; i < aParamList.size(); i++)
	{
		byteRaw_t code;
		AddLineInfo(code);
		code.m_nOper = OPER_SET;
		code.m_param1 = aParamList[i];
		m_cByteCode.m_aCodeList.push_back(code);
	}

	return true;
}

/**
 * CompileGoto
 * Назначение:
 * Компилирование оператора GOTO (определение расположение метки перехода
 * для последующей ее замены на адрес и тип = LABEL)
 * Возвращаемое значение:
 * true,false
 */
bool CCompileModule::CompileGoto()
{
	GETKeyWord(KEY_GOTO);

	label_t data;
	data.m_sName = GETIdentifier();
	data.m_nLine = m_cByteCode.m_aCodeList.size();//запоминаем те переходы, которые потом надо будет обработать
	data.m_nError = m_nCurrentCompile;
	m_pContext->m_cLabels.push_back(data);

	byteRaw_t code;
	AddLineInfo(code);
	code.m_nOper = OPER_GOTO;
	m_cByteCode.m_aCodeList.push_back(code);

	return true;
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
param_t CCompileModule::GetCurrentIdentifier(int& nIsSet)
{
	param_t variable; int nPrevSet = nIsSet;

	wxString realName = GETIdentifier(true);
	wxString name = StringUtils::MakeUpper(realName);

	if (IsNextDelimeter('(')) { //это вызов функции
		function_t* foundedFunction = NULL;
		if (m_cContext.FindFunction(name, foundedFunction, true)) {
			int nNumber = GetConstString(realName);
			std::vector <param_t> aParamList;
			GETDelimeter('(');
			while (!IsNextDelimeter(')')) {
				if (IsNextDelimeter(',')) {
					param_t data;
					data.m_nArray = DEF_VAR_SKIP;//пропущенный параметр
					data.m_nIndex = DEF_VAR_SKIP;
					aParamList.push_back(data);
				}
				else {
					aParamList.emplace_back(GetExpression());
					if (IsNextDelimeter(')'))
						break;
				}
				GETDelimeter(',');
			}
			GETDelimeter(')');
			if (!nIsSet && foundedFunction != NULL &&
				foundedFunction->m_pContext && foundedFunction->m_pContext->m_nReturn == RETURN_PROCEDURE) {
				SetError(ERROR_USE_PROCEDURE_AS_FUNCTION, foundedFunction->m_sRealName);
			}
			if (aParamList.size() > foundedFunction->m_aParamList.size()) {
				SetError(ERROR_MANY_PARAMS); //Слишком много фактических параметров
			}
			byteRaw_t code;
			AddLineInfo(code);
			code.m_nOper = OPER_CALL_M;
			// переменная у которой вызывается метод
			code.m_param2 = GetVariable(foundedFunction->m_sContextVar, false, true);
			code.m_param3.m_nIndex = nNumber;//номер вызываемого метода из списка встретившихся методов
			code.m_param3.m_nArray = aParamList.size();//число параметров
			variable = GetVariable();
			code.m_param1 = variable;//переменная, в которую возвращается значение
			m_cByteCode.m_aCodeList.push_back(code);
			for (unsigned int i = 0; i < aParamList.size(); i++) {
				byteRaw_t code;
				AddLineInfo(code);
				code.m_nOper = OPER_SET;
				code.m_param1 = aParamList[i];
				m_cByteCode.m_aCodeList.push_back(code);
			}
		}
		else {
			function_t* foundedFunc = NULL;
			if (m_bExpressionOnly)
				foundedFunc = GetFunction(name);
			else if (!GetContext()->FindFunction(name, foundedFunc))
				SetError(ERROR_CALL_FUNCTION, realName);
			if (!nIsSet && foundedFunc && foundedFunc->m_pContext && foundedFunc->m_pContext->m_nReturn == RETURN_PROCEDURE) {
				SetError(ERROR_USE_PROCEDURE_AS_FUNCTION, foundedFunc->m_sRealName);
			}
			variable = GetCallFunction(realName);
		}
		if (IsTypeVar(realName)) {
			variable.m_sType = GetTypeVar(realName);//это приведение типов
		}
		nIsSet = 0;
	}
	else { //это вызов переменной
		variable_t* foundedVar = NULL; nIsSet = 1;
		if (m_cContext.FindVariable(realName, foundedVar, true)) {
			byteRaw_t code;
			AddLineInfo(code);
			int nNumber = GetConstString(realName);
			if (IsNextDelimeter('=') && nPrevSet == 1) {
				GETDelimeter('='); nIsSet = 0;
				code.m_nOper = OPER_SET_A;
				code.m_param1 = GetVariable(foundedVar->m_sContextVar, false, true);//переменная у которой вызывается атрибут
				code.m_param2.m_nIndex = nNumber;//номер вызываемого метода из списка встретившихся атрибутов и методов
				code.m_param3 = GetExpression();
				m_cByteCode.m_aCodeList.push_back(code);
				return variable;
			}
			else {
				code.m_nOper = OPER_GET_A;
				code.m_param2 = GetVariable(foundedVar->m_sContextVar, false, true);//переменная у которой вызывается атрибут
				code.m_param3.m_nIndex = nNumber;//номер вызываемого атрибута из списка встретившихся атрибутов и методов
				variable = GetVariable();
				code.m_param1 = variable;//переменная, в которую возвращается значение
				m_cByteCode.m_aCodeList.push_back(code);
			}
		}
		else {
			bool bCheckError = !nPrevSet;
			if (IsNextDelimeter('.'))//эта переменная содержит вызов метода
				bCheckError = true;
			variable = GetVariable(realName, bCheckError);
		}
	}

MLabel:

	if (IsNextDelimeter('[')) { //это массив
		GETDelimeter('[');
		param_t sKey = GetExpression();
		GETDelimeter(']');
		//определяем тип вызова (т.е. это установка значения массива или получение)
		//Пример:
		//Мас[10]=12; - Set
		//А=Мас[10]; - Get
		//Мас[10][2]=12; - Get,Set
		nIsSet = 0;
		if (IsNextDelimeter('[')) { //проверки типа переменной массива (поддержка многомерных массивов)
			byteRaw_t code;
			AddLineInfo(code);
			code.m_nOper = OPER_CHECK_ARRAY;
			code.m_param1 = variable;//переменная - массив
			code.m_param2 = sKey;//индекс массива
			m_cByteCode.m_aCodeList.push_back(code);
		}
		if (IsNextDelimeter('=') && nPrevSet == 1) {
			GETDelimeter('=');
			byteRaw_t code;
			AddLineInfo(code);
			code.m_nOper = OPER_SET_ARRAY;
			code.m_param1 = variable;//переменная - массив
			code.m_param2 = sKey;//индекс массива (точнее ключ т.к. используется ассоциативный массив)
			code.m_param3 = GetExpression();

			CorrectTypeDef(sKey);//проверка типа значения индексной переменной

			m_cByteCode.m_aCodeList.push_back(code);
			return variable;
		}
		else {
			byteRaw_t code;
			AddLineInfo(code);
			code.m_nOper = OPER_GET_ARRAY;
			code.m_param2 = variable;//переменная - массив
			code.m_param3 = sKey;//индекс массива (точнее ключ т.к. используется ассоциативный массив)
			variable = GetVariable();
			code.m_param1 = variable;//переменная, в которую возвращается значение
			CorrectTypeDef(sKey);//проверка типа значения индексной переменной
			m_cByteCode.m_aCodeList.push_back(code);
		}
		goto MLabel;
	}

	if (IsNextDelimeter('.')) { //это вызов метода или атрибута агрегатного объекта
		GETDelimeter('.');
		wxString sRealMethod = GETIdentifier(true);
		//wxString sMethod = StringUtils::MakeUpper(sRealMethod);
		int nNumber = GetConstString(sRealMethod);
		if (IsNextDelimeter('(')) { //это вызов метода
			std::vector <param_t> aParamList;
			GETDelimeter('(');
			while (!IsNextDelimeter(')')) {
				if (IsNextDelimeter(',')) {
					param_t data;
					data.m_nArray = DEF_VAR_SKIP;//пропущенный параметр
					data.m_nIndex = DEF_VAR_SKIP;
					aParamList.push_back(data);
				}
				else {
					aParamList.emplace_back(GetExpression());
					if (IsNextDelimeter(')')) break;
				}
				GETDelimeter(',');
			}
			GETDelimeter(')');

			byteRaw_t code;
			AddLineInfo(code);
			code.m_nOper = OPER_CALL_M;
			code.m_param2 = variable; // переменная у которой вызывается метод
			code.m_param3.m_nIndex = nNumber;//номер вызываемого метода из списка встретившихся методов
			code.m_param3.m_nArray = aParamList.size();//число параметров
			variable = GetVariable();
			code.m_param1 = variable;//переменная, в которую возвращается значение
			m_cByteCode.m_aCodeList.push_back(code);
			for (unsigned int i = 0; i < aParamList.size(); i++) {
				byteRaw_t code;
				AddLineInfo(code);
				code.m_nOper = OPER_SET;
				code.m_param1 = aParamList[i];
				m_cByteCode.m_aCodeList.push_back(code);
			}

			nIsSet = 0;
		}
		else { //иначе - вызов атрибута
			//определяем тип вызова (т.е. это установка атрибута или получение)
			//Пример:
			//А=Спр.Товар; - Get
			//Спр.Товар=0; - Set
			//Спр.Товар.Код=0;  - Get,Set
			byteRaw_t code;
			AddLineInfo(code);
			if (IsNextDelimeter('=') && nPrevSet == 1) {
				GETDelimeter('='); 	nIsSet = 0;
				code.m_nOper = OPER_SET_A;
				code.m_param1 = variable;//переменная у которой вызывается атрибут
				code.m_param2.m_nIndex = nNumber;//номер вызываемого метода из списка встретившихся атрибутов и методов
				code.m_param3 = GetExpression();
				m_cByteCode.m_aCodeList.push_back(code);
				return variable;
			}
			else {
				code.m_nOper = OPER_GET_A;
				code.m_param2 = variable;//переменная у которой вызывается атрибут
				code.m_param3.m_nIndex = nNumber;//номер вызываемого атрибута из списка встретившихся атрибутов и методов
				variable = GetVariable();
				code.m_param1 = variable;//переменная, в которую возвращается значение
				m_cByteCode.m_aCodeList.push_back(code);
			}
		}
		goto MLabel;
	}
	return variable;
}

bool CCompileModule::CompileIf()
{
	std::vector <int>aAddrLine;

	GETKeyWord(KEY_IF);

	param_t sParam;
	byteRaw_t code;
	AddLineInfo(code);
	code.m_nOper = OPER_IF;

	sParam = GetExpression();
	code.m_param1 = sParam;
	CorrectTypeDef(sParam);//проверка типа значения

	m_cByteCode.m_aCodeList.push_back(code);

	int nLastIFLine = m_cByteCode.m_aCodeList.size() - 1;

	GETKeyWord(KEY_THEN);
	CompileBlock();

	while (IsNextKeyWord(KEY_ELSEIF)) {
		//Записываем выход из всех проверок для предыдущего блока
		code.m_nOper = OPER_GOTO;
		m_cByteCode.m_aCodeList.push_back(code);
		aAddrLine.push_back(m_cByteCode.m_aCodeList.size() - 1);//параметр для оператора GOTO будет известен потом

		//для предыдущего условия устанавливаем адрес прехода при несовпадении условия
		m_cByteCode.m_aCodeList[nLastIFLine].m_param2.m_nIndex = m_cByteCode.m_aCodeList.size();

		GETKeyWord(KEY_ELSEIF);
		AddLineInfo(code);
		code.m_nOper = OPER_IF;

		sParam = GetExpression();
		code.m_param1 = sParam;
		CorrectTypeDef(sParam);//проверка типа значения

		m_cByteCode.m_aCodeList.push_back(code);
		nLastIFLine = m_cByteCode.m_aCodeList.size() - 1;

		GETKeyWord(KEY_THEN);
		CompileBlock();
	}

	if (IsNextKeyWord(KEY_ELSE)) {
		//Записываем выход из всех проверок для предыдущего блока
		AddLineInfo(code);
		code.m_nOper = OPER_GOTO;
		m_cByteCode.m_aCodeList.push_back(code);
		aAddrLine.push_back(m_cByteCode.m_aCodeList.size() - 1);//параметр для оператора GOTO будет известен потом

		//для предыдущего условия устанавливаем адрес прехода при несовпадении условия
		m_cByteCode.m_aCodeList[nLastIFLine].m_param2.m_nIndex = m_cByteCode.m_aCodeList.size();
		nLastIFLine = 0;

		GETKeyWord(KEY_ELSE);
		CompileBlock();
	}

	GETKeyWord(KEY_ENDIF);

	int nCurCompile = m_cByteCode.m_aCodeList.size();

	//для последнего условия устанавливаем адрес прехода при несовпадении условия
	m_cByteCode.m_aCodeList[nLastIFLine].m_param2.m_nIndex = nCurCompile;

	//Устанавливаем параметр для оператора GOTO - выход из всех локальных условий
	for (unsigned int i = 0; i < aAddrLine.size(); i++) {
		m_cByteCode.m_aCodeList[aAddrLine[i]].m_param1.m_nIndex = nCurCompile;
	}

	return true;
}

bool CCompileModule::CompileWhile()
{
	m_pContext->StartDoList();

	int nStartWhile = m_cByteCode.m_aCodeList.size();

	GETKeyWord(KEY_WHILE);
	byteRaw_t code;
	AddLineInfo(code);
	code.m_nOper = OPER_IF;

	param_t sParam = GetExpression();
	code.m_param1 = sParam;
	CorrectTypeDef(sParam);//проверка типа значения

	int nEndWhile = m_cByteCode.m_aCodeList.size();

	m_cByteCode.m_aCodeList.push_back(code);

	GETKeyWord(KEY_DO);
	CompileBlock();
	GETKeyWord(KEY_ENDDO);

	byteRaw_t code2;
	AddLineInfo(code2);
	code2.m_nOper = OPER_GOTO;
	code2.m_param1.m_nIndex = nStartWhile;
	m_cByteCode.m_aCodeList.push_back(code2);

	m_cByteCode.m_aCodeList[nEndWhile].m_param2.m_nIndex = m_cByteCode.m_aCodeList.size();

	//запоминаем адреса переходов для команд Continue и Break
	m_pContext->FinishDoList(m_cByteCode, m_cByteCode.m_aCodeList.size() - 1, m_cByteCode.m_aCodeList.size());

	return true;
}

bool CCompileModule::CompileFor()
{
	m_pContext->StartDoList();

	GETKeyWord(KEY_FOR);

	wxString realName = GETIdentifier(true);
	wxString name = StringUtils::MakeUpper(realName);

	param_t variable = GetVariable(realName);

	//проверка типа переменной
	if (!variable.m_sType.IsEmpty()) {
		if (!CValue::CompareObjectName(variable.m_sType, eValueTypes::TYPE_NUMBER)) {
			SetError(ERROR_NUMBER_TYPE);
		}
	}

	GETDelimeter('=');
	param_t Variable2 = GetExpression();

	byteRaw_t code0;
	AddLineInfo(code0);
	code0.m_nOper = OPER_LET;
	code0.m_param1 = variable;
	code0.m_param2 = Variable2;
	m_cByteCode.m_aCodeList.push_back(code0);

	//проверка типа значения
	if (!variable.m_sType.IsEmpty())
	{
		if (!CValue::CompareObjectName(Variable2.m_sType, eValueTypes::TYPE_NUMBER)) {
			SetError(ERROR_BAD_TYPE_EXPRESSION);
		}
	}

	GETKeyWord(KEY_TO);
	param_t VariableTo =
		m_pContext->GetVariable(name + wxT("@to"), true, false, false, true); //loop variable

	byteRaw_t code1;
	AddLineInfo(code1);
	code1.m_nOper = OPER_LET;
	code1.m_param1 = VariableTo;
	code1.m_param2 = GetExpression();
	m_cByteCode.m_aCodeList.push_back(code1);

	byteRaw_t code;
	AddLineInfo(code);
	code.m_nOper = OPER_FOR;
	code.m_param1 = variable;
	code.m_param2 = VariableTo;
	m_cByteCode.m_aCodeList.push_back(code);

	int nStartFOR = m_cByteCode.m_aCodeList.size() - 1;

	GETKeyWord(KEY_DO);
	CompileBlock();
	GETKeyWord(KEY_ENDDO);

	byteRaw_t code2;
	AddLineInfo(code2);
	code2.m_nOper = OPER_NEXT;
	code2.m_param1 = variable;
	code2.m_param2.m_nIndex = nStartFOR;
	m_cByteCode.m_aCodeList.push_back(code2);

	m_cByteCode.m_aCodeList[nStartFOR].m_param3.m_nIndex = m_cByteCode.m_aCodeList.size();

	//запоминаем адреса переходов для команд Continue и Break
	m_pContext->FinishDoList(m_cByteCode, m_cByteCode.m_aCodeList.size() - 1, m_cByteCode.m_aCodeList.size());

	return true;
}

bool CCompileModule::CompileForeach()
{
	m_pContext->StartDoList();

	GETKeyWord(KEY_FOREACH);

	wxString realName = GETIdentifier(true);
	wxString name = StringUtils::MakeUpper(realName);

	param_t variable = GetVariable(realName);

	GETKeyWord(KEY_IN);

	param_t VariableIn =
		m_pContext->GetVariable(name + wxT("@in"), true, false, false, true); //loop variable

	byteRaw_t code1;
	AddLineInfo(code1);
	code1.m_nOper = OPER_LET;
	code1.m_param1 = VariableIn;
	code1.m_param2 = GetExpression();
	m_cByteCode.m_aCodeList.push_back(code1);

	param_t VariableIt =
		m_pContext->GetVariable(name + wxT("@it"), true, false, false, true);  //storage iterpos;

	byteRaw_t code;
	AddLineInfo(code);
	code.m_nOper = OPER_FOREACH;
	code.m_param1 = variable;
	code.m_param2 = VariableIn;
	code.m_param3 = VariableIt; // for storage iterpos;
	m_cByteCode.m_aCodeList.push_back(code);

	int nStartFOREACH = m_cByteCode.m_aCodeList.size() - 1;

	GETKeyWord(KEY_DO);
	CompileBlock();
	GETKeyWord(KEY_ENDDO);

	byteRaw_t code2;
	AddLineInfo(code2);
	code2.m_nOper = OPER_NEXT_ITER;
	code2.m_param1 = VariableIt; // for storage iterpos;
	code2.m_param2.m_nIndex = nStartFOREACH;
	m_cByteCode.m_aCodeList.push_back(code2);

	m_cByteCode.m_aCodeList[nStartFOREACH].m_param4.m_nIndex = m_cByteCode.m_aCodeList.size();

	//запоминаем адреса переходов для команд Continue и Break
	m_pContext->FinishDoList(m_cByteCode, m_cByteCode.m_aCodeList.size() - 1, m_cByteCode.m_aCodeList.size());

	return true;
}

/**
 * обработка вызова функции или процедуры
 */
param_t CCompileModule::GetCallFunction(const wxString& realName)
{
	wxString name = StringUtils::MakeUpper(realName);
	сallFunction_t* callFunc = new сallFunction_t;

	function_t* foundedFunction = NULL;
	if (m_bExpressionOnly)
		foundedFunction = GetFunction(name);
	else if (!GetContext()->FindFunction(name, foundedFunction))
		SetError(ERROR_CALL_FUNCTION, realName);

	callFunc->m_sName = name;
	callFunc->m_sRealName = realName;
	callFunc->m_nError = m_nCurrentCompile;//для выдачи сообщений при ошибках
	GETDelimeter('(');
	while (!IsNextDelimeter(')')) {
		if (IsNextDelimeter(',')) {
			param_t data;
			data.m_nArray = DEF_VAR_SKIP;//пропущенный параметр
			data.m_nIndex = DEF_VAR_SKIP;
			callFunc->m_aParamList.push_back(data);
		}
		else {
			callFunc->m_aParamList.emplace_back(GetExpression());
			if (IsNextDelimeter(')'))
				break;
		}
		GETDelimeter(',');
	}

	GETDelimeter(')');
	param_t variable = GetVariable();

	byteRaw_t code;
	AddLineInfo(code);

	callFunc->m_nNumberString = code.m_nNumberString;
	callFunc->m_nNumberLine = code.m_nNumberLine;
	callFunc->m_sModuleName = code.m_sModuleName;
	callFunc->m_sRetValue = variable;

	if (foundedFunction && GetContext()->m_sCurFuncName != name) {
		AddCallFunction(callFunc);
		wxDELETE(callFunc);
	}
	else {
		if (m_bExpressionOnly)
			SetError(ERROR_CALL_FUNCTION, realName);
		code.m_nOper = OPER_GOTO;//переход в конец байт-кода, где будет производиться развернутый вызов
		m_cByteCode.m_aCodeList.push_back(code);
		callFunc->m_nAddLine = m_cByteCode.m_aCodeList.size() - 1;
		m_apCallFunctions.push_back(callFunc);
	}

	return variable;
}

/**
 * Получает номер константы из уникального списка значений
 * (если такого значения в списке нет, то оно создается)
 */
param_t CCompileModule::FindConst(CValue& m_vData)
{
	param_t Const; Const.m_nArray = DEF_VAR_CONST;

	wxString sConst = wxString::Format(wxT("%d:%s"), m_vData.GetType(), m_vData.GetString());

	if (m_aHashConstList[sConst]) {
		Const.m_nIndex = m_aHashConstList[sConst] - 1;
	}
	else {
		Const.m_nIndex = m_cByteCode.m_aConstList.size();
		m_cByteCode.m_aConstList.push_back(m_vData);
		m_aHashConstList[sConst] = Const.m_nIndex + 1;
	}

	Const.m_sType = GetTypeVar(m_vData.GetTypeString());
	return Const;
}

#define SetOper(x)	code.m_nOper=x;

/**
 * Компиляция произвольного выражения (служебные вызовы из самой функции)
 */
param_t CCompileModule::GetExpression(int nPriority)
{
	param_t variable;
	lexem_t lex = GETLexem();

	//Сначала обрабатываем Левые операторы
	if ((lex.m_nType == KEYWORD && lex.m_nData == KEY_NOT) || (lex.m_nType == DELIMITER && lex.m_nData == '!'))
	{
		variable = GetVariable();
		param_t Variable2 = GetExpression(s_aPriority['!']);
		byteRaw_t code;
		code.m_nOper = OPER_NOT;
		AddLineInfo(code);

		if (!Variable2.m_sType.IsEmpty()) {
			CheckTypeDef(Variable2, CValue::GetNameObjectFromVT(eValueTypes::TYPE_BOOLEAN));
		}
		
		variable.m_sType = CValue::GetNameObjectFromVT(eValueTypes::TYPE_BOOLEAN, true);

		code.m_param1 = variable;
		code.m_param2 = Variable2;
		m_cByteCode.m_aCodeList.push_back(code);
	}
	else if ((lex.m_nType == KEYWORD && lex.m_nData == KEY_NEW))
	{
		wxString sObjectName = GETIdentifier(true);
		int nNumber = GetConstString(sObjectName);

		std::vector <param_t> aParamList;

		if (IsNextDelimeter('('))//это вызов метода
		{
			GETDelimeter('(');

			while (!IsNextDelimeter(')'))
			{
				if (IsNextDelimeter(','))
				{
					param_t data;
					data.m_nArray = DEF_VAR_SKIP;//пропущенный параметр
					data.m_nIndex = DEF_VAR_SKIP;
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

		if (lex.m_nData == KEY_NEW && !CValue::IsRegisterObject(sObjectName, eObjectType::eObjectType_object))
			SetError(ERROR_CALL_CONSTRUCTOR, sObjectName);

		byteRaw_t code;
		AddLineInfo(code);
		code.m_nOper = OPER_NEW;

		code.m_param2.m_nIndex = nNumber;//номер вызываемого метода из списка встретившихся методов
		code.m_param2.m_nArray = aParamList.size();//число параметров

		variable = GetVariable();
		code.m_param1 = variable;//переменная, в которую возвращается значение
		m_cByteCode.m_aCodeList.push_back(code);

		for (unsigned int i = 0; i < aParamList.size(); i++)
		{
			byteRaw_t code;
			AddLineInfo(code);
			code.m_nOper = OPER_SET;
			code.m_param1 = aParamList[i];
			m_cByteCode.m_aCodeList.push_back(code);
		}
	}
	else if (lex.m_nType == DELIMITER && lex.m_nData == '(')
	{
		variable = GetExpression();
		GETDelimeter(')');
	}
	else if (lex.m_nType == DELIMITER && lex.m_nData == '?')
	{
		variable = GetVariable();
		byteRaw_t code;
		AddLineInfo(code);
		code.m_nOper = OPER_ITER;
		code.m_param1 = variable;
		GETDelimeter('(');
		code.m_param2 = GetExpression();
		GETDelimeter(',');
		code.m_param3 = GetExpression();
		GETDelimeter(',');
		code.m_param4 = GetExpression();
		GETDelimeter(')');
		m_cByteCode.m_aCodeList.push_back(code);
	}
	else if (lex.m_nType == IDENTIFIER)
	{
		m_nCurrentCompile--;//шаг назад
		int nSet = 0;
		variable = GetCurrentIdentifier(nSet);
	}
	else if (lex.m_nType == CONSTANT)
	{
		variable = FindConst(lex.m_vData);
	}
	else if ((lex.m_nType == DELIMITER && lex.m_nData == '+') || (lex.m_nType == DELIMITER && lex.m_nData == '-'))
	{
		//проверяем допустимость такого задания
		int nCurPriority = s_aPriority[lex.m_nData];

		if (nPriority >= nCurPriority) SetError(ERROR_EXPRESSION);//сравниваем приоритеты левой (предыдущей операции) и текущей выполняемой операции

		//Это задание пользователем знака выражения
		if (lex.m_nData == '+')//ничего не делаем (игнорируем)
		{
			byteRaw_t code;
			variable = GetExpression(nPriority);
			if (!variable.m_sType.IsEmpty()) { 
				CheckTypeDef(variable, CValue::GetNameObjectFromVT(eValueTypes::TYPE_NUMBER)); 
			}
			variable.m_sType = CValue::GetNameObjectFromVT(eValueTypes::TYPE_NUMBER, true);
			return variable;
		}
		else
		{
			variable = GetExpression(100);//сверх высокий приоритет!
			byteRaw_t code;
			AddLineInfo(code);
			code.m_nOper = OPER_INVERT;

			if (!variable.m_sType.IsEmpty()) 
				CheckTypeDef(variable, CValue::GetNameObjectFromVT(eValueTypes::TYPE_NUMBER));

			code.m_param2 = variable;
			variable = GetVariable();
			variable.m_sType = CValue::GetNameObjectFromVT(eValueTypes::TYPE_NUMBER, true);
			code.m_param1 = variable;
			m_cByteCode.m_aCodeList.push_back(code);
		}
	}
	else
	{
		SetError(ERROR_EXPRESSION);
	}

	//Теперь обрабатываем Правые операторы
	//итак в variable имеем первый индекс переменной выражения

MOperation:

	lex = PreviewGetLexem();

	if (lex.m_nType == DELIMITER && lex.m_nData == ')')
		return variable;

	//смотрим есть ли далее операторы выполнения действий над данной переменной
	if ((lex.m_nType == DELIMITER && lex.m_nData != ';') || (lex.m_nType == KEYWORD && lex.m_nData == KEY_AND) || (lex.m_nType == KEYWORD && lex.m_nData == KEY_OR))
	{
		if (lex.m_nData >= 0 && lex.m_nData <= 255)
		{
			int nCurPriority = s_aPriority[lex.m_nData];
			if (nPriority < nCurPriority)//сравниваем приоритеты левой (предыдущей операции) и текущей выполняемой операции
			{
				byteRaw_t code;
				AddLineInfo(code);
				lex = GetLexem();

				if (lex.m_nData == '*') {
					SetOper(OPER_MULT);
				}
				else if (lex.m_nData == '/') {
					SetOper(OPER_DIV);
				}
				else if (lex.m_nData == '+') {
					SetOper(OPER_ADD);
				}
				else if (lex.m_nData == '-') {
					SetOper(OPER_SUB);
				}
				else if (lex.m_nData == '%') {
					SetOper(OPER_MOD);
				}
				else if (lex.m_nData == KEY_AND) {
					SetOper(OPER_AND);
				}
				else if (lex.m_nData == KEY_OR) {
					SetOper(OPER_OR);
				}
				else if (lex.m_nData == '>')
				{
					SetOper(OPER_GT);
					if (IsNextDelimeter('=')) {
						GETDelimeter('=');
						SetOper(OPER_GE);
					}
				}
				else if (lex.m_nData == '<')
				{
					SetOper(OPER_LS);
					if (IsNextDelimeter('=')) {
						GETDelimeter('=');
						SetOper(OPER_LE);
					}
					else if (IsNextDelimeter('>')) {
						GETDelimeter('>');
						SetOper(OPER_NE);
					}
				}
				else if (lex.m_nData == '=') {
					SetOper(OPER_EQ);
				}
				else {
					SetError(ERROR_EXPRESSION);
				}

				param_t Variable1 = GetVariable();
				param_t Variable2 = variable;
				param_t Variable3 = GetExpression(nCurPriority);

				if (DEF_VAR_TEMP != Variable3.m_nArray && DEF_VAR_CONST != Variable3.m_nArray) { //доп. проверка на запрещенные операции
					if (CValue::CompareObjectName(Variable2.m_sType, eValueTypes::TYPE_STRING)) {
						if (OPER_DIV == code.m_nOper
							|| OPER_MOD == code.m_nOper
							|| OPER_MULT == code.m_nOper
							|| OPER_AND == code.m_nOper
							|| OPER_OR == code.m_nOper) {
							SetError(ERROR_TYPE_OPERATION);
						}
					}
				}

				if (DEF_VAR_CONST != Variable2.m_nArray && DEF_VAR_TEMP != Variable2.m_nArray) { //константы не проверяем  - т.к. они типизированы по дефолту
					CheckTypeDef(Variable3, Variable2.m_sType);
				}

				Variable1.m_sType = Variable2.m_sType;

				if (code.m_nOper >= OPER_GT && code.m_nOper <= OPER_NE) {
					Variable1.m_sType = CValue::GetNameObjectFromVT(eValueTypes::TYPE_BOOLEAN, true);
				}

				code.m_param1 = Variable1;
				code.m_param2 = Variable2;
				code.m_param3 = Variable3;

				m_cByteCode.m_aCodeList.push_back(code);

				variable = Variable1;
				goto MOperation;
			}
		}
	}

	return variable;
}

void CCompileModule::SetParent(CCompileModule* setParent)
{
	m_cByteCode.m_parent = NULL;

	m_parent = setParent;
	m_cContext.m_parentContext = NULL;

	if (m_parent != NULL) {
		m_cByteCode.m_parent = &m_parent->m_cByteCode;
		m_cContext.m_parentContext = &m_parent->m_cContext;
	}

	OnSetParent(setParent);
}

param_t CCompileModule::AddVariable(const wxString& name, const wxString& typeVar, bool exportVar, bool contextVar, bool tempVar)
{
	return m_pContext->AddVariable(name, typeVar, exportVar, contextVar, tempVar);
}

/**
 * Функция возвращает номер переменной по строковому имени
 */
param_t CCompileModule::GetVariable(const wxString& name, bool bCheckError, bool bLoadFromContext)
{
	return m_pContext->GetVariable(name, true, bCheckError, bLoadFromContext);
}

/**
 * Cоздаем новый идентификатор переменной
 */
param_t CCompileModule::GetVariable()
{
	wxString name = wxString::Format(wxT("@%d"), m_pContext->m_nTempVar);//@ - для гарантии уникальности имени
	param_t variable = m_pContext->GetVariable(name, false, false, false, true);//временную переменную ищем только в локальном контексте
	variable.m_nArray = DEF_VAR_TEMP;//признак временной локальной переменной
	m_pContext->m_nTempVar++;
	return variable;
}

#pragma warning(pop)