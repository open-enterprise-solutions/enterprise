////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, 2С-team
//	Description : Processor unit 
////////////////////////////////////////////////////////////////////////////

#include "procUnit.h"
#include "debugger/debugServer.h"
#include "systemObjects.h"
#include "utils/stringUtils.h"

#define curCode	m_pByteCode->m_aCodeList[lCodeLine]

#define index1	curCode.m_param1.m_nIndex
#define index2	curCode.m_param2.m_nIndex
#define index3	curCode.m_param3.m_nIndex
#define index4	curCode.m_param4.m_nIndex

#define array1	curCode.m_param1.m_nArray
#define array2	curCode.m_param2.m_nArray
#define array3	curCode.m_param3.m_nArray
#define array4	curCode.m_param4.m_nArray

#define locVariable1 *m_pRefLocVars[index1]
#define locVariable2 *m_pRefLocVars[index2]
#define locVariable3 *m_pRefLocVars[index3]
#define locVariable4 *m_pRefLocVars[index4]

#define variable(x) (array##x<=0?*pRefLocVars[index##x]:(array##x==DEF_VAR_CONST?m_pByteCode->m_aConstList[index##x]:*m_pppArrayList[array##x+(bDelta?1:0)][index##x]))

#define variable1 variable(1)
#define variable2 variable(2)
#define variable3 variable(3)
#define variable4 variable(4)

//**************************************************************************************************************
//*                                              support error place                                           *
//**************************************************************************************************************

CProcUnit* CProcUnit::m_currentRunModule = NULL;

struct CErrorPlace {
	long m_errorLine;
	byteCode_t* m_byteCode;
	byteCode_t* m_skipByteCode;
public:
	CErrorPlace() {
		Reset();
	}
	bool IsEmpty() const {
		return m_errorLine == wxNOT_FOUND;
	}
	void Reset() {
		m_byteCode = m_skipByteCode = NULL;
		m_errorLine = wxNOT_FOUND;
	};
};

static CErrorPlace s_errorPlace;

void CProcUnit::Raise() {
	s_errorPlace.Reset(); //инициализация места ошибки
	s_errorPlace.m_skipByteCode = CProcUnit::GetCurrentByteCode(); //возвращаемся назад в вызываемый модуль (если он есть)
}

std::vector <CRunContext*> CProcUnit::s_aRunContext;

//**************************************************************************************************************
//*                                              stack support                                                 *
//**************************************************************************************************************

inline void BeginByteCode(CRunContext* pCode)
{
	CProcUnit::AddRunContext(pCode);
}

inline bool EndByteCode()
{
	unsigned int n = CProcUnit::GetCountRunContext();
	if (n > 0)
		CProcUnit::BackRunContext();
	n--;
	if (n <= 0)
		return false;
	return true;
}

//Обнуление стека
inline void ResetByteCode()
{
	while (EndByteCode());
}

static int s_nRecCount = 0; //контроль зацикливания

struct CStackGuard {
	CRunContext* m_currentContext;
public:
	CStackGuard(CRunContext* runContext) {
		if (s_nRecCount > MAX_REC_COUNT) {//критическая ошибка	
			wxString sError = "";
			for (unsigned int i = 0; i < CProcUnit::GetCountRunContext(); i++) {
				CRunContext* pLastContext = CProcUnit::GetRunContext(i);
				wxASSERT(pLastContext);
				byteCode_t* m_pByteCode = pLastContext->GetByteCode();
				wxASSERT(m_pByteCode);
				sError += wxString::Format("\n%s (#line %d)",
					m_pByteCode->m_sModuleName,
					m_pByteCode->m_aCodeList[pLastContext->m_lCurLine].m_nNumberLine + 1
				);
			}
			CTranslateError::Error(
				_("Number of recursive calls exceeded the maximum allowed value!\nCall stack :") + sError
			);
		}
		s_nRecCount++;
		m_currentContext = runContext;
		BeginByteCode(runContext);
	};
	~CStackGuard() {
		s_nRecCount--;
		EndByteCode();
	};
};

//**************************************************************************************************************
//*                                              inline functions                                              *
//**************************************************************************************************************

//проверка доступности переменной
#define CHECK_READONLY(Operation)\
if(cValue1.m_bReadOnly)\
{\
	CValue cVal;\
	Operation(cVal,cValue2,cValue3);\
	cValue1.SetValue(cVal);\
	return;\
}\
if(cValue1.m_typeClass==eValueTypes::TYPE_REFFER)\
	cValue1.m_pRef->DecrRef();\

//Функции для быстрой работы с типом CValue
inline void AddValue(CValue& cValue1, const CValue& cValue2, const CValue& cValue3)
{
	CHECK_READONLY(AddValue);
	cValue1.m_typeClass = cValue2.GetType();
	if (cValue1.m_typeClass == eValueTypes::TYPE_NUMBER) {
		cValue1.m_fData = cValue2.GetNumber() + cValue3.GetNumber();
	}
	else if (cValue1.m_typeClass == eValueTypes::TYPE_DATE) {
		if (cValue3.m_typeClass == eValueTypes::TYPE_DATE) { //дата + дата -> число
			cValue1.m_typeClass = eValueTypes::TYPE_NUMBER;
			cValue1.m_fData = cValue2.GetDate() + cValue3.GetDate();
		}
		else {
			cValue1.m_dData = cValue2.m_dData + cValue3.GetDate();
		}
	}
	else {
		cValue1.m_typeClass = eValueTypes::TYPE_STRING;
		cValue1.m_sData = cValue2.GetString() + cValue3.GetString();
	}
}

inline void SubValue(CValue& cValue1, const CValue& cValue2, const CValue& cValue3)
{
	CHECK_READONLY(SubValue);
	cValue1.m_typeClass = cValue2.GetType();
	if (cValue1.m_typeClass == eValueTypes::TYPE_NUMBER) {
		cValue1.m_fData = cValue2.GetNumber() - cValue3.GetNumber();
	}
	else if (cValue1.m_typeClass == eValueTypes::TYPE_DATE) {
		if (cValue3.m_typeClass == eValueTypes::TYPE_DATE) { //дата - дата -> число
			cValue1.m_typeClass = eValueTypes::TYPE_NUMBER;
			cValue1.m_fData = cValue2.GetDate() - cValue3.GetDate();
		}
		else {
			cValue1.m_dData = cValue2.m_dData - cValue3.GetDate();
		}
	}
	else {
		CTranslateError::Error("Subtraction operation cannot be applied for this type (%s)", cValue2.GetTypeString());
	}
}

inline void MultValue(CValue& cValue1, const CValue& cValue2, const CValue& cValue3)
{
	CHECK_READONLY(MultValue);
	cValue1.m_typeClass = cValue2.GetType();
	if (cValue1.m_typeClass == eValueTypes::TYPE_NUMBER) {
		cValue1.m_fData = cValue2.GetNumber() * cValue3.GetNumber();
	}
	else if (cValue1.m_typeClass == eValueTypes::TYPE_DATE) {
		if (cValue3.m_typeClass == eValueTypes::TYPE_DATE) { //дата * дата -> число
			cValue1.m_typeClass = eValueTypes::TYPE_NUMBER;
			cValue1.m_fData = cValue2.GetDate() * cValue3.GetDate();
		}
		else
			cValue1.m_dData = cValue2.m_dData * cValue3.GetDate();
	}
	else {
		CTranslateError::Error("Multiplication operation cannot be applied for this type (%s)", cValue2.GetTypeString());
	}
}

inline void DivValue(CValue& cValue1, const CValue& cValue2, const CValue& cValue3)
{
	CHECK_READONLY(DivValue);
	cValue1.m_typeClass = cValue2.GetType();
	if (cValue1.m_typeClass == eValueTypes::TYPE_NUMBER) {
		const number_t& flNumber3 = cValue3.GetNumber();
		if (flNumber3.IsZero())
			CTranslateError::Error("Divide by zero");
		cValue1.m_fData = cValue2.GetNumber() / flNumber3;
	}
	else {
		CTranslateError::Error("Division operation cannot be applied for this type (%s)", cValue2.GetTypeString());
	};
}

inline void ModValue(CValue& cValue1, const CValue& cValue2, const CValue& cValue3)
{
	CHECK_READONLY(ModValue);
	cValue1.m_typeClass = cValue2.GetType();
	if (cValue1.m_typeClass == eValueTypes::TYPE_NUMBER) {
		ttmath::Int<TTMATH_BITS(128)> val128_2, val128_3;
		const number_t& flNumber3 = cValue3.GetNumber(); flNumber3.ToInt(val128_3);
		if (val128_3.IsZero())
			CTranslateError::Error("Divide by zero");
		const number_t& flNumber2 = cValue2.GetNumber(); flNumber2.ToInt(val128_2);
		cValue1.m_fData = val128_2 % val128_3;
	}
	else {
		CTranslateError::Error("Modulo operation cannot be applied for this type (%s)", cValue2.GetTypeString());
	}
}

//Реализация операторов сравнения
inline void CompareValueGT(CValue& cValue1, const CValue& cValue2, const CValue& cValue3)
{
	CHECK_READONLY(CompareValueGT);
	cValue1.m_typeClass = eValueTypes::TYPE_BOOLEAN;
	cValue1.m_bData = cValue2.CompareValueGT(cValue3);
}

inline void CompareValueGE(CValue& cValue1, const CValue& cValue2, const CValue& cValue3)
{
	CHECK_READONLY(CompareValueGT);
	cValue1.m_typeClass = eValueTypes::TYPE_BOOLEAN;
	cValue1.m_bData = cValue2.CompareValueGT(cValue3);
}

inline void CompareValueLS(CValue& cValue1, const CValue& cValue2, const CValue& cValue3)
{
	CHECK_READONLY(CompareValueLS);
	cValue1.m_typeClass = eValueTypes::TYPE_BOOLEAN;
	cValue1.m_bData = cValue2.CompareValueLS(cValue3);
}

inline void CompareValueLE(CValue& cValue1, const CValue& cValue2, const CValue& cValue3)
{
	CHECK_READONLY(CompareValueLE);
	cValue1.m_typeClass = eValueTypes::TYPE_BOOLEAN;
	cValue1.m_bData = cValue2.CompareValueLE(cValue3);
}

inline void CompareValueEQ(CValue& cValue1, const CValue& cValue2, const CValue& cValue3)
{
	CHECK_READONLY(CompareValueEQ);
	cValue1.m_typeClass = eValueTypes::TYPE_BOOLEAN;
	cValue1.m_bData = cValue2.CompareValueEQ(cValue3);
}

inline void CompareValueNE(CValue& cValue1, const CValue& cValue2, const CValue& cValue3)
{
	CHECK_READONLY(CompareValueNE);
	cValue1.m_typeClass = eValueTypes::TYPE_BOOLEAN;
	cValue1.m_bData = cValue2.CompareValueNE(cValue3);
}

inline void CopyValue(CValue& cValue1, CValue& cValue2)
{
	if (&cValue1 == &cValue2)
		return;

	//проверка доступности переменной и контроль ссылок
	if (cValue1.m_bReadOnly) {
		cValue1.SetValue(cValue2);
		return;
	}
	else {//Reset
		if (cValue1.m_pRef && cValue1.m_typeClass == eValueTypes::TYPE_REFFER)
			cValue1.m_pRef->DecrRef();

		cValue1.m_typeClass = eValueTypes::TYPE_EMPTY;
		cValue1.m_sData = wxEmptyString;

		cValue1.m_pRef = NULL;
	}

	if (cValue2.m_typeClass == eValueTypes::TYPE_REFFER) {
		cValue1 = cValue2.GetValue();
		return;
	}

	cValue1.m_typeClass = cValue2.m_typeClass;

	switch (cValue2.m_typeClass)
	{
	case eValueTypes::TYPE_NULL:
		break;
	case eValueTypes::TYPE_BOOLEAN:
		cValue1.m_bData = cValue2.m_bData;
		break;
	case eValueTypes::TYPE_NUMBER:
		cValue1.m_fData = cValue2.m_fData;
		break;
	case eValueTypes::TYPE_STRING:
		cValue1.m_sData = cValue2.m_sData;
		break;
	case eValueTypes::TYPE_DATE: cValue1.m_dData = cValue2.m_dData;
		break;
	case eValueTypes::TYPE_REFFER: cValue1.m_pRef = cValue2.m_pRef; cValue1.m_pRef->IncrRef();
		break;
	case eValueTypes::TYPE_MODULE:
	case eValueTypes::TYPE_OLE:
	case eValueTypes::TYPE_ENUM:
	case eValueTypes::TYPE_VALUE:
		cValue1.m_typeClass = eValueTypes::TYPE_REFFER;
		cValue1.m_pRef = &cValue2; cValue1.m_pRef->IncrRef();
		break;
	default: cValue1.m_typeClass = eValueTypes::TYPE_EMPTY;
	}
}

inline bool IsEmptyValue(const CValue& cValue1)
{
	return cValue1.IsEmpty();
}

#define IsHasValue(cValue1) (!IsEmptyValue(cValue1))

inline void SetTypeBoolean(CValue& cValue1, bool bValue)
{
	//проверка доступности переменной и контроль ссылок
	if (cValue1.m_bReadOnly) {
		cValue1.SetValue(bValue);
		return;
	}
	cValue1.Reset();
	cValue1.m_typeClass = eValueTypes::TYPE_BOOLEAN;
	cValue1.m_bData = bValue;
}

inline void SetTypeNumber(CValue& cValue1, const number_t& fValue)
{
	//проверка доступности переменной и контроль ссылок
	if (cValue1.m_bReadOnly) {
		cValue1.SetValue(fValue);
		return;
	}
	cValue1.Reset();
	cValue1.m_typeClass = eValueTypes::TYPE_NUMBER;
	cValue1.m_fData = fValue;
}

#define CheckAndError(variable, name)\
{\
	if(variable.m_typeClass!=eValueTypes::TYPE_REFFER)\
		CTranslateError::Error("No attribute or method found '%s' - a variable is not an aggregate object", name);\
	else\
		CTranslateError::Error("Aggregate object field not found '%s'", name);\
}

//Индексные массивы
inline void SetArrayValue(CValue& cValue1, const CValue& cValue2, CValue& cValue3)
{
	cValue1.SetAt(cValue2, cValue3);
}

inline void GetArrayValue(CValue& cValue1, CValue& cValue2, const CValue& cValue3)
{
	CValue retValue;
	if (cValue2.GetAt(cValue3, retValue))
		CopyValue(cValue1, retValue);
}

inline CValue GetValue(CValue& cValue1)
{
	if (cValue1.m_bReadOnly
		&& cValue1.m_typeClass != eValueTypes::TYPE_REFFER) {
		CValue cVal;
		CopyValue(cVal, cValue1);
		return cVal;
	}
	return cValue1;
}

//////////////////////////////////////////////////////////////////////
//						Construction/Destruction                    //
//////////////////////////////////////////////////////////////////////

#include "definition.h"

struct tryData_t {
	long m_lStartLine, m_lEndLine;
public:
	tryData_t() :
		m_lStartLine(0), m_lEndLine(0) {
	}
	tryData_t(long lStartLine, long lEndLine) :
		m_lStartLine(lStartLine), m_lEndLine(m_lEndLine) {
	}
};

CValue CProcUnit::Execute(CRunContext* pContext, bool bDelta)
{
#ifdef _DEBUG
	if (pContext == NULL) {
		CTranslateError::Error("No execution context defined!");
		if (m_pByteCode == NULL)
			CTranslateError::Error("No execution code set!");
	}
#endif

	pContext->SetProcUnit(this);

	CStackGuard cStack(pContext);

	CValue* pLocVars = pContext->m_pLocVars;
	CValue** pRefLocVars = pContext->m_pRefLocVars;

	byteRaw_t* pCodeList = m_pByteCode->m_aCodeList.data();

	long lCodeLine = pContext->m_lStart;
	long lFinish = m_pByteCode->m_aCodeList.size();
	long lPrevLine = wxNOT_FOUND;

	CValue cRetValue;
	std::vector<tryData_t> tryList;

start_label:

	try { //медленнее на 2-3% на каждый вложенный модуль
		while (lCodeLine < lFinish) {
			if (!CTranslateError::IsSimpleMode()) {
				pContext->m_lCurLine = lCodeLine;
				m_currentRunModule = this;
			}

			//enter in debugger
			if (!CTranslateError::IsSimpleMode())
				debugServer->EnterDebugger(pContext, curCode, lPrevLine);

			switch (curCode.m_nOper)
			{
			case OPER_CONST: CopyValue(variable1, m_pByteCode->m_aConstList[index2]); break;
			case OPER_CONSTN: SetTypeNumber(variable1, index2); break;
			case OPER_ADD: AddValue(variable1, variable2, variable3); break;
			case OPER_SUB: SubValue(variable1, variable2, variable3); break;
			case OPER_DIV: DivValue(variable1, variable2, variable3); break;
			case OPER_MOD: ModValue(variable1, variable2, variable3); break;
			case OPER_MULT: MultValue(variable1, variable2, variable3); break;
			case OPER_LET: CopyValue(variable1, variable2); break;
			case OPER_INVERT: SetTypeNumber(variable1, -variable2.GetNumber()); break;
			case OPER_NOT: SetTypeBoolean(variable1, IsEmptyValue(variable2)); break;
			case OPER_AND: if (IsHasValue(variable2) && IsHasValue(variable3))
				SetTypeBoolean(variable1, true); else SetTypeBoolean(variable1, false);
				break;
			case OPER_OR:
				if (IsHasValue(variable2) || IsHasValue(variable3))
					SetTypeBoolean(variable1, true);
				else SetTypeBoolean(variable1, false); break;
			case OPER_EQ: CompareValueEQ(variable1, variable2, variable3); break;
			case OPER_NE: CompareValueNE(variable1, variable2, variable3); break;
			case OPER_GT: CompareValueGT(variable1, variable2, variable3); break;
			case OPER_LS: CompareValueLS(variable1, variable2, variable3); break;
			case OPER_GE: CompareValueGE(variable1, variable2, variable3); break;
			case OPER_LE: CompareValueLE(variable1, variable2, variable3); break;
			case OPER_IF:
				if (IsEmptyValue(variable1))
					lCodeLine = index2 - 1;
				break;
			case OPER_FOR:
				if (variable1.m_typeClass != eValueTypes::TYPE_NUMBER)
					CTranslateError::Error("Only variables with type can be used to organize the loop \"number\"");
				if (variable1.m_fData == variable2.m_fData)
					lCodeLine = index3 - 1;
				break;
			case OPER_FOREACH: {
				if (!variable2.HasIterator()) {
					CTranslateError::Error("Undefined value iterator");
				}
				if (variable3.m_typeClass != eValueTypes::TYPE_NUMBER) {
					CopyValue(variable3, CValue(0));
				}
				unsigned int m_nIndex = variable3.GetUInteger();
				if (m_nIndex < variable2.GetItSize()) {
					CopyValue(variable1, variable2.GetItAt(m_nIndex));
				}
				else {
					variable3 = 0; lCodeLine = index4 - 1;
				}
			} break;
			case OPER_NEXT: {if (variable1.m_typeClass == eValueTypes::TYPE_NUMBER) {
				variable1.m_fData++;
			} lCodeLine = index2 - 1;
			} break;
			case OPER_NEXT_ITER: {
				variable1.m_fData++; lCodeLine = index2 - 1;
			} break;
			case OPER_ITER: {
				if (IsHasValue(variable2))
					CopyValue(variable1, variable3);
				else
					CopyValue(variable1, variable4);
			}  break;
			case OPER_NEW: {
				CValue* pRetValue = &variable1;
				CRunContextSmall cRunContext(array2);
				cRunContext.m_lParamCount = array2;
				const wxString className = m_pByteCode->m_aConstList[index2].m_sData;
				//загружаем параметры
				for (long i = 0; i < cRunContext.m_lParamCount; i++) {
					lCodeLine++;
					if (index1 >= 0) {
						if (variable1.m_bReadOnly && variable1.m_typeClass != eValueTypes::TYPE_REFFER) {
							CopyValue(cRunContext.m_pLocVars[i], variable1);
						}
						else {
							cRunContext.m_pRefLocVars[i] = &variable1;
						}
					}
				}
				CopyValue(*pRetValue, CValue::CreateObject(className, cRunContext.m_lParamCount > 0 ? cRunContext.m_pRefLocVars : NULL, cRunContext.m_lParamCount));
			} break;
			case OPER_SET_A: {//установка атрибута
				const wxString& propName = m_pByteCode->m_aConstList[index2].m_sData;
				const long lPropNum = variable1.FindProp(propName);
				if (lPropNum < 0)
					CheckAndError(variable1, propName);
				if (!variable1.IsPropWritable(lPropNum))
					CTranslateError::Error("Object field not writable (%s)", propName);
				variable1.SetPropVal(lPropNum, GetValue(variable3));
			} break;
			case OPER_GET_A://получение атрибута
			{
				CValue* pRetValue = &variable1;
				CValue* pVariable2 = &variable2;
				const wxString& propName = m_pByteCode->m_aConstList[index3].m_sData;
				const long lPropNum = variable2.FindProp(propName);
				if (lPropNum < 0)
					CheckAndError(variable2, propName);
				if (!variable2.IsPropReadable(lPropNum))
					CTranslateError::Error("Object field not readable (%s)", propName);
				
				CValue vRet; bool result = variable2.GetPropVal(lPropNum, vRet);
				if (result && vRet.m_typeClass == eValueTypes::TYPE_REFFER)
					*pRetValue = vRet;
				else if (result)
					CopyValue(*pRetValue, vRet);
				break;
			}
			case OPER_CALL_M://вызов метода
			{
				CValue* pRetValue = &variable1;
				CValue* pVariable2 = &variable2;

				const wxString& funcName = m_pByteCode->m_aConstList[index3].m_sData;
				long lMethodNum = wxNOT_FOUND;
				//оптимизация вызовов
				CValue* storageValue = reinterpret_cast<CValue*>(array4);
				if (storageValue && storageValue == pVariable2->GetRef()) { //ранее были вызовы 
					lMethodNum = index4;
#ifdef _DEBUG
					lMethodNum = pVariable2->FindMethod(funcName);
					if (lMethodNum != index4)
						CTranslateError::Error("Error value %d must %d (It is recommended to turn off method optimization)", index4, lMethodNum);
#endif
				}
				else {//не было вызовов
					lMethodNum = pVariable2->FindMethod(funcName);
					index4 = lMethodNum;
					array4 = reinterpret_cast<wxLongLong_t>(pVariable2->GetRef());
				}

				if (lMethodNum < 0)
					CheckAndError(variable2, funcName);

				CRunContextSmall cRunContext(std::max(array3, MAX_STATIC_VAR));
				cRunContext.m_lParamCount = array3;

				//Слишком много фактических параметров
				long paramCount = pVariable2->GetNParams(lMethodNum);

				if (paramCount < cRunContext.m_lParamCount)
					CTranslateError::Error(ERROR_MANY_PARAMS, funcName, funcName);
				else if (paramCount == wxNOT_FOUND && cRunContext.m_lParamCount == 0)
					CTranslateError::Error(ERROR_MANY_PARAMS, funcName, funcName);

				//загружаем параметры
				for (long i = 0; i < cRunContext.m_lParamCount; i++) {
					lCodeLine++;
					if (index1 >= 0 && !pVariable2->GetParamDefValue(lMethodNum, i, *cRunContext.m_pRefLocVars[i])) {
						if (variable1.m_bReadOnly && variable1.m_typeClass != eValueTypes::TYPE_REFFER) {
							CopyValue(cRunContext.m_pLocVars[i], variable1);
						}
						else {
							cRunContext.m_pRefLocVars[i] = &variable1;
						}
					}
				}

				if (pVariable2->HasRetVal(lMethodNum)) {
					pVariable2->CallAsFunc(lMethodNum,
						*pRetValue, cRunContext.m_pRefLocVars, cRunContext.m_lParamCount
					);
				}
				else {
					// operator = 
					if (m_pByteCode->m_aCodeList[lCodeLine + 1].m_nOper == OPER_LET)
						CTranslateError::Error(ERROR_USE_PROCEDURE_AS_FUNCTION, funcName, funcName);
					bool result = pVariable2->CallAsProc(lMethodNum,
						cRunContext.m_pRefLocVars, cRunContext.m_lParamCount
					);
				} break;
			}
			case OPER_CALL: { //вызов обычной функции
				long lModuleNumber = array2;
				CRunContext cRunContext(index3);
				cRunContext.m_lStart = index2;
				cRunContext.m_lParamCount = array3;
				byteCode_t* pLocalByteCode = m_ppArrayCode[lModuleNumber]->m_pByteCode;
				byteRaw_t* pCodeList2 = pLocalByteCode->m_aCodeList.data();
				cRunContext.m_compileContext = reinterpret_cast<compileContext_t*>(pCodeList2[cRunContext.m_lStart].m_param1.m_nArray);
				CValue* pRetValue = &variable1;
				//загружаем параметры
				for (long i = 0; i < cRunContext.m_lParamCount; i++) {
					lCodeLine++;
					if (curCode.m_nOper == OPER_SETCONST) {
						CopyValue(cRunContext.m_pLocVars[i], pLocalByteCode->m_aConstList[index1]);
					}
					else {
						if (variable1.m_bReadOnly || index2 == 1) {//передача параметра по значению
							CopyValue(cRunContext.m_pLocVars[i], variable1);
						}
						else {
							cRunContext.m_pRefLocVars[i] = &variable1;
						}
					}
				}
				CopyValue(*pRetValue, m_ppArrayCode[lModuleNumber]->Execute(&cRunContext, 0));
				break;
			}
			case OPER_SET_ARRAY: SetArrayValue(variable1, variable2, GetValue(variable3)); break; //установка значения массива
			case OPER_GET_ARRAY: GetArrayValue(variable1, variable2, variable3); break; //получение значения массива			
			case OPER_GOTO: case OPER_ENDTRY: {
				long lNewLine = index1;
				long size = tryList.size() - 1;
				if (size >= 0) {
					if (lNewLine >= tryList[size].m_lEndLine ||
						lNewLine <= tryList[size].m_lStartLine) {
						tryList.resize(size);//выход из поля видимости  try..catch
					}
				}
				lCodeLine = lNewLine - 1;//т.к. потом добавим 1
			} break;
			case OPER_TRY: tryList.emplace_back(lCodeLine, index1); break; //переход при ошибке
			case OPER_RAISE: CTranslateError::Error(CTranslateError::GetLastError()); break;
			case OPER_RAISE_T: CTranslateError::Error(m_pByteCode->m_aConstList[index1].GetString()); break;
			case OPER_RET: if (index1 != DEF_VAR_NORET) CopyValue(cRetValue, variable1);
			case OPER_ENDFUNC:
			case OPER_END:
				lCodeLine = lFinish;
				break; //выход
			case OPER_FUNC: if (bDelta) {
				while (lCodeLine < lFinish) {
					if (curCode.m_nOper != OPER_ENDFUNC) {
						lCodeLine++;
					}
					else break;
				}
			} break; //это начальный запуск - пропускаем тела процедур и функций
			case OPER_SET_TYPE:
				variable1.SetType(CValue::GetVTByID(array2));
				break;
				//Операторы работы с типизированными данными
				//NUMBER
			case OPER_ADD + TYPE_DELTA1: variable1.m_fData = variable2.m_fData + variable3.m_fData; break;
			case OPER_SUB + TYPE_DELTA1: variable1.m_fData = variable2.m_fData - variable3.m_fData; break;
			case OPER_DIV + TYPE_DELTA1: if (variable3.m_fData.IsZero()) { CTranslateError::Error("Divide by zero"); } variable1.m_fData = variable2.m_fData / variable3.m_fData; break;
			case OPER_MOD + TYPE_DELTA1: if (variable3.m_fData.IsZero()) { CTranslateError::Error("Divide by zero"); } variable1.m_fData = variable2.m_fData.Round() % variable3.m_fData.Round(); break;
			case OPER_MULT + TYPE_DELTA1: variable1.m_fData = variable2.m_fData * variable3.m_fData; break;
			case OPER_LET + TYPE_DELTA1: variable1.m_fData = variable2.m_fData; break;
			case OPER_NOT + TYPE_DELTA1: variable1.m_fData = variable2.m_fData.IsZero(); break;
			case OPER_INVERT + TYPE_DELTA1: variable1.m_fData = -variable2.m_fData; break;
			case OPER_EQ + TYPE_DELTA1: variable1.m_fData = (variable2.m_fData == variable3.m_fData); break;
			case OPER_NE + TYPE_DELTA1: variable1.m_fData = (variable2.m_fData != variable3.m_fData); break;
			case OPER_GT + TYPE_DELTA1: variable1.m_fData = (variable2.m_fData > variable3.m_fData); break;
			case OPER_LS + TYPE_DELTA1: variable1.m_fData = (variable2.m_fData < variable3.m_fData); break;
			case OPER_GE + TYPE_DELTA1: variable1.m_fData = (variable2.m_fData >= variable3.m_fData); break;
			case OPER_LE + TYPE_DELTA1: variable1.m_fData = (variable2.m_fData <= variable3.m_fData); break;
			case OPER_SET_ARRAY + TYPE_DELTA1:	SetArrayValue(variable1, variable2, GetValue(variable3)); break;//установка значения массива
			case OPER_GET_ARRAY + TYPE_DELTA1: GetArrayValue(variable1, variable2, variable3); break; //получение значения массива	
			case OPER_IF + TYPE_DELTA1: if (variable1.m_fData.IsZero()) lCodeLine = index2 - 1; break;
				//STRING
			case OPER_ADD + TYPE_DELTA2: variable1.m_sData = variable2.m_sData + variable3.m_sData; break;
			case OPER_LET + TYPE_DELTA2: variable1.m_sData = variable2.m_sData; break;
			case OPER_SET_ARRAY + TYPE_DELTA2: SetArrayValue(variable1, variable2, GetValue(variable3)); break;//установка значения массива			
			case OPER_GET_ARRAY + TYPE_DELTA2: GetArrayValue(variable1, variable2, variable3); break; //получение значения массива
			case OPER_IF + TYPE_DELTA2: if (variable1.m_sData.IsEmpty()) lCodeLine = index2 - 1; break;
				//DATE
			case OPER_ADD + TYPE_DELTA3: variable1.m_dData = variable2.m_dData + variable3.m_dData; break;
			case OPER_SUB + TYPE_DELTA3: variable1.m_dData = variable2.m_dData - variable3.m_dData; break;
			case OPER_DIV + TYPE_DELTA3: if (variable3.m_dData == 0) { CTranslateError::Error("Divide by zero"); } variable1.m_dData = variable2.m_dData / variable3.GetInteger(); break;
			case OPER_MOD + TYPE_DELTA3: if (variable3.m_dData == 0) { CTranslateError::Error("Divide by zero"); } variable1.m_dData = (int)variable2.m_dData % variable3.GetInteger(); break;
			case OPER_MULT + TYPE_DELTA3: variable1.m_dData = variable2.m_dData * variable3.m_dData; break;
			case OPER_LET + TYPE_DELTA3: variable1.m_dData = variable2.m_dData; break;
			case OPER_NOT + TYPE_DELTA3: variable1.m_dData = ~variable2.m_dData; break;
			case OPER_INVERT + TYPE_DELTA3: variable1.m_dData = -variable2.m_dData; break;
			case OPER_EQ + TYPE_DELTA3: variable1.m_dData = (variable2.m_dData == variable3.m_dData); break;
			case OPER_NE + TYPE_DELTA3: variable1.m_dData = (variable2.m_dData != variable3.m_dData); break;
			case OPER_GT + TYPE_DELTA3: variable1.m_dData = (variable2.m_dData > variable3.m_dData); break;
			case OPER_LS + TYPE_DELTA3: variable1.m_dData = (variable2.m_dData < variable3.m_dData); break;
			case OPER_GE + TYPE_DELTA3: variable1.m_dData = (variable2.m_dData >= variable3.m_dData); break;
			case OPER_LE + TYPE_DELTA3: variable1.m_dData = (variable2.m_dData <= variable3.m_dData); break;
			case OPER_SET_ARRAY + TYPE_DELTA3:	SetArrayValue(variable1, variable2, GetValue(variable3)); break; //установка значения массива
			case OPER_GET_ARRAY + TYPE_DELTA3: GetArrayValue(variable1, variable2, variable3); break; //получение значения массива
			case OPER_IF + TYPE_DELTA3: if (!variable1.m_dData) lCodeLine = index2 - 1; break;
				//BOOLEAN
			case OPER_ADD + TYPE_DELTA4: variable1.m_bData = variable2.m_bData + variable3.m_bData; break;
			case OPER_LET + TYPE_DELTA4: variable1.m_bData = variable2.m_bData; break;
			case OPER_NOT + TYPE_DELTA4: variable1.m_bData = !variable2.m_bData; break;
			case OPER_INVERT + TYPE_DELTA4: variable1.m_bData = !variable2.m_bData; break;
			case OPER_EQ + TYPE_DELTA4: variable1.m_bData = (variable2.m_bData == variable3.m_bData); break;
			case OPER_NE + TYPE_DELTA4: variable1.m_bData = (variable2.m_bData != variable3.m_bData); break;
			case OPER_GT + TYPE_DELTA4: variable1.m_bData = (variable2.m_bData > variable3.m_bData); break;
			case OPER_LS + TYPE_DELTA4: variable1.m_bData = (variable2.m_bData < variable3.m_bData); break;
			case OPER_GE + TYPE_DELTA4: variable1.m_bData = (variable2.m_bData >= variable3.m_bData); break;
			case OPER_LE + TYPE_DELTA4: variable1.m_bData = (variable2.m_bData <= variable3.m_bData); break;
			case OPER_IF + TYPE_DELTA4: if (!variable1.m_bData) lCodeLine = index2 - 1; break;
			}
			lCodeLine++;
		}
	}
	catch (const CInterruptBreak* err) {
		CSystemObjects::Message(err->what(),
			eStatusMessage::eStatusMessage_Error
		);
		while (lCodeLine < lFinish) {
			if (curCode.m_nOper != OPER_GOTO
				&& curCode.m_nOper != OPER_NEXT
				&& curCode.m_nOper != OPER_NEXT_ITER) {
				lCodeLine++;
			}
			else {
				lCodeLine++;
				goto start_label;
				break;
			}
		}
		return CValue();
	}
	catch (const CTranslateError* err) {
		long size = tryList.size() - 1;
		if (size >= 0) {
			s_errorPlace.Reset(); //Ошибка обрабатывается в этом модуле - стираем место ошибки
			long nLine = tryList[size].m_lEndLine;
			tryList.resize(size);
			lCodeLine = nLine;
			goto start_label;
		}
		//в этом модуле нет обработчика - сохраняем место ошибки для следующих модулей
		//Но ошибку сразу не выдаем, т.к. не знаем есть ли дальше обработчики
		if (!s_errorPlace.m_byteCode) {
			if (m_pByteCode != s_errorPlace.m_skipByteCode) { //системная функция Ошибка выдает исключение только для дочерних модулей
				//ранее сохранили оргинально место ошибки (т.е. ошибка произошла не в этом модуле)
				s_errorPlace.m_byteCode = m_pByteCode;
				s_errorPlace.m_errorLine = lCodeLine;
			}
		}
		CTranslateError::ProcessError(m_pByteCode->m_aCodeList[lCodeLine], err->what());
	}

	return cRetValue;
}

//параметры nRunModule:
//false-не запускать
//true-запускать
CValue CProcUnit::Execute(byteCode_t& cByteCode, bool bRunModule)
{
	if (!cByteCode.m_bCompile)
		CTranslateError::Error("Module: %s not compiled!", cByteCode.m_sModuleName);

	s_nRecCount = 0;
	m_pByteCode = &cByteCode;

	//проверяем соответствия модулей (скомпилированного и запущенного)
	if (GetParent()) {
		if (GetParent()->m_pByteCode != m_pByteCode->m_parent) {
			m_pByteCode = NULL;
			CTranslateError::Error("System error - compilation failed (#1)\nModule:%s\nParent1:%s\nParent2:%s",
				cByteCode.m_sModuleName,
				cByteCode.m_parent->m_sModuleName,
				GetParent()->m_pByteCode->m_sModuleName
			);
		}
	}
	else if (m_pByteCode->m_parent) {
		m_pByteCode = NULL;
		CTranslateError::Error("System error - compilation failed (#2)\nModule1:%s\nParent1:%s",
			cByteCode.m_sModuleName,
			cByteCode.m_parent->m_sModuleName
		);
	}

	m_cCurContext.SetLocalCount(cByteCode.m_lVarCount);
	m_cCurContext.m_lStart = cByteCode.m_lStartModule;

	unsigned int nParentCount = GetParentCount();

	m_ppArrayCode = new CProcUnit * [nParentCount + 1];
	m_ppArrayCode[0] = this;

	m_pppArrayList = new CValue * *[nParentCount + 2];
	m_pppArrayList[0] = m_cCurContext.m_pRefLocVars;
	m_pppArrayList[1] = m_cCurContext.m_pRefLocVars;//начинаем с 1, т.к. 0 - означает локальный контекст

	for (unsigned int i = 0; i < nParentCount; i++) {
		CProcUnit* pCurUnit = GetParent(i);
		m_ppArrayCode[i + 1] = pCurUnit;
		m_pppArrayList[i + 2] = pCurUnit->m_cCurContext.m_pRefLocVars;
	}

	//поддержка внешних переменных
	for (unsigned int i = 0; i < cByteCode.m_aExternValues.size(); i++) {
		if (cByteCode.m_aExternValues[i]) {
			m_cCurContext.m_pRefLocVars[i] = cByteCode.m_aExternValues[i];
		}
	}

	bool bDelta = true;

	//Начальная инициализация переменных модуля
	unsigned int lFinish = m_pByteCode->m_aCodeList.size();
	CValue** pRefLocVars = m_cCurContext.m_pRefLocVars;

	for (unsigned int lCodeLine = 0; lCodeLine < lFinish; lCodeLine++) {
		byteRaw_t& byte = m_pByteCode->m_aCodeList[lCodeLine];
		if (byte.m_nOper == OPER_SET_TYPE) {
			variable1.SetType(CValue::GetVTByID(array2));
		}
	}

	//Запрещаем на запись константы
	for (unsigned int i = 0; i < m_pByteCode->m_aConstList.size(); i++) {
		m_pByteCode->m_aConstList[i].m_bReadOnly = true;
	}

	if (bRunModule) {
		m_cCurContext.m_compileContext = &cByteCode.m_compileModule->m_cContext;
		return Execute(&m_cCurContext, bDelta);
	}

	return CValue();
}

//Поиск функции в модуле по имени
//bExportOnly=0-поиск любых функций в текущем модуле + экспортные в родительских модулях
//bExportOnly=1-поиск экспортных функций в текущем и родительских модулях
//bExportOnly=2-поиск экспортных функций в только текущем модуле
long CProcUnit::FindMethod(const wxString& methodName, bool bError, int bExportOnly) const
{
	if (!m_pByteCode ||
		!m_pByteCode->m_bCompile) {
		CTranslateError::Error("Module not compiled!");
	}

	long lCodeLine = bExportOnly ?
		m_pByteCode->FindExportMethod(methodName) :
		m_pByteCode->FindMethod(methodName);

	if (bError && lCodeLine < 0)
		CTranslateError::Error("Procedure or function \"%s\" not found!", methodName);

	if (lCodeLine >= 0) {
		return lCodeLine;
	}
	if (GetParent() &&
		bExportOnly <= 1) {
		unsigned int nCodeSize = m_pByteCode->m_aCodeList.size();
		lCodeLine = GetParent()->FindMethod(methodName, false, 1);
		if (lCodeLine >= 0)
			return nCodeSize + lCodeLine;
	}
	return wxNOT_FOUND;
}

//Вызов функции по имении
//Вызов осуществляется только в текущем модуле
CValue CProcUnit::CallFunction(const wxString& funcName, CValue** ppParams, const long lSizeArray)
{
	if (m_pByteCode != NULL) {
		long lCodeLine = m_pByteCode->FindMethod(funcName);
		if (lCodeLine != wxNOT_FOUND)
			return CallFunction(lCodeLine, ppParams, lSizeArray);
	}
	return CValue();
}

//Вызов функции по ее адресу в массиве байт кодов
//Вызов осущетсвляется в т.ч. и в родительском модуле
CValue CProcUnit::CallFunction(const long lCodeLine, CValue** ppParams, const long lSizeArray)
{
	if (m_pByteCode == NULL || !m_pByteCode->m_bCompile)
		CTranslateError::Error("Module not compiled!");
	long lCodeSize = m_pByteCode->m_aCodeList.size();
	if (lCodeLine >= lCodeSize) {
		if (GetParent())
			return GetParent()->CallFunction(lCodeLine - lCodeSize, ppParams, lSizeArray);
		CTranslateError::Error("Error calling module function!");
	}

	CRunContext cRunContext(index3);//число локальных переменных

	cRunContext.m_lParamCount = array3;//число формальных параметров
	cRunContext.m_lStart = lCodeLine;
	cRunContext.m_compileContext = reinterpret_cast<compileContext_t*>(m_pByteCode->m_aCodeList[cRunContext.m_lStart].m_param1.m_nArray);

	//загружаем параметры
	memcpy(&cRunContext.m_pRefLocVars[0], &ppParams[0], std::min(lSizeArray, cRunContext.m_lParamCount) * sizeof(CValue*));

	//выполним произвольный код 
	return Execute(&cRunContext, false);
}

long CProcUnit::FindProp(const wxString& propName) const
{
	auto foundedIt = std::find_if(m_pByteCode->m_aExportVarList.begin(), m_pByteCode->m_aExportVarList.end(),
		[propName](const std::pair<const wxString, long>& pair) {
			return StringUtils::CompareString(propName, pair.first);
		}
	);
	if (foundedIt != m_pByteCode->m_aExportVarList.end())
		return (long)foundedIt->second - 1;
	return wxNOT_FOUND;
}

bool CProcUnit::SetPropVal(const long lPropNum, const CValue& varPropVal)//установка атрибута
{
	*m_cCurContext.m_pRefLocVars[lPropNum] = varPropVal;
	return true;
}

bool CProcUnit::SetPropVal(const wxString& propName, const CValue& varPropVal)//установка атрибута
{
	long lPropNum = FindProp(propName);
	if (lPropNum != wxNOT_FOUND) {
		*m_cCurContext.m_pRefLocVars[lPropNum] = varPropVal;
	}
	else {
		long lPropPos = m_cCurContext.GetLocalCount();
		m_cCurContext.SetLocalCount(lPropPos + 1);
		m_cCurContext.m_cLocVars[lPropPos] = CValue(propName);
		*m_cCurContext.m_pRefLocVars[lPropPos] = varPropVal;
	}
	return true;
}

bool CProcUnit::GetPropVal(const long lPropNum, CValue& pvarPropVal) //значение атрибута
{
	pvarPropVal = m_cCurContext.m_pRefLocVars[lPropNum];
	return true;
}

bool CProcUnit::GetPropVal(const wxString& propName, CValue& pvarPropVal) //установка атрибута
{
	const long lPropNum = FindProp(propName);
	if (lPropNum != wxNOT_FOUND) {
		pvarPropVal = m_cCurContext.m_pRefLocVars[lPropNum];
		return true;
	}
	return false;
}

CValue CProcUnit::Evaluate(const wxString& strCode, CRunContext* pRunContext, bool сompileBlock, bool* bError)
{
	if (pRunContext == NULL)
		pRunContext = CProcUnit::GetCurrentRunContext();
	if (strCode.IsEmpty() ||
		pRunContext == NULL)
		return CValue();
	bool isSimpleMode = CTranslateError::IsSimpleMode();
	if (!isSimpleMode)
		CTranslateError::SetSimpleMode(true);
	auto foundedIt = std::find_if(pRunContext->m_stringEvals.begin(), pRunContext->m_stringEvals.end(),
		[strCode](std::pair<const wxString, CProcUnit*>& pair) {
			return StringUtils::CompareString(strCode, pair.first);
		}
	);
	CProcUnit* pRunEval = NULL;
	if (foundedIt == pRunContext->m_stringEvals.end()) { //еще не было компиляции такого текста
		CProcUnit* pSimpleRun = new CProcUnit;
		CCompileModule* compileModule = new CCompileModule;
		if (!pSimpleRun->CompileExpression(strCode, pRunContext, *compileModule, сompileBlock)) {
			if (bError)
				*bError = true;
			//delete from memory
			delete pSimpleRun;
			delete compileModule;
			if (!isSimpleMode)
				CTranslateError::SetSimpleMode(false);
			return CTranslateError::GetLastError();
		}
		//все ОК
		pRunContext->m_stringEvals.insert_or_assign(
			StringUtils::MakeUpper(strCode), pSimpleRun
		);
		pRunEval = pSimpleRun;
	}
	else {
		pRunEval = foundedIt->second;
	}
	//Запускаем
	bool bDelta = false;
	compileContext_t* compileContext = pRunContext->m_compileContext;
	wxASSERT(compileContext);
	CCompileModule* compileModule = compileContext->m_compileModule;
	wxASSERT(compileModule);
	if (compileModule->m_bExpressionOnly) {
		compileContext_t* curContext = compileContext;
		CCompileModule* curModule = compileModule;
		while (curContext != NULL) {
			if (!curModule->m_bExpressionOnly)
				break;
			curContext = curContext->m_parentContext;
			curModule = compileModule->GetParent();
		}
		if (curContext && curContext->m_nReturn == RETURN_NONE)
			bDelta = true;
	}
	else {
		if (compileContext->m_nReturn == RETURN_NONE)
			bDelta = true;
	}
	CValue cRetValue;
	try {
		cRetValue = pRunEval->Execute(&pRunEval->m_cCurContext, bDelta);
	}
	catch (const CTranslateError*) {
		if (bError)
			*bError = true;
		if (!isSimpleMode)
			CTranslateError::SetSimpleMode(false);
		return CTranslateError::GetLastError();
	}
	if (bError)
		*bError = false;
	if (!isSimpleMode)
		CTranslateError::SetSimpleMode(false);
	return cRetValue;
}

bool CProcUnit::CompileExpression(const wxString& strCode, CRunContext* pRunContext, CCompileModule& cModule, bool bCompileBlock)
{
	cModule.Load(strCode);

	byteCode_t* byteCode = pRunContext->GetByteCode();
	//задаем в качестве родителей контекст вызова выражения
	if (byteCode != NULL) {
		cModule.m_cByteCode.m_parent = byteCode;
		cModule.m_parent = byteCode->m_compileModule;
		cModule.m_cContext.m_parentContext = pRunContext->m_compileContext;
	}

	cModule.m_bExpressionOnly = true;
	cModule.m_cContext.m_nFindLocalInParent = 2;
	cModule.m_nCurrentCompile = wxNOT_FOUND;

	try {
		if (!cModule.PrepareLexem())
			return false;
	}
	catch (...) {
		return false;
	}

	cModule.m_cByteCode.m_compileModule = &cModule;

	//дорабатываем массив байт-кодов для возвращения результата выражения
	byteRaw_t code;
	code.m_nOper = OPER_RET;

	try {
		if (bCompileBlock) cModule.CompileBlock();
		else code.m_param1 = cModule.GetExpression();
	}
	catch (...)
	{
		return false;
	}

	if (!bCompileBlock) {
		cModule.m_cByteCode.m_aCodeList.push_back(code);
	}

	byteRaw_t code2;
	code2.m_nOper = OPER_END;

	cModule.m_cByteCode.m_aCodeList.push_back(code2);
	cModule.m_cByteCode.m_lVarCount = cModule.m_cContext.m_cVariables.size();

	//признак завершенности компилирования
	cModule.m_cByteCode.m_bCompile = true;

	//Проецируем в памяти
	SetParent(pRunContext->m_procUnit);

	try {
		compileContext_t* compileContext = pRunContext->m_compileContext;
		wxASSERT(compileContext);
		CCompileModule* compleModule = compileContext->m_compileModule;
		wxASSERT(compleModule);
		Execute(cModule.m_cByteCode, false);
		if (compleModule->m_bExpressionOnly) {
			int nParentNumber = 1;
			compileContext_t* curContext = compileContext;
			CCompileModule* curModule = compleModule;
			while (curContext != NULL) {
				if (curModule->m_bExpressionOnly)
					nParentNumber++;
				curContext = curContext->m_parentContext; curModule = compleModule->GetParent();
			}
			m_pppArrayList[nParentNumber] = pRunContext->m_procUnit->m_pppArrayList[nParentNumber - 1];
		}
		else {
			m_pppArrayList[1] = pRunContext->m_pRefLocVars;
		}
		m_cCurContext.m_compileContext = &cModule.m_cContext;
	}
	catch (...) {
		return false;
	}

	return true;
}

//*************************************************************************************************
//*                                        RunContext                                             *
//*************************************************************************************************

CRunContext::~CRunContext() {
	if (m_lVarCount > MAX_STATIC_VAR) {
		delete[]m_pLocVars;
		delete[]m_pRefLocVars;
	}
	//удаление m_stringEvals
	for (auto it = m_stringEvals.begin(); it != m_stringEvals.end(); it++) {
		CProcUnit* procUnit = static_cast<CProcUnit*>(it->second);
		if (procUnit) {
			CCompileModule* compileModule = procUnit->m_pByteCode->m_compileModule;
			delete procUnit; delete compileModule;
		}
	}
}

byteCode_t* CRunContext::GetByteCode() const {
	return m_procUnit ?
		m_procUnit->GetByteCode() : NULL;
}