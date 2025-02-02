////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, 2С-team
//	Description : compile module 
////////////////////////////////////////////////////////////////////////////

#include "compileCode.h"
#include "codeDef.h"

#include "systemManager/systemManager.h"

#pragma warning(push)
#pragma warning(disable : 4018)

//////////////////////////////////////////////////////////////////////
//                           Constants
//////////////////////////////////////////////////////////////////////

std::map<wxString, void*> s_aHelpDescription;//описание ключевых слов и системных функций
std::map<wxString, void*> s_aHashKeywordList;

//Массив приоритетов математических операций
static std::array<int, 256> s_aPriority = { 0 };

//////////////////////////////////////////////////////////////////////
// Construction/Destruction CCompileCode
//////////////////////////////////////////////////////////////////////

CCompileCode::CCompileCode() :
	CTranslateCode(),
	m_pContext(GetContext()),
	m_parent(nullptr),
	m_bExpressionOnly(false), m_changeCode(false),
	m_onlyFunction(false)
{
	InitializeCompileModule();
	//у родительских контекстов локальные переменные не ищем!
	m_cContext.m_nFindLocalInParent = 0;
}

CCompileCode::CCompileCode(const wxString& strModuleName, const wxString& strDocPath, bool onlyFunction) :
	CTranslateCode(strModuleName, strDocPath),
	m_pContext(GetContext()),
	m_parent(nullptr),
	m_bExpressionOnly(false), m_changeCode(false),
	m_onlyFunction(onlyFunction)
{
	InitializeCompileModule();
	//у родительских контекстов локальные переменные не ищем!
	m_cContext.m_nFindLocalInParent = 0;
}

CCompileCode::CCompileCode(const wxString& strFileName) :
	CTranslateCode(strFileName),
	m_pContext(GetContext()),
	m_parent(nullptr),
	m_bExpressionOnly(false), m_changeCode(false),
	m_onlyFunction(false)
{
	InitializeCompileModule();
	//у родительских контекстов локальные переменные не ищем!
	m_cContext.m_nFindLocalInParent = 0;
}

CCompileCode::~CCompileCode()
{
	Reset();

	m_aExternValues.clear();
	m_aContextValues.clear();
}

void CCompileCode::InitializeCompileModule()
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

void CCompileCode::Reset()
{
	m_pContext = nullptr;

	m_aHashConstList.clear();
	m_cByteCode.Reset();

	m_cContext.m_nDoNumber = 0;
	m_cContext.m_nReturn = 0;
	m_cContext.m_nTempVar = 0;
	m_cContext.m_nFindLocalInParent = 1;

	m_cContext.aContinueList.clear();
	m_cContext.aBreakList.clear();

	m_cContext.m_cLabels.clear();
	m_cContext.m_cLabelsDef.clear();

	m_cContext.m_strCurFuncName = "";

	//clear functions & variables 
	for (auto& function : m_cContext.m_cFunctions) {
		wxDELETE(function.second);
	}

	m_cContext.m_cVariables.clear();
	m_cContext.m_cFunctions.clear();

	for (auto& function : m_apCallFunctions) {
		wxDELETE(function);
	}

	m_apCallFunctions.clear();
}

void CCompileCode::PrepareModuleData()
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
		contextValue->PrepareNames();

		//добавляем переменные из контекста
		for (unsigned int i = 0; i < contextValue->GetNProps(); i++) {
			const wxString& strPropName = contextValue->GetPropName(i);
			//определяем номер и тип переменной
			CVariable cVariables(strPropName);
			cVariables.m_strContextVar = pair.first;
			cVariables.m_bContext = true;
			cVariables.m_bExport = true;
			cVariables.m_nNumber = i;

			GetContext()->m_cVariables.insert_or_assign(
				stringUtils::MakeUpper(strPropName), cVariables
			);
		}

		//добавляем методы из контекста
		for (long i = 0; i < contextValue->GetNMethods(); i++) {

			const wxString& strMethodName = contextValue->GetMethodName(i);

			CCompileContext* compileContext = new CCompileContext(GetContext());
			compileContext->SetModule(this);

			if (contextValue->HasRetVal(i))
				compileContext->m_nReturn = RETURN_FUNCTION;
			else
				compileContext->m_nReturn = RETURN_PROCEDURE;

			//определяем номер и тип функции
			CFunction* pFunction = new CFunction(strMethodName, compileContext);
			pFunction->m_nStart = i;
			pFunction->m_bContext = true;
			pFunction->m_bExport = true;
			pFunction->m_strContextVar = pair.first;

			CParamVariable contextVariable;
			contextVariable.m_vData.m_nArray = DEF_VAR_DEFAULT;
			contextVariable.m_vData.m_nIndex = DEF_VAR_DEFAULT;
			for (int n = 0; n < contextValue->GetNParams(i); n++) {
				pFunction->m_aParamList.push_back(contextVariable);
			}

			pFunction->m_strRealName = strMethodName;
			pFunction->m_strShortDescription = contextValue->GetMethodHelper(i);

			//проверка на типизированность
			GetContext()->m_cFunctions.insert_or_assign(
				stringUtils::MakeUpper(strMethodName), pFunction
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
void CCompileCode::SetError(int codeError, const wxString& errorDesc)
{
	wxString strFileName, strModuleName, strDocPath; int currPos = 0, currLine = 0;

	if (m_nCurrentCompile >= m_listLexem.size()) {
		m_nCurrentCompile = m_listLexem.size() - 1;
	}

	if (m_nCurrentCompile >= 0 && m_nCurrentCompile < m_listLexem.size()) {
		
		const lexem_t& lex = m_listLexem.at(m_nCurrentCompile);
	
		strFileName = lex.m_strFileName;
		strModuleName = lex.m_strModuleName;
		strDocPath = lex.m_strDocPath;
		currPos = lex.m_nNumberString;
		currLine = lex.m_nNumberLine;
	}

	CTranslateCode::SetError(codeError,
		strFileName, strModuleName, strDocPath,
		currPos, currLine,
		errorDesc);
}

/**
 * другой вариант функции
 */
void CCompileCode::SetError(int nErr, const wxUniChar& c)
{
	SetError(nErr, wxString::Format(wxT("%c"), c));
}

/**
 * ProcessError
 * Назначение:
 * Запомнить ошибку трансляции и вызвать исключение
 * Возвращаемое значение:
 * Метод не возвращает управление!strCurWord
 */

void CCompileCode::ProcessError(const wxString& strFileName,
	const wxString& strModuleName, const wxString& strDocPath,
	unsigned int currPos, unsigned int currLine,
	const wxString& strCodeLineError, int codeError, const wxString& strErrorDesc) const
{
	CBackendException::ProcessError(
		strFileName,
		strModuleName, strDocPath,
		currPos, currLine,
		strCodeLineError, codeError, strErrorDesc
	);
}

//////////////////////////////////////////////////////////////////////
// Compiling
//////////////////////////////////////////////////////////////////////

/**
 *добавление в байт код информации о текущей строке
 */
void CCompileCode::AddLineInfo(CByteUnit& code)
{
	code.m_strModuleName = m_strModuleName;
	code.m_strDocPath = m_strDocPath;
	code.m_strFileName = m_strFileName;

	if (m_nCurrentCompile >= 0) {
		if (m_nCurrentCompile < m_listLexem.size()) {
			if (m_listLexem[m_nCurrentCompile].m_nType != ENDPROGRAM) {
				code.m_strModuleName = m_listLexem[m_nCurrentCompile].m_strModuleName;
				code.m_strDocPath = m_listLexem[m_nCurrentCompile].m_strDocPath;
				code.m_strFileName = m_listLexem[m_nCurrentCompile].m_strFileName;
			}
			code.m_nNumberString = m_listLexem[m_nCurrentCompile].m_nNumberString;
			code.m_nNumberLine = m_listLexem[m_nCurrentCompile].m_nNumberLine;
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
lexem_t CCompileCode::GetLexem()
{
	lexem_t lex;
	if (m_nCurrentCompile + 1 < m_listLexem.size()) {
		lex = m_listLexem[++m_nCurrentCompile];
	}
	return lex;
}

//Получить следующую лексему из списка байт кода без увеличения счетчика текущей позиции
lexem_t CCompileCode::PreviewGetLexem()
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
lexem_t CCompileCode::GETLexem()
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
void CCompileCode::GETDelimeter(const wxUniChar& c)
{
	lexem_t lex = GETLexem();
	if (!(lex.m_nType == DELIMITER && c == lex.m_nData)) {
		m_nCurrentCompile--;
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
bool CCompileCode::IsKeyWord(int k)
{
	if (m_nCurrentCompile + 1 < m_listLexem.size()) {
		const lexem_t& lex = m_listLexem[m_nCurrentCompile];
		if (lex.m_nType == KEYWORD && lex.m_nData == k)
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
bool CCompileCode::IsNextKeyWord(int k)
{
	if (m_nCurrentCompile + 1 < m_listLexem.size()) {
		const lexem_t& lex = m_listLexem[m_nCurrentCompile + 1];
		if (lex.m_nType == KEYWORD && k == lex.m_nData)
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
bool CCompileCode::IsDelimeter(const wxUniChar& c)
{
	if (m_nCurrentCompile + 1 < m_listLexem.size()) {
		const lexem_t& lex = m_listLexem[m_nCurrentCompile];
		if (lex.m_nType == DELIMITER && c == lex.m_nData)
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
bool CCompileCode::IsNextDelimeter(const wxUniChar& c)
{
	if (m_nCurrentCompile + 1 < m_listLexem.size()) {
		const lexem_t& lex = m_listLexem[m_nCurrentCompile + 1];
		if (lex.m_nType == DELIMITER && c == lex.m_nData)
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
void CCompileCode::GETKeyWord(int nKey)
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
wxString CCompileCode::GETIdentifier(bool strRealName)
{
	const lexem_t& lex = GETLexem();
	if (lex.m_nType != IDENTIFIER) {
		if (strRealName && lex.m_nType == KEYWORD) {
			return lex.m_strData;
		}
		SetError(ERROR_IDENTIFIER_DEFINE);
	}

	if (strRealName) {
		return lex.m_vData.m_sData;
	}

	return lex.m_strData;
}

/**
 * GETConstant
 * Получить следующую лексему как константу
 * Возвращаемое значение:
 * константа
 */
CValue CCompileCode::GETConstant()
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
int CCompileCode::GetConstString(const wxString& strMethodName)
{
	auto& it = std::find_if(m_aHashConstList.begin(), m_aHashConstList.end(),
		[strMethodName](std::pair <const wxString, unsigned int>& pair) {
			return stringUtils::CompareString(strMethodName, pair.first);
		}
	);

	if (it != m_aHashConstList.end())
		return it->second - 1;

	m_cByteCode.m_aConstList.push_back(strMethodName);
	m_aHashConstList.insert_or_assign(strMethodName, m_cByteCode.m_aConstList.size());

	return m_cByteCode.m_aConstList.size() - 1;
}

/**
 * AddVariable
 * Назначение:
 * Добавить имя и адрес внешней переменной в специальный массив для дальнейшего использования
 */
void CCompileCode::AddVariable(const wxString& strVarName, const CValue& vObject)
{
	if (strVarName.IsEmpty())
		return;

	//учитываем внешние переменные при компиляции
	m_aExternValues[strVarName.Upper()] = vObject.m_typeClass == eValueTypes::TYPE_REFFER
		? vObject.GetRef() : const_cast<CValue*>(&vObject);

	//ставим флаг для перекомпиляции
	m_changeCode = true;
}

/**
 * AddVariable
 * Назначение:
 * Добавить имя и адрес внешней перменной в специальный массив для дальнейшего использования
 */
void CCompileCode::AddVariable(const wxString& strVarName, CValue* pValue)
{
	if (strVarName.IsEmpty())
		return;

	//учитываем внешние переменные при компиляции
	m_aExternValues[strVarName.Upper()] = pValue;

	//ставим флаг для перекомпиляции
	m_changeCode = true;
}

/**
 * AddContextVariable
 * Назначение:
 * Добавить имя и адрес внешней перменной в специальный массив для дальнейшего использования
 */
void CCompileCode::AddContextVariable(const wxString& strVarName, const CValue& vObject)
{
	if (strVarName.IsEmpty())
		return;

	//добавляем переменные из контекста
	m_aContextValues[strVarName.Upper()] = vObject.m_typeClass == eValueTypes::TYPE_REFFER ? vObject.GetRef() : const_cast<CValue*>(&vObject);

	//ставим флаг для перекомпиляции
	m_changeCode = true;
}

/**
 * AddContextVariable
 * Назначение:
 * Добавить имя и адрес внешней перменной в специальный массив для дальнейшего использования
 */
void CCompileCode::AddContextVariable(const wxString& strVarName, CValue* pValue)
{
	if (strVarName.IsEmpty())
		return;

	//добавляем переменные из контекста
	m_aContextValues[strVarName.Upper()] = pValue;

	//ставим флаг для перекомпиляции
	m_changeCode = true;
}

/**
 * RemoveVariable
 * Назначение:
 * Удалить имя и адрес внешней перменной
 */
void CCompileCode::RemoveVariable(const wxString& strVarName)
{
	if (strVarName.IsEmpty())
		return;

	m_aExternValues.erase(strVarName.Upper());
	m_aContextValues.erase(strVarName.Upper());

	//ставим флаг для перекомпиляции
	m_changeCode = true;
}

/**
 * Compile
 * Назначение:
 * Трасляция и компиляция исходного кода в байт-код (объектный код)
 * Возвращаемое значение:
 * true,false
 */
bool CCompileCode::Compile(const wxString& strCode)
{
	//clear functions & variables
	Reset();

	//контекст самого модуля
	m_pContext = GetContext();

	Load(strCode);

	//prepare lexem
	if (!PrepareLexem()) {
		return false;
	}

	//подготовить контекстные переменные
	PrepareModuleData();

	//Компиляция 
	if (CompileModule()) {
		m_changeCode = false;
		return true;
	}

	return false;
}

bool CCompileCode::IsTypeVar(const wxString& typeVar)
{
	if (!typeVar.IsEmpty()) {
		if (CValue::IsRegisterCtor(typeVar, eCtorObjectType::eCtorObjectType_object_primitive))
			return true;
	}
	else {
		const lexem_t& lex = PreviewGetLexem();
		if (CValue::IsRegisterCtor(lex.m_strData, eCtorObjectType::eCtorObjectType_object_primitive))
			return true;
	}

	return false;
}

wxString CCompileCode::GetTypeVar(const wxString& strType)
{
	if (!strType.IsEmpty()) {
		if (!CValue::IsRegisterCtor(strType, eCtorObjectType::eCtorObjectType_object_primitive))
			SetError(ERROR_TYPE_DEF);
		return strType.Upper();
	}
	else {
		const lexem_t& lex = GETLexem();
		if (!CValue::IsRegisterCtor(lex.m_strData, eCtorObjectType::eCtorObjectType_object_primitive))
			SetError(ERROR_TYPE_DEF);
		return lex.m_strData.Upper();
	}
}

/**
 * CompileDeclaration
 * Назначение:
 * Компиляция явного объявления переменных
 * Возвращаемое значение:
 * true,false
 */
bool CCompileCode::CompileDeclaration()
{
	const lexem_t& lex = PreviewGetLexem(); wxString strType;
	if (IDENTIFIER == lex.m_nType) {
		strType = GetTypeVar(); //типизированное задание переменных
	}
	else {
		GETKeyWord(KEY_VAR);
	}

	while (true) {
		wxString strRealName = GETIdentifier(true);
		wxString strName = stringUtils::MakeUpper(strRealName);
		int nParentNumber = 0;
		CCompileContext* pCurContext = GetContext();
		while (pCurContext) {
			nParentNumber++;
			if (nParentNumber > MAX_OBJECTS_LEVEL) {
				CSystemFunction::Message(pCurContext->m_compileModule->GetModuleName());
				if (nParentNumber > 2 * MAX_OBJECTS_LEVEL) {
					CBackendException::Error("Recursive call of modules!");
				}
			}
			CVariable* currentVariable = nullptr;
			if (pCurContext->FindVariable(strName, currentVariable)) { //нашли
				if (currentVariable->m_bExport ||
					pCurContext->m_compileModule == this) {
					SetError(ERROR_DEF_VARIABLE, strRealName);
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
		CParamUnit variable =
			m_pContext->AddVariable(strRealName, strType, bExport);

		if (nArrayCount >= 0) {//записываем информацию о массивах
			CByteUnit code;
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

			CByteUnit code;
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
bool CCompileCode::CompileModule()
{
	lexem_t lex;

	//устанавливаем курсор на начало массива лексем
	m_nCurrentCompile = -1;
	m_pContext = GetContext();//контекст самого модуля
	while ((lex = PreviewGetLexem()).m_nType != ERRORTYPE) {
		if ((KEYWORD == lex.m_nType && lex.m_nData == KEY_VAR) || (IDENTIFIER == lex.m_nType && IsTypeVar(lex.m_strData))) {
			if (!m_onlyFunction) {
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
	CByteUnit code;
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
			CByteUnit code;
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

	if (m_nCurrentCompile + 1 < m_listLexem.size() - 1) {
		SetError(ERROR_END_PROGRAM);
	}

	m_cByteCode.SetModule(this);
	//компиляция завершена успешно
	m_cByteCode.m_bCompile = true;

	return true;
}

//поиск определения функции в текущем модуле и во всех родительских
CFunction* CCompileCode::GetFunction(const wxString& strName, int* pNumber)
{
	int nCanUseLocalInParent = m_cContext.m_nFindLocalInParent - 1;
	int nNumber = 0;
	//ищем в текущем модуле
	CFunction* foundedFunction = nullptr;
	if (!GetContext()->FindFunction(strName, foundedFunction)) {
		CCompileCode* pCurModule = m_parent;
		while (pCurModule != nullptr) {
			nNumber++;
			foundedFunction = pCurModule->m_pContext->m_cFunctions[strName];
			if (foundedFunction != nullptr) { //нашли
				//смотрим это экспортная функция или нет
				if (nCanUseLocalInParent > 0 ||
					foundedFunction->m_bExport)
					break;//ок
				foundedFunction = nullptr;
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
bool CCompileCode::AddCallFunction(CCallFunction* pRealCall)
{
	int nModuleNumber = 0;
	//находим определение функции
	CFunction* foundedFunction = GetFunction(pRealCall->m_strName, &nModuleNumber);
	if (foundedFunction == nullptr) {
		m_nCurrentCompile = pRealCall->m_nError;
		SetError(ERROR_CALL_FUNCTION, pRealCall->m_strRealName);//нет такой функции в модуле
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

	CByteUnit code;
	AddLineInfo(code);

	code.m_nNumberString = pRealCall->m_nNumberString;
	code.m_nNumberLine = pRealCall->m_nNumberLine;
	code.m_strModuleName = pRealCall->m_strModuleName;

	if (foundedFunction->m_bContext) {//виртуальная функция - вызов заменям на конструкцию Контекст.ИмяФункции(...)
		code.m_nOper = OPER_CALL_M;
		code.m_param1 = pRealCall->m_sRetValue;//переменная, в которую возвращается значение
		code.m_param2 = pRealCall->m_sContextVal;//переменная у которой вызывается метод
		code.m_param3.m_nIndex = GetConstString(pRealCall->m_strName);//номер вызываемого метода из списка встретившихся методов
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
		CByteUnit code;
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
bool CCompileCode::CompileFunction()
{
	//сейчас мы на уровне лексемы, где задано ключевое слово FUNCTION или PROCEDURE
	lexem_t lex;

	if (IsNextKeyWord(KEY_FUNCTION)) {
		GETKeyWord(KEY_FUNCTION);
		m_pContext = new CCompileContext(GetContext());//создаем новый контекст, в котором будем компилировать тело функции
		m_pContext->SetModule(this);
		m_pContext->m_nReturn = RETURN_FUNCTION;
	}
	else if (IsNextKeyWord(KEY_PROCEDURE)) {
		GETKeyWord(KEY_PROCEDURE);
		m_pContext = new CCompileContext(GetContext());//создаем новый контекст, в котором будем компилировать тело процедуры
		m_pContext->SetModule(this);
		m_pContext->m_nReturn = RETURN_PROCEDURE;
	}
	else {
		SetError(ERROR_FUNC_DEFINE);
	}

	//вытаскиваем текст объявления функции
	lex = PreviewGetLexem();

	wxString strShortDescription;
	int m_nNumberLine = lex.m_nNumberLine;
	int nRes = m_strBuffer.find('\n', lex.m_nNumberString);
	if (nRes >= 0) {
		//strShortDescription = m_strBuffer.Mid(lex.m_nNumberString, nRes - lex.m_nNumberString - 1);
		strShortDescription = m_strBuffer.Mid(lex.m_nNumberString, nRes - lex.m_nNumberString - 1);
		nRes = strShortDescription.find_first_of('/');
		if (nRes > 0) {
			if (strShortDescription[nRes - 1] == '/') { //итак - это комментарий
				strShortDescription = strShortDescription.Mid(nRes + 1);
			}
		}
		else {
			nRes = strShortDescription.find_first_of(')');
			strShortDescription = strShortDescription.Left(nRes + 1);
		}
	}

	//получаем имя функции
	const wxString& strFuncRealName = GETIdentifier(true);
	const wxString& strFuncName = stringUtils::MakeUpper(strFuncRealName);

	int errorPlace = m_nCurrentCompile;

	CFunction* pFunction = new CFunction(strFuncName, m_pContext);
	pFunction->m_strRealName = strFuncRealName;
	pFunction->m_strShortDescription = strShortDescription;
	pFunction->m_nNumberLine = m_nNumberLine;

	//компилируем список формальных параметров + регистрируем их как локальные
	GETDelimeter('(');
	if (!IsNextDelimeter(')')) {
		while (true) {
			wxString typeVar = wxEmptyString;
			//проверка на типизированность
			if (IsTypeVar())
				typeVar = GetTypeVar();

			CParamVariable cVariable;
			if (IsNextKeyWord(KEY_VAL)) {
				GETKeyWord(KEY_VAL);
				cVariable.m_bByRef = true;
			}
			const wxString& strRealName = GETIdentifier(true);
			cVariable.m_strName = strRealName;
			cVariable.m_strType = typeVar;

			CVariable* foundedVar = nullptr;
			//регистрируем эту переменную как локальную
			if (m_pContext->FindVariable(strRealName, foundedVar)) { //было объявление + повторное объявление = ошибка
				SetError(ERROR_IDENTIFIER_DUPLICATE, strRealName);
			}
			if (IsNextDelimeter('[')) {//это массив
				GETDelimeter('[');
				GETDelimeter(']');
			}
			else if (IsNextDelimeter('=')) {
				GETDelimeter('=');
				cVariable.m_vData = FindConst(GETConstant());
			}
			m_pContext->AddVariable(strRealName, typeVar);
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
	CCompileContext* pCurContext = GetContext();
	while (pCurContext) {
		nParentNumber++;
		if (nParentNumber > MAX_OBJECTS_LEVEL) {
			CSystemFunction::Message(pCurContext->m_compileModule->GetModuleName());
			if (nParentNumber > 2 * MAX_OBJECTS_LEVEL) {
				CBackendException::Error("Recursive call of modules!");
			}
		}
		CFunction* foundedFunc = nullptr;
		if (pCurContext->FindFunction(strFuncName, foundedFunc)) { //нашли
			if (foundedFunc->m_bExport ||
				pCurContext->m_compileModule == this) {
				m_nCurrentCompile = errorPlace;
				SetError(ERROR_DEF_FUNCTION, strFuncRealName);
			}
		}
		pCurContext = pCurContext->m_parentContext;
	}

	//проверка на типизированность
	GetContext()->m_cFunctions[strFuncName] = pFunction;

	//вставляем информацию о функции в массив байт-кодов:
	CByteUnit code0;
	AddLineInfo(code0);
	code0.m_nOper = OPER_FUNC;
#if defined(_LP64) || defined(__LP64__) || defined(__arch64__) || defined(_WIN64)
	code0.m_param1.m_nArray = reinterpret_cast<wxLongLong_t>(m_pContext);
#else
	code0.m_param1.m_nArray = reinterpret_cast<int>(m_pContext);
#endif
	m_cByteCode.m_aCodeList.push_back(code0);

	const long lAddres = pFunction->m_nStart = m_cByteCode.m_aCodeList.size() - 1;

	m_cByteCode.m_aFuncList[strFuncName] = {
		(long)pFunction->m_aParamList.size(),
		lAddres,
		m_pContext->m_nReturn == RETURN_FUNCTION
	};

	if (pFunction->m_bExport) {
		m_cByteCode.m_aExportFuncList[strFuncName] = {
			(long)pFunction->m_aParamList.size(),
			lAddres,
			m_pContext->m_nReturn == RETURN_FUNCTION
		};
	}

	for (unsigned int i = 0; i < pFunction->m_aParamList.size(); i++) {
		//add set oper
		CByteUnit code;
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
		CParamUnit variable;
		variable.m_strType = pFunction->m_aParamList[i].m_strType;
		variable.m_nArray = 0;
		variable.m_nIndex = i;//индекс совпадает с номером
		AddTypeSet(variable);
	}

	GetContext()->m_strCurFuncName = strFuncName;
	CompileBlock();
	m_pContext->DoLabels();
	GetContext()->m_strCurFuncName = wxEmptyString;

	if (m_pContext->m_nReturn == RETURN_FUNCTION) {
		GETKeyWord(KEY_ENDFUNCTION);
	}
	else {
		GETKeyWord(KEY_ENDPROCEDURE);
	}

	CByteUnit code;
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
void CCompileCode::AddTypeSet(const CParamUnit& sVariable)
{
	const wxString& typeName = sVariable.m_strType;
	if (!typeName.IsEmpty()) {
		CByteUnit code;
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
		if(var.m_strType!=type)\
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
if(!sKey.m_strType.IsEmpty())\
{\
	if (CValue::CompareObjectName(sKey.m_strType, eValueTypes::TYPE_NUMBER)) code.m_nOper+=TYPE_DELTA1;\
	else if (CValue::CompareObjectName(sKey.m_strType, eValueTypes::TYPE_STRING)) code.m_nOper+=TYPE_DELTA2;\
	else if (CValue::CompareObjectName(sKey.m_strType, eValueTypes::TYPE_DATE)) code.m_nOper+=TYPE_DELTA3;\
    else if (CValue::CompareObjectName(sKey.m_strType, eValueTypes::TYPE_BOOLEAN))  code.m_nOper+=TYPE_DELTA4;\
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
bool CCompileCode::CompileBlock()
{
	lexem_t lex;
	while ((lex = PreviewGetLexem()).m_nType != ERRORTYPE)
	{
		/*if (IDENTIFIER == lex.m_nType && IsTypeVar(lex.m_strData))
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

				CByteUnit code;
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
				CByteUnit code;
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
				CByteUnit code;
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
					CByteUnit code;
					AddLineInfo(code);
					code.m_nOper = OPER_GOTO;
					m_cByteCode.m_aCodeList.push_back(code);
					int nAddrLine = m_cByteCode.m_aCodeList.size() - 1;
					std::vector<int>* pList = m_pContext->aContinueList[m_pContext->m_nDoNumber];
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
					CByteUnit code;
					AddLineInfo(code);
					code.m_nOper = OPER_GOTO;
					m_cByteCode.m_aCodeList.push_back(code);
					int nAddrLine = m_cByteCode.m_aCodeList.size() - 1;
					std::vector<int>* pList = m_pContext->aBreakList[m_pContext->m_nDoNumber];
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
					unsigned int pLabel = m_pContext->m_cLabelsDef[lex.m_strData];
					if (pLabel > 0)
						SetError(ERROR_IDENTIFIER_DUPLICATE, lex.m_strData);//произошло дублированное определения меток
					//записываем адрес перехода:
					m_pContext->m_cLabelsDef[lex.m_strData] = m_cByteCode.m_aCodeList.size() - 1;
					GETDelimeter(':');
				}
				else { //здесь обрабатываются вызовы функций, методов, присваиваение выражений
					m_nCurrentCompile--;//шаг назад
					int nSet = 1;
					if (m_onlyFunction && m_pContext == GetContext()) SetError(ERROR_ONLY_FUNCTION);
					CParamUnit variable = GetCurrentIdentifier(nSet);//получаем левую часть выражения (до знака '=')
					if (nSet) { //если есть правая часть, т.е. знак '='
						GETDelimeter('=');//это присваивание переменной какого-то выражения
						CParamUnit expression = GetExpression();
						CByteUnit code;
						code.m_nOper = OPER_LET;
						AddLineInfo(code);

						CheckTypeDef(expression, variable.m_strType);
						variable.m_strType = expression.m_strType;

						bool bShortLet = false; int n = 0;

						if (DEF_VAR_TEMP == expression.m_nArray) { //сокращаем только временные переменные
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
							code.m_param2 = expression;
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

bool CCompileCode::CompileNewObject()
{
	GETKeyWord(KEY_NEW);

	wxString sObjectName = GETIdentifier(true);
	int nNumber = GetConstString(sObjectName);

	std::vector <CParamUnit> aParamList;

	if (IsNextDelimeter('('))//это вызов метода
	{
		GETDelimeter('(');

		while (!IsNextDelimeter(')'))
		{
			if (IsNextDelimeter(','))
			{
				CParamUnit data;
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

	if (!CValue::IsRegisterCtor(sObjectName, eCtorObjectType::eCtorObjectType_object_value))
		SetError(ERROR_CALL_CONSTRUCTOR, sObjectName);

	CByteUnit code;
	AddLineInfo(code);

	code.m_nOper = OPER_NEW;
	code.m_param2.m_nIndex = nNumber;//номер вызываемого метода из списка встретившихся методов
	code.m_param2.m_nArray = aParamList.size();//число параметров

	CParamUnit variable = GetVariable();
	code.m_param1 = variable;//переменная, в которую возвращается значение
	m_cByteCode.m_aCodeList.push_back(code);

	for (unsigned int i = 0; i < aParamList.size(); i++)
	{
		CByteUnit code;
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
bool CCompileCode::CompileGoto()
{
	GETKeyWord(KEY_GOTO);

	CLabel data;
	data.m_strName = GETIdentifier();
	data.m_nLine = m_cByteCode.m_aCodeList.size();//запоминаем те переходы, которые потом надо будет обработать
	data.m_nError = m_nCurrentCompile;
	m_pContext->m_cLabels.push_back(data);

	CByteUnit code;
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
CParamUnit CCompileCode::GetCurrentIdentifier(int& nIsSet)
{
	CParamUnit variable; int nPrevSet = nIsSet;

	wxString strRealName = GETIdentifier(true);
	wxString strName = stringUtils::MakeUpper(strRealName);

	if (IsNextDelimeter('(')) { //это вызов функции
		CFunction* foundedFunction = nullptr;
		if (m_cContext.FindFunction(strName, foundedFunction, true)) {
			int nNumber = GetConstString(strRealName);
			std::vector <CParamUnit> aParamList;
			GETDelimeter('(');
			while (!IsNextDelimeter(')')) {
				if (IsNextDelimeter(',')) {
					CParamUnit data;
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
			if (!nIsSet && foundedFunction != nullptr &&
				foundedFunction->m_pContext && foundedFunction->m_pContext->m_nReturn == RETURN_PROCEDURE) {
				SetError(ERROR_USE_PROCEDURE_AS_FUNCTION, foundedFunction->m_strRealName);
			}
			if (aParamList.size() > foundedFunction->m_aParamList.size()) {
				SetError(ERROR_MANY_PARAMS); //Слишком много фактических параметров
			}
			CByteUnit code;
			AddLineInfo(code);
			code.m_nOper = OPER_CALL_M;
			// переменная у которой вызывается метод
			code.m_param2 = GetVariable(foundedFunction->m_strContextVar, false, true);
			code.m_param3.m_nIndex = nNumber;//номер вызываемого метода из списка встретившихся методов
			code.m_param3.m_nArray = aParamList.size();//число параметров
			variable = GetVariable();
			code.m_param1 = variable;//переменная, в которую возвращается значение
			m_cByteCode.m_aCodeList.push_back(code);
			for (unsigned int i = 0; i < aParamList.size(); i++) {
				CByteUnit code;
				AddLineInfo(code);
				code.m_nOper = OPER_SET;
				code.m_param1 = aParamList[i];
				m_cByteCode.m_aCodeList.push_back(code);
			}
		}
		else {
			CFunction* foundedFunc = nullptr;
			if (m_bExpressionOnly)
				foundedFunc = GetFunction(strName);
			else if (!GetContext()->FindFunction(strName, foundedFunc))
				SetError(ERROR_CALL_FUNCTION, strRealName);
			if (!nIsSet && foundedFunc && foundedFunc->m_pContext && foundedFunc->m_pContext->m_nReturn == RETURN_PROCEDURE) {
				SetError(ERROR_USE_PROCEDURE_AS_FUNCTION, foundedFunc->m_strRealName);
			}
			variable = GetCallFunction(strRealName);
		}
		if (IsTypeVar(strRealName)) {
			variable.m_strType = GetTypeVar(strRealName);//это приведение типов
		}
		nIsSet = 0;
	}
	else { //это вызов переменной
		CVariable* foundedVar = nullptr; nIsSet = 1;
		if (m_cContext.FindVariable(strRealName, foundedVar, true)) {
			CByteUnit code;
			AddLineInfo(code);
			int nNumber = GetConstString(strRealName);
			if (IsNextDelimeter('=') && nPrevSet == 1) {
				GETDelimeter('='); nIsSet = 0;
				code.m_nOper = OPER_SET_A;
				code.m_param1 = GetVariable(foundedVar->m_strContextVar, false, true);//переменная у которой вызывается атрибут
				code.m_param2.m_nIndex = nNumber;//номер вызываемого метода из списка встретившихся атрибутов и методов
				code.m_param3 = GetExpression();
				m_cByteCode.m_aCodeList.push_back(code);
				return variable;
			}
			else {
				code.m_nOper = OPER_GET_A;
				code.m_param2 = GetVariable(foundedVar->m_strContextVar, false, true);//переменная у которой вызывается атрибут
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
			variable = GetVariable(strRealName, bCheckError);
		}
	}

MLabel:

	if (IsNextDelimeter('[')) { //это массив
		GETDelimeter('[');
		CParamUnit sKey = GetExpression();
		GETDelimeter(']');
		//определяем тип вызова (т.е. это установка значения массива или получение)
		//Пример:
		//Мас[10]=12; - Set
		//А=Мас[10]; - Get
		//Мас[10][2]=12; - Get,Set
		nIsSet = 0;
		if (IsNextDelimeter('[')) { //проверки типа переменной массива (поддержка многомерных массивов)
			CByteUnit code;
			AddLineInfo(code);
			code.m_nOper = OPER_CHECK_ARRAY;
			code.m_param1 = variable;//переменная - массив
			code.m_param2 = sKey;//индекс массива
			m_cByteCode.m_aCodeList.push_back(code);
		}
		if (IsNextDelimeter('=') && nPrevSet == 1) {
			GETDelimeter('=');
			CByteUnit code;
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
			CByteUnit code;
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
		wxString strRealMethod = GETIdentifier(true);
		//wxString sMethod = stringUtils::MakeUpper(strRealMethod);
		int nNumber = GetConstString(strRealMethod);
		if (IsNextDelimeter('(')) { //это вызов метода
			std::vector <CParamUnit> aParamList;
			GETDelimeter('(');
			while (!IsNextDelimeter(')')) {
				if (IsNextDelimeter(',')) {
					CParamUnit data;
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

			CByteUnit code;
			AddLineInfo(code);
			code.m_nOper = OPER_CALL_M;
			code.m_param2 = variable; // переменная у которой вызывается метод
			code.m_param3.m_nIndex = nNumber;//номер вызываемого метода из списка встретившихся методов
			code.m_param3.m_nArray = aParamList.size();//число параметров
			variable = GetVariable();
			code.m_param1 = variable;//переменная, в которую возвращается значение
			m_cByteCode.m_aCodeList.push_back(code);
			for (unsigned int i = 0; i < aParamList.size(); i++) {
				CByteUnit code;
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
			CByteUnit code;
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

bool CCompileCode::CompileIf()
{
	std::vector <int>aAddrLine;

	GETKeyWord(KEY_IF);

	CParamUnit sParam;
	CByteUnit code;
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

bool CCompileCode::CompileWhile()
{
	m_pContext->StartDoList();

	int nStartWhile = m_cByteCode.m_aCodeList.size();

	GETKeyWord(KEY_WHILE);
	CByteUnit code;
	AddLineInfo(code);
	code.m_nOper = OPER_IF;

	CParamUnit sParam = GetExpression();
	code.m_param1 = sParam;
	CorrectTypeDef(sParam);//проверка типа значения

	int nEndWhile = m_cByteCode.m_aCodeList.size();

	m_cByteCode.m_aCodeList.push_back(code);

	GETKeyWord(KEY_DO);
	CompileBlock();
	GETKeyWord(KEY_ENDDO);

	CByteUnit code2;
	AddLineInfo(code2);
	code2.m_nOper = OPER_GOTO;
	code2.m_param1.m_nIndex = nStartWhile;
	m_cByteCode.m_aCodeList.push_back(code2);

	m_cByteCode.m_aCodeList[nEndWhile].m_param2.m_nIndex = m_cByteCode.m_aCodeList.size();

	//запоминаем адреса переходов для команд Continue и Break
	m_pContext->FinishDoList(m_cByteCode, m_cByteCode.m_aCodeList.size() - 1, m_cByteCode.m_aCodeList.size());

	return true;
}

bool CCompileCode::CompileFor()
{
	m_pContext->StartDoList();

	GETKeyWord(KEY_FOR);

	wxString strRealName = GETIdentifier(true);
	wxString strName = stringUtils::MakeUpper(strRealName);

	CParamUnit variable = GetVariable(strRealName);

	//проверка типа переменной
	if (!variable.m_strType.IsEmpty()) {
		if (!CValue::CompareObjectName(variable.m_strType, eValueTypes::TYPE_NUMBER)) {
			SetError(ERROR_NUMBER_TYPE);
		}
	}

	GETDelimeter('=');
	CParamUnit Variable2 = GetExpression();

	CByteUnit code0;
	AddLineInfo(code0);
	code0.m_nOper = OPER_LET;
	code0.m_param1 = variable;
	code0.m_param2 = Variable2;
	m_cByteCode.m_aCodeList.push_back(code0);

	//проверка типа значения
	if (!variable.m_strType.IsEmpty())
	{
		if (!CValue::CompareObjectName(Variable2.m_strType, eValueTypes::TYPE_NUMBER)) {
			SetError(ERROR_BAD_TYPE_EXPRESSION);
		}
	}

	GETKeyWord(KEY_TO);
	CParamUnit VariableTo =
		m_pContext->GetVariable(strName + wxT("@to"), true, false, false, true); //loop variable

	CByteUnit code1;
	AddLineInfo(code1);
	code1.m_nOper = OPER_LET;
	code1.m_param1 = VariableTo;
	code1.m_param2 = GetExpression();
	m_cByteCode.m_aCodeList.push_back(code1);

	CByteUnit code;
	AddLineInfo(code);
	code.m_nOper = OPER_FOR;
	code.m_param1 = variable;
	code.m_param2 = VariableTo;
	m_cByteCode.m_aCodeList.push_back(code);

	int nStartFOR = m_cByteCode.m_aCodeList.size() - 1;

	GETKeyWord(KEY_DO);
	CompileBlock();
	GETKeyWord(KEY_ENDDO);

	CByteUnit code2;
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

bool CCompileCode::CompileForeach()
{
	m_pContext->StartDoList();

	GETKeyWord(KEY_FOREACH);

	wxString strRealName = GETIdentifier(true);
	wxString strName = stringUtils::MakeUpper(strRealName);

	CParamUnit variable = GetVariable(strRealName);

	GETKeyWord(KEY_IN);

	CParamUnit VariableIn =
		m_pContext->GetVariable(strName + wxT("@in"), true, false, false, true); //loop variable

	CByteUnit code1;
	AddLineInfo(code1);
	code1.m_nOper = OPER_LET;
	code1.m_param1 = VariableIn;
	code1.m_param2 = GetExpression();
	m_cByteCode.m_aCodeList.push_back(code1);

	CParamUnit VariableIt =
		m_pContext->GetVariable(strName + wxT("@it"), true, false, false, true);  //storage iterpos;

	CByteUnit code;
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

	CByteUnit code2;
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
CParamUnit CCompileCode::GetCallFunction(const wxString& strRealName)
{
	wxString strName = stringUtils::MakeUpper(strRealName);
	CCallFunction* callFunc = new CCallFunction;

	CFunction* foundedFunction = nullptr;
	if (m_bExpressionOnly)
		foundedFunction = GetFunction(strName);
	else if (!GetContext()->FindFunction(strName, foundedFunction))
		SetError(ERROR_CALL_FUNCTION, strRealName);

	callFunc->m_strName = strName;
	callFunc->m_strRealName = strRealName;
	callFunc->m_nError = m_nCurrentCompile;//для выдачи сообщений при ошибках
	GETDelimeter('(');
	while (!IsNextDelimeter(')')) {
		if (IsNextDelimeter(',')) {
			CParamUnit data;
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
	CParamUnit variable = GetVariable();

	CByteUnit code;
	AddLineInfo(code);

	callFunc->m_nNumberString = code.m_nNumberString;
	callFunc->m_nNumberLine = code.m_nNumberLine;
	callFunc->m_strModuleName = code.m_strModuleName;
	callFunc->m_sRetValue = variable;

	if (foundedFunction && GetContext()->m_strCurFuncName != strName) {
		AddCallFunction(callFunc);
		wxDELETE(callFunc);
	}
	else {
		if (m_bExpressionOnly)
			SetError(ERROR_CALL_FUNCTION, strRealName);
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
CParamUnit CCompileCode::FindConst(const CValue& vData)
{
	CParamUnit paramConst; paramConst.m_nArray = DEF_VAR_CONST;

	const wxString& strConst =
		wxString::Format(wxT("%d:%s"), vData.GetType(), vData.GetString());

	if (m_aHashConstList[strConst]) {
		paramConst.m_nIndex = m_aHashConstList[strConst] - 1;
	}
	else {
		paramConst.m_nIndex = m_cByteCode.m_aConstList.size();
		m_cByteCode.m_aConstList.push_back(vData);
		m_aHashConstList[strConst] = paramConst.m_nIndex + 1;
	}

	paramConst.m_strType = GetTypeVar(vData.GetClassName());
	return paramConst;
}

#define SetOper(x)	code.m_nOper=x;

/**
 * Компиляция произвольного выражения (служебные вызовы из самой функции)
 */
CParamUnit CCompileCode::GetExpression(int nPriority)
{
	CParamUnit variable;
	lexem_t lex = GETLexem();

	//Сначала обрабатываем Левые операторы
	if ((lex.m_nType == KEYWORD && lex.m_nData == KEY_NOT) || (lex.m_nType == DELIMITER && lex.m_nData == '!'))
	{
		variable = GetVariable();
		CParamUnit Variable2 = GetExpression(s_aPriority['!']);
		CByteUnit code;
		code.m_nOper = OPER_NOT;
		AddLineInfo(code);

		if (!Variable2.m_strType.IsEmpty()) {
			CheckTypeDef(Variable2, CValue::GetNameObjectFromVT(eValueTypes::TYPE_BOOLEAN));
		}

		variable.m_strType = CValue::GetNameObjectFromVT(eValueTypes::TYPE_BOOLEAN, true);

		code.m_param1 = variable;
		code.m_param2 = Variable2;
		m_cByteCode.m_aCodeList.push_back(code);
	}
	else if ((lex.m_nType == KEYWORD && lex.m_nData == KEY_NEW))
	{
		wxString sObjectName = GETIdentifier(true);
		int nNumber = GetConstString(sObjectName);

		std::vector <CParamUnit> aParamList;

		if (IsNextDelimeter('('))//это вызов метода
		{
			GETDelimeter('(');

			while (!IsNextDelimeter(')'))
			{
				if (IsNextDelimeter(','))
				{
					CParamUnit data;
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

		if (lex.m_nData == KEY_NEW && !CValue::IsRegisterCtor(sObjectName, eCtorObjectType::eCtorObjectType_object_value))
			SetError(ERROR_CALL_CONSTRUCTOR, sObjectName);

		CByteUnit code;
		AddLineInfo(code);
		code.m_nOper = OPER_NEW;

		code.m_param2.m_nIndex = nNumber;//номер вызываемого метода из списка встретившихся методов
		code.m_param2.m_nArray = aParamList.size();//число параметров

		variable = GetVariable();
		code.m_param1 = variable;//переменная, в которую возвращается значение
		m_cByteCode.m_aCodeList.push_back(code);

		for (unsigned int i = 0; i < aParamList.size(); i++)
		{
			CByteUnit code;
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
		CByteUnit code;
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
			CByteUnit code;
			variable = GetExpression(nPriority);
			if (!variable.m_strType.IsEmpty()) {
				CheckTypeDef(variable, CValue::GetNameObjectFromVT(eValueTypes::TYPE_NUMBER));
			}
			variable.m_strType = CValue::GetNameObjectFromVT(eValueTypes::TYPE_NUMBER, true);
			return variable;
		}
		else
		{
			variable = GetExpression(100);//сверх высокий приоритет!
			CByteUnit code;
			AddLineInfo(code);
			code.m_nOper = OPER_INVERT;

			if (!variable.m_strType.IsEmpty())
				CheckTypeDef(variable, CValue::GetNameObjectFromVT(eValueTypes::TYPE_NUMBER));

			code.m_param2 = variable;
			variable = GetVariable();
			variable.m_strType = CValue::GetNameObjectFromVT(eValueTypes::TYPE_NUMBER, true);
			code.m_param1 = variable;
			m_cByteCode.m_aCodeList.push_back(code);
		}
	}
	else
	{
		m_nCurrentCompile--;
		SetError(ERROR_EXPRESSION);
	}

	//Теперь обрабатываем Правые операторы
	//итак в variable имеем первый индекс переменной выражения

delimOperation:

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
				CByteUnit code;
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

				CParamUnit Variable1 = GetVariable();
				CParamUnit Variable2 = variable;
				CParamUnit Variable3 = GetExpression(nCurPriority);

				if (DEF_VAR_TEMP != Variable3.m_nArray && DEF_VAR_CONST != Variable3.m_nArray) { //доп. проверка на запрещенные операции
					if (CValue::CompareObjectName(Variable2.m_strType, eValueTypes::TYPE_STRING)) {
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
					CheckTypeDef(Variable3, Variable2.m_strType);
				}

				Variable1.m_strType = Variable2.m_strType;

				if (code.m_nOper >= OPER_GT && code.m_nOper <= OPER_NE) {
					Variable1.m_strType = CValue::GetNameObjectFromVT(eValueTypes::TYPE_BOOLEAN, true);
				}

				code.m_param1 = Variable1;
				code.m_param2 = Variable2;
				code.m_param3 = Variable3;

				m_cByteCode.m_aCodeList.push_back(code);

				variable = Variable1;
				goto delimOperation;
			}
		}
	}

	return variable;
}

void CCompileCode::SetParent(CCompileCode* setParent)
{
	m_cByteCode.m_parent = nullptr;

	m_parent = setParent;
	m_cContext.m_parentContext = nullptr;

	if (m_parent != nullptr) {
		m_cByteCode.m_parent = &m_parent->m_cByteCode;
		m_cContext.m_parentContext = &m_parent->m_cContext;
	}

	OnSetParent(setParent);
}

CParamUnit CCompileCode::AddVariable(const wxString& strName, const wxString& typeVar, bool exportVar, bool contextVar, bool tempVar)
{
	return m_pContext->AddVariable(strName, typeVar, exportVar, contextVar, tempVar);
}

/**
 * Функция возвращает номер переменной по строковому имени
 */
CParamUnit CCompileCode::GetVariable(const wxString& strName, bool bCheckError, bool bLoadFromContext)
{
	return m_pContext->GetVariable(strName, true, bCheckError, bLoadFromContext);
}

/**
 * Cоздаем новый идентификатор переменной
 */
CParamUnit CCompileCode::GetVariable()
{
	wxString strName = wxString::Format(wxT("@%d"), m_pContext->m_nTempVar);//@ - для гарантии уникальности имени
	CParamUnit variable = m_pContext->GetVariable(strName, false, false, false, true);//временную переменную ищем только в локальном контексте
	variable.m_nArray = DEF_VAR_TEMP;//признак временной локальной переменной
	m_pContext->m_nTempVar++;
	return variable;
}

#pragma warning(pop)