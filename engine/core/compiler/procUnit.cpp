////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, 2С-team
//	Description : Processor unit 
////////////////////////////////////////////////////////////////////////////

#include "procUnit.h"
#include "debugger/debugServer.h"
#include "systemObjects.h"
#include "utils/stringUtils.h"

#define CurCode	m_pByteCode->m_aCodeList[nCodeLine]

#define Index1	CurCode.m_param1.m_nIndex
#define Index2	CurCode.m_param2.m_nIndex
#define Index3	CurCode.m_param3.m_nIndex
#define Index4	CurCode.m_param4.m_nIndex

#define Array1	CurCode.m_param1.m_nArray
#define Array2	CurCode.m_param2.m_nArray
#define Array3	CurCode.m_param3.m_nArray
#define Array4	CurCode.m_param4.m_nArray

#define LocVariable1 *m_pRefLocVars[Index1]
#define LocVariable2 *m_pRefLocVars[Index2]
#define LocVariable3 *m_pRefLocVars[Index3]
#define LocVariable4 *m_pRefLocVars[Index4]

#define Variable(x) (Array##x<=0?*pRefLocVars[Index##x]:(Array##x==DEF_VAR_CONST?m_pByteCode->m_aConstList[Index##x]:*m_pppArrayList[Array##x+nDelta][Index##x]))

#define Variable1 Variable(1)
#define Variable2 Variable(2)
#define Variable3 Variable(3)
#define Variable4 Variable(4)

//**************************************************************************************************************
//*                                          static procUnit func                                              *
//**************************************************************************************************************

static CProcUnit *m_pCurrentRunModule;

CProcUnit *CProcUnit::GetCurrentRunModule()
{
	return m_pCurrentRunModule;
}

void CProcUnit::ClearCurrentRunModule()
{
	m_pCurrentRunModule = NULL;
}

static std::vector <CRunContext *> aRunContext; //список исполняемых кодов модулей

void CProcUnit::AddRunContext(CRunContext *runContext)
{
	aRunContext.push_back(runContext);
}

unsigned int CProcUnit::GetCountRunContext()
{
	return aRunContext.size();
}

CRunContext *CProcUnit::GetPrevRunContext()
{
	if (aRunContext.size() < 2)
		return NULL;
	return aRunContext[aRunContext.size() - 2];
}

CRunContext *CProcUnit::GetCurrentRunContext()
{
	if (!aRunContext.size())
		return NULL;
	return aRunContext.back();
}

CRunContext *CProcUnit::GetRunContext(unsigned int idx)
{
	if (aRunContext.size() < idx)
		return NULL;
	return aRunContext[idx];
}

void CProcUnit::BackRunContext()
{
	aRunContext.pop_back();
}

CByteCode *CProcUnit::GetCurrentByteCode()
{
	CRunContext *pContext = GetCurrentRunContext();
	if (pContext) return pContext->GetByteCode();
	return NULL;
}

//**************************************************************************************************************
//*                                              support error place                                           *
//**************************************************************************************************************

struct CErrorPlace
{
	int nLine;

	CByteCode* m_pByteCode;
	CByteCode* pSkipByteCode;

	CErrorPlace() { Reset(); };

	bool IsEmpty() { return nLine == wxNOT_FOUND; }

	void Reset()
	{
		m_pByteCode = NULL;
		pSkipByteCode = NULL;
		nLine = wxNOT_FOUND;
	};
};

static CErrorPlace s_errorPlace;

void CProcUnit::Raise()
{
	s_errorPlace.Reset(); //инициализация места ошибки
	s_errorPlace.pSkipByteCode = CProcUnit::GetCurrentByteCode(); //возвращаемся назад в вызываемый модуль (если он есть)
}

//**************************************************************************************************************
//*                                              stack support                                                 *
//**************************************************************************************************************

inline void BeginByteCode(CRunContext *pCode)
{
	CProcUnit::AddRunContext(pCode);
}

inline bool EndByteCode()
{
	int n = CProcUnit::GetCountRunContext();
	if (n > 0) CProcUnit::BackRunContext();
	n--;
	if (n <= 0) return false;
	else return true;
}

//Обнуление стека
inline void ResetByteCode()
{
	while (EndByteCode());
}

static int s_nRecCount = 0; //контроль зацикливания

struct CStackGuard
{
	CRunContext *pCurrentContext;

	CStackGuard(CRunContext *pContext)
	{
		if (s_nRecCount > MAX_REC_COUNT) {//критическая ошибка	
			wxString sError = "";
			for (unsigned int i = 0; i < CProcUnit::GetCountRunContext(); i++) {
				CRunContext *pLastContext = CProcUnit::GetRunContext(i);
				wxASSERT(pLastContext);
				CByteCode *m_pByteCode = pLastContext->GetByteCode();
				wxASSERT(m_pByteCode);
				sError += wxString::Format("\n%s (#line %d)",
					m_pByteCode->m_sModuleName,
					m_pByteCode->m_aCodeList[pLastContext->m_nCurLine].m_nNumberLine + 1
				);
			}
			CTranslateError::Error(_("Number of recursive calls exceeded the maximum allowed value!\nCall stack :") + sError);
		}

		s_nRecCount++;
		pCurrentContext = pContext;
		BeginByteCode(pContext);
	};

	~CStackGuard()
	{
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
	CValue Val;\
	Operation(Val,cValue2,cValue3);\
	cValue1.SetValue(Val);\
	return;\
}\
if(cValue1.m_typeClass==eValueTypes::TYPE_REFFER)\
	cValue1.m_pRef->DecrRef();\

//Функции для быстрой работы с типом CValue
inline void AddValue(CValue &cValue1, const CValue &cValue2, const CValue &cValue3)
{
	CHECK_READONLY(AddValue);

	cValue1.m_typeClass = cValue2.GetType();

	if (cValue1.m_typeClass == eValueTypes::TYPE_NUMBER) {
		cValue1.m_fData = cValue2.GetNumber() + cValue3.GetNumber();
	}
	else if (cValue1.m_typeClass == eValueTypes::TYPE_DATE)
	{
		if (cValue3.m_typeClass == eValueTypes::TYPE_DATE) { //дата + дата -> число
			cValue1.m_typeClass = eValueTypes::TYPE_NUMBER;
			cValue1.m_fData = cValue2.GetDate() + cValue3.GetDate();
		}
		else {
			cValue1.m_dData = cValue2.m_dData + cValue3.GetDate();
		}
	}
	else
	{
		cValue1.m_typeClass = eValueTypes::TYPE_STRING; cValue1.m_sData = cValue2.GetString() + cValue3.GetString();
	}
}

inline void SubValue(CValue &cValue1, const CValue &cValue2, const CValue &cValue3)
{
	CHECK_READONLY(SubValue);

	cValue1.m_typeClass = cValue2.GetType();

	if (cValue1.m_typeClass == eValueTypes::TYPE_NUMBER)
	{
		cValue1.m_fData = cValue2.GetNumber() - cValue3.GetNumber();
	}
	else if (cValue1.m_typeClass == eValueTypes::TYPE_DATE)
	{
		if (cValue3.m_typeClass == eValueTypes::TYPE_DATE) { //дата - дата -> число

			cValue1.m_typeClass = eValueTypes::TYPE_NUMBER;
			cValue1.m_fData = cValue2.GetDate() - cValue3.GetDate();
		}
		else {
			cValue1.m_dData = cValue2.m_dData - cValue3.GetDate();
		}
	}
	else
	{
		wxString sTypeValue = cValue2.GetTypeString();
		CTranslateError::Error(_("Subtraction operation cannot be applied for this type (%s)"), sTypeValue.wc_str());
	}
}

inline void MultValue(CValue &cValue1, const CValue &cValue2, const CValue &cValue3)
{
	CHECK_READONLY(MultValue);

	cValue1.m_typeClass = cValue2.GetType();

	if (cValue1.m_typeClass == eValueTypes::TYPE_NUMBER)
	{
		cValue1.m_fData = cValue2.GetNumber() * cValue3.GetNumber();
	}
	else if (cValue1.m_typeClass == eValueTypes::TYPE_DATE)
	{
		if (cValue3.m_typeClass == eValueTypes::TYPE_DATE)//дата * дата -> число
		{
			cValue1.m_typeClass = eValueTypes::TYPE_NUMBER;
			cValue1.m_fData = cValue2.GetDate() * cValue3.GetDate();
		}
		else cValue1.m_dData = cValue2.m_dData * cValue3.GetDate();
	}
	else
	{
		wxString sTypeValue = cValue2.GetTypeString();
		CTranslateError::Error(_("Multiplication operation cannot be applied for this type (%s)"), sTypeValue.wc_str());
	}
}

inline void DivValue(CValue &cValue1, const CValue &cValue2, const CValue &cValue3)
{
	CHECK_READONLY(DivValue);

	cValue1.m_typeClass = cValue2.GetType();

	if (cValue1.m_typeClass == eValueTypes::TYPE_NUMBER)
	{
		number_t flNumber3 = cValue3.GetNumber();
		if (flNumber3.IsZero()) CTranslateError::Error(_("Divide by zero"));
		cValue1.m_fData = cValue2.GetNumber() / flNumber3;
	}
	else
	{
		wxString sTypeValue = cValue2.GetTypeString();
		CTranslateError::Error(_("Division operation cannot be applied for this type (%s)"), sTypeValue.wc_str());
	};
}

inline void ModValue(CValue &cValue1, const CValue &cValue2, const CValue &cValue3)
{
	CHECK_READONLY(ModValue);

	cValue1.m_typeClass = cValue2.GetType();

	if (cValue1.m_typeClass == eValueTypes::TYPE_NUMBER)
	{
		ttmath::Int<TTMATH_BITS(128)> val128_2, val128_3;
		number_t flNumber3 = cValue3.GetNumber(); flNumber3.ToInt(val128_3);
		if (val128_3.IsZero())
			CTranslateError::Error(_("Divide by zero"));
		number_t flNumber2 = cValue2.GetNumber(); flNumber2.ToInt(val128_2);
		cValue1.m_fData = val128_2 % val128_3;
	}
	else
	{
		wxString sTypeValue = cValue2.GetTypeString();
		CTranslateError::Error(_("Modulo operation cannot be applied for this type (%s)"), sTypeValue.wc_str());
	}
}

//Реализация операторов сравнения
inline void CompareValueGT(CValue &cValue1, const CValue &cValue2, const CValue &cValue3)
{
	CHECK_READONLY(CompareValueGT);

	cValue1.m_typeClass = eValueTypes::TYPE_BOOLEAN;
	cValue1.m_bData = cValue2.CompareValueGT(cValue3);
}

inline void CompareValueGE(CValue &cValue1, const CValue &cValue2, const CValue &cValue3)
{
	CHECK_READONLY(CompareValueGT);

	cValue1.m_typeClass = eValueTypes::TYPE_BOOLEAN;
	cValue1.m_bData = cValue2.CompareValueGT(cValue3);
}

inline void CompareValueLS(CValue &cValue1, const CValue &cValue2, const CValue &cValue3)
{
	CHECK_READONLY(CompareValueLS);

	cValue1.m_typeClass = eValueTypes::TYPE_BOOLEAN;
	cValue1.m_bData = cValue2.CompareValueLS(cValue3);
}

inline void CompareValueLE(CValue &cValue1, const CValue &cValue2, const CValue &cValue3)
{
	CHECK_READONLY(CompareValueLE);

	cValue1.m_typeClass = eValueTypes::TYPE_BOOLEAN;
	cValue1.m_bData = cValue2.CompareValueLE(cValue3);
}

inline void CompareValueEQ(CValue &cValue1, const CValue &cValue2, const CValue &cValue3)
{
	CHECK_READONLY(CompareValueEQ);

	cValue1.m_typeClass = eValueTypes::TYPE_BOOLEAN;
	cValue1.m_bData = cValue2.CompareValueEQ(cValue3);
}

inline void CompareValueNE(CValue &cValue1, const CValue &cValue2, const CValue &cValue3)
{
	CHECK_READONLY(CompareValueNE);

	cValue1.m_typeClass = eValueTypes::TYPE_BOOLEAN;
	cValue1.m_bData = cValue2.CompareValueNE(cValue3);
}

inline void CopyValue(CValue &cValue1, CValue &cValue2)
{
	if (&cValue1 == &cValue2) return;

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
	case eValueTypes::TYPE_NULL: break;
	case eValueTypes::TYPE_BOOLEAN: cValue1.m_bData = cValue2.m_bData; break;
	case eValueTypes::TYPE_NUMBER: cValue1.m_fData = cValue2.m_fData; break;
	case eValueTypes::TYPE_STRING: cValue1.m_sData = cValue2.m_sData; break;
	case eValueTypes::TYPE_DATE: cValue1.m_dData = cValue2.m_dData; break;
	case eValueTypes::TYPE_REFFER: cValue1.m_pRef = cValue2.m_pRef; cValue1.m_pRef->IncrRef(); break;
	case eValueTypes::TYPE_MODULE:
	case eValueTypes::TYPE_OLE:
	case eValueTypes::TYPE_ENUM:
	case eValueTypes::TYPE_VALUE:
		cValue1.m_typeClass = eValueTypes::TYPE_REFFER;
		cValue1.m_pRef = &cValue2; cValue1.m_pRef->IncrRef(); break;
	default: cValue1.m_typeClass = eValueTypes::TYPE_EMPTY;
	}
}

inline bool IsEmptyValue(const CValue &cValue1)
{
	return cValue1.IsEmpty();
}

#define IsHasValue(cValue1) (!IsEmptyValue(cValue1))

inline void SetTypeBoolean(CValue &cValue1, bool bValue)
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

inline void SetTypeNumber(CValue &cValue1, const number_t &fValue)
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

#define CheckAndError(Variable,sName)\
{\
	if(Variable.m_typeClass!=eValueTypes::TYPE_REFFER)\
		CTranslateError::Error(_("No attribute or method found '%s' - a variable is not an aggregate object"), sName.wc_str());\
	else\
		CTranslateError::Error(_("Aggregate object field not found '%s'"), sName.wc_str());\
}

//Индексные массивы
inline void SetArrayValue(CValue &cValue1, const CValue &cValue2, CValue &cValue3)
{
	cValue1.SetAt(cValue2, cValue3);
}

inline void GetArrayValue(CValue &cValue1, CValue &cValue2, const CValue &cValue3)
{
	CopyValue(cValue1, cValue2.GetAt(cValue3));
}

inline CValue GetValue(CValue &cValue1)
{
	if (cValue1.m_bReadOnly
		&& cValue1.m_typeClass != eValueTypes::TYPE_REFFER)
	{
		CValue cVal;
		CopyValue(cVal, cValue1);
		return cVal;
	}

	return cValue1;
}

//////////////////////////////////////////////////////////////////////
//						Construction/Destruction                    //
//////////////////////////////////////////////////////////////////////

CProcUnit::CProcUnit() : m_pppArrayList(NULL), m_ppArrayCode(NULL), m_pByteCode(NULL), m_nAutoDeleteParent(0) {}

void CProcUnit::Clear()
{
	if (m_pppArrayList) delete[]m_pppArrayList;
	if (m_ppArrayCode) delete m_ppArrayCode;

	if (m_pCurrentRunModule == this)
		m_pCurrentRunModule = NULL;

	m_nAutoDeleteParent = 0;

	m_pppArrayList = NULL;
	m_ppArrayCode = NULL;
	m_pByteCode = NULL;

	m_aParent.clear();
}

CProcUnit::~CProcUnit()
{
	Clear();
}

void CProcUnit::SetParent(CProcUnit *pSetParent)
{
	m_aParent.clear();
	if (!pSetParent)
		return;
	unsigned int nCount =
		pSetParent->m_aParent.size();
	m_aParent.push_back(pSetParent);
	for (unsigned int i = 1; i <= nCount; i++) {
		m_aParent.push_back(pSetParent->m_aParent[i - 1]);
	}
}

CProcUnit *CProcUnit::GetParent(unsigned int nLevel)
{
	if (nLevel >= m_aParent.size()) {
		wxASSERT(nLevel == 0);
		return NULL;
	}
	else {
		wxASSERT(nLevel == m_aParent.size() - 1 || m_aParent[nLevel]);
		return m_aParent[nLevel];
	}
}

unsigned int CProcUnit::GetParentCount()
{
	return m_aParent.size();
}

#include "definition.h"

CValue CProcUnit::Execute(CRunContext *pContext, int nDelta)
{
#ifdef _DEBUG
	if (!pContext) {
		CTranslateError::Error(_("No execution context defined!"));
		if (!m_pByteCode) {
			CTranslateError::Error(_("No execution code set!"));
		}
	}
#endif

	pContext->SetProcUnit(this);

	CValue cRetValue;

	// if is not initializer then set no return value
	auto foundedFunction = std::find_if(m_pByteCode->m_aFuncList.begin(), m_pByteCode->m_aFuncList.end(),
		[pContext](std::pair<wxString, unsigned int> pair)
	{
		return pContext->m_nStart == (pair.second - 1);
	});

	if (foundedFunction != m_pByteCode->m_aFuncList.end()) {
		cRetValue = new CValueNoRet(foundedFunction->first);
	}

	CStackGuard cStack(pContext);

	CValue *pLocVars = pContext->m_pLocVars;
	CValue **pRefLocVars = pContext->m_pRefLocVars;

	CByte *pCodeList = m_pByteCode->m_aCodeList.data();

	int nCodeLine = pContext->m_nStart;
	int nFinish = m_pByteCode->m_aCodeList.size();
	int nPrevLine = wxNOT_FOUND;

	std::vector<wxPoint> aTryList;

start_label:

	try { //медленнее на 2-3% на каждый вложенный модуль
		while (nCodeLine < nFinish) {
			if (!CTranslateError::IsSimpleMode()) {
				pContext->m_nCurLine = nCodeLine;
				m_pCurrentRunModule = this;
			}

			//enter in debugger
			if (!CTranslateError::IsSimpleMode()) {
				debugServer->EnterDebugger(pContext, CurCode, nPrevLine);
			}

			switch (CurCode.m_nOper)
			{
			case OPER_CONST: CopyValue(Variable1, m_pByteCode->m_aConstList[Index2]); break;
			case OPER_CONSTN: SetTypeNumber(Variable1, Index2); break;
			case OPER_ADD: AddValue(Variable1, Variable2, Variable3); break;
			case OPER_SUB: SubValue(Variable1, Variable2, Variable3); break;
			case OPER_DIV: DivValue(Variable1, Variable2, Variable3); break;
			case OPER_MOD: ModValue(Variable1, Variable2, Variable3); break;
			case OPER_MULT: MultValue(Variable1, Variable2, Variable3); break;
			case OPER_LET: Variable2.CheckValue(); CopyValue(Variable1, Variable2); break;
			case OPER_INVERT: SetTypeNumber(Variable1, -Variable2.GetNumber()); break;
			case OPER_NOT: SetTypeBoolean(Variable1, IsEmptyValue(Variable2)); break;
			case OPER_AND: {if (IsHasValue(Variable2) && IsHasValue(Variable3))
				SetTypeBoolean(Variable1, true); else SetTypeBoolean(Variable1, false);
			} break;
			case OPER_OR: { if (IsHasValue(Variable2) || IsHasValue(Variable3))
				SetTypeBoolean(Variable1, true); else SetTypeBoolean(Variable1, false);
			} break;
			case OPER_EQ: CompareValueEQ(Variable1, Variable2, Variable3); break;
			case OPER_NE: CompareValueNE(Variable1, Variable2, Variable3); break;
			case OPER_GT: CompareValueGT(Variable1, Variable2, Variable3); break;
			case OPER_LS: CompareValueLS(Variable1, Variable2, Variable3); break;
			case OPER_GE: CompareValueGE(Variable1, Variable2, Variable3); break;
			case OPER_LE: CompareValueLE(Variable1, Variable2, Variable3); break;
			case OPER_IF: {if (IsEmptyValue(Variable1))
				nCodeLine = Index2 - 1;
			} break;
			case OPER_FOR: {
				if (Variable1.m_typeClass != eValueTypes::TYPE_NUMBER) {
					CTranslateError::Error(_("Only variables with type can be used to organize the loop \"number\""));
				}
				if (Variable1.m_fData == Variable2.m_fData)
					nCodeLine = Index3 - 1;
			} break;
			case OPER_FOREACH: {
				if (!Variable2.HasIterator()) {
					CTranslateError::Error(_("Undefined value iterator"));
				}
				if (Variable3.m_typeClass != eValueTypes::TYPE_NUMBER) {
					CopyValue(Variable3, CValue(0));
				}
				unsigned int m_nIndex = Variable3.ToUInt();
				if (m_nIndex < Variable2.GetItSize()) {
					CopyValue(Variable1, Variable2.GetItAt(m_nIndex));
				}
				else {
					Variable3 = 0; nCodeLine = Index4 - 1;
				}
			} break;
			case OPER_NEXT: {if (Variable1.m_typeClass == eValueTypes::TYPE_NUMBER) {
				Variable1.m_fData++;
			} nCodeLine = Index2 - 1;
			} break;
			case OPER_NEXT_ITER: {
				Variable1.m_fData++; nCodeLine = Index2 - 1;
			} break;
			case OPER_ITER: {
				if (IsHasValue(Variable2)) {
					CopyValue(Variable1, Variable3);
				}
				else {
					CopyValue(Variable1, Variable4);
				}
			}  break;
			case OPER_NEW: {

				CValue *pRetValue = &Variable1;
				wxString sObjectName = m_pByteCode->m_aConstList[Index2].m_sData;

				CRunContextSmall cRunContext(Array2);
				cRunContext.m_nParamCount = Array2;

				//загружаем параметры
				for (unsigned int i = 0; i < cRunContext.m_nParamCount; i++)
				{
					nCodeLine++;
					if (Index1 >= 0)
					{
						if (Variable1.m_bReadOnly&&Variable1.m_typeClass != eValueTypes::TYPE_REFFER) {
							CopyValue(cRunContext.m_pLocVars[i], Variable1);
						}
						else {
							cRunContext.m_pRefLocVars[i] = &Variable1;
						}
					}
				}

				CopyValue(*pRetValue, CValue::CreateObject(sObjectName, cRunContext.m_nParamCount > 0 ? cRunContext.m_pRefLocVars : NULL));
			} break;
			case OPER_SET_A: {//установка атрибута

				wxString sAttributeName = m_pByteCode->m_aConstList[Index2].m_sData;
				int nAttr = Variable1.FindAttribute(sAttributeName);
				if (nAttr < 0) CheckAndError(Variable1, sAttributeName);
				attributeArg_t aParams(nAttr, sAttributeName);
				Variable1.SetAttribute(aParams, GetValue(Variable3));
			} break;
			case OPER_GET_A://получение атрибута
			{
				CValue *pRetValue = &Variable1;
				CValue *pVariable2 = &Variable2;
				wxString sAttributeName = m_pByteCode->m_aConstList[Index3].m_sData;
				int nAttr = Variable2.FindAttribute(sAttributeName);
				if (nAttr < 0) CheckAndError(Variable2, sAttributeName);
				attributeArg_t aParams(nAttr, sAttributeName);
				CValue vRet = Variable2.GetAttribute(aParams);

				if (vRet.m_typeClass == eValueTypes::TYPE_REFFER) *pRetValue = vRet;
				else CopyValue(*pRetValue, vRet); break;
			}
			case OPER_CALL_M://вызов метода
			{
				CValue *pRetValue = &Variable1;
				CValue *pVariable2 = &Variable2;
				int nMethod = wxNOT_FOUND;
				//оптимизация вызовов
				CValue *pStorageValue = reinterpret_cast<CValue *>(Array4);
				if (pStorageValue && pStorageValue == pVariable2->GetRef())//ранее были вызовы
				{
					nMethod = Index4;
#ifdef _DEBUG
					nMethod = pVariable2->FindMethod(m_pByteCode->m_aConstList[Index3].m_sData);
					if (nMethod != Index4) {
						CTranslateError::Error(_("Error value %d must %d (It is recommended to turn off method optimization)"), Index4, nMethod);
					}
#endif
				}
				else//не было вызовов
				{
					nMethod = pVariable2->FindMethod(m_pByteCode->m_aConstList[Index3].m_sData);
					Index4 = nMethod;
#if defined(_LP64) || defined(__LP64__) || defined(__arch64__) || defined(_WIN64)
					Array4 = reinterpret_cast<wxLongLong_t>(pVariable2->GetRef());
#else 
					Array4 = reinterpret_cast<int>(pVariable2->GetRef());
#endif
				}
				wxString sFuncName = m_pByteCode->m_aConstList[Index3].m_sData;

				if (nMethod < 0) {
					CheckAndError(Variable2, sFuncName);
				}

				CRunContextSmall cRunContext(std::max(Array3, MAX_STATIC_VAR));
				cRunContext.m_nParamCount = Array3;

				//загружаем параметры
				for (unsigned int i = 0; i < cRunContext.m_nParamCount; i++) {
					nCodeLine++;
					if (Index1 >= 0) {
						if (Variable1.m_bReadOnly && Variable1.m_typeClass != eValueTypes::TYPE_REFFER) {
							CopyValue(cRunContext.m_pLocVars[i], Variable1);
						}
						else {
							cRunContext.m_pRefLocVars[i] = &Variable1;
						}
					}
				}

				methodArg_t methodParams(cRunContext.m_pRefLocVars, cRunContext.m_nParamCount, nMethod, sFuncName);
				CopyValue(*pRetValue, pVariable2->Method(methodParams)); methodParams.CheckParams(); break;
			}
			case OPER_CALL://вызов обычной функции
			{
				int nModuleNumber = Array2;

				CRunContext cRunContext(Index3);

				cRunContext.m_nStart = Index2;
				cRunContext.m_nParamCount = Array3;

				CByteCode *pLocalByteCode = m_ppArrayCode[nModuleNumber]->m_pByteCode;

				CByte	*pCodeList2 = pLocalByteCode->m_aCodeList.data();
				cRunContext.m_compileContext = reinterpret_cast<CCompileContext *>(pCodeList2[cRunContext.m_nStart].m_param1.m_nArray);

				CValue *pRetValue = &Variable1;

				//загружаем параметры
				for (unsigned int i = 0; i < cRunContext.m_nParamCount; i++)
				{
					nCodeLine++;
					if (CurCode.m_nOper == OPER_SETCONST) {
						CopyValue(cRunContext.m_pLocVars[i], pLocalByteCode->m_aConstList[Index1]);
					}
					else {
						if (Variable1.m_bReadOnly || Index2 == 1) {//передача параметра по значению
							CopyValue(cRunContext.m_pLocVars[i], Variable1);
						}
						else {
							cRunContext.m_pRefLocVars[i] = &Variable1;
						}
					}
				}

				CopyValue(*pRetValue, m_ppArrayCode[nModuleNumber]->Execute(&cRunContext, 0)); break;
			}
			case OPER_SET_ARRAY: SetArrayValue(Variable1, Variable2, GetValue(Variable3)); break; //установка значения массива
			case OPER_GET_ARRAY: GetArrayValue(Variable1, Variable2, Variable3); break; //получение значения массива			
			case OPER_GOTO: case OPER_ENDTRY: {
				int nNewLine = Index1;
				int n = aTryList.size() - 1;
				if (n >= 0) {
					int nLineStart = aTryList[n].x;
					int nLineEnd = aTryList[n].y;
					if (nNewLine >= nLineEnd || nNewLine <= nLineStart) {
						aTryList.resize(n);//выход из поля видимости  try..catch
					}
				}
				nCodeLine = nNewLine - 1;//т.к. потом добавим 1

			} break;
			case OPER_TRY: aTryList.emplace_back(nCodeLine, Index1); break; //переход при ошибке
			case OPER_RAISE: CTranslateError::Error(CTranslateError::GetLastError()); break;
			case OPER_RAISE_T: CTranslateError::Error(m_pByteCode->m_aConstList[Index1].GetString()); break;
			case OPER_RET: if (Index1 != DEF_VAR_NORET) { Variable1.CheckValue(); CopyValue(cRetValue, Variable1); }
			case OPER_ENDFUNC:
			case OPER_END: nCodeLine = nFinish; break; //выход
			case OPER_FUNC: if (nDelta == 1) { while (nCodeLine < nFinish) { if (CurCode.m_nOper != OPER_ENDFUNC) { nCodeLine++; } else break; } } break; //это начальный запуск - пропускаем тела процедур и функций
			case OPER_SET_TYPE: Variable1.SetType(CValue::GetVTByID(Array2)); break;
				//Операторы работы с типизированными данными
				//NUMBER
			case OPER_ADD + TYPE_DELTA1: Variable1.m_fData = Variable2.m_fData + Variable3.m_fData; break;
			case OPER_SUB + TYPE_DELTA1: Variable1.m_fData = Variable2.m_fData - Variable3.m_fData; break;
			case OPER_DIV + TYPE_DELTA1: if (Variable3.m_fData.IsZero()) { CTranslateError::Error(_("Divide by zero")); } Variable1.m_fData = Variable2.m_fData / Variable3.m_fData; break;
			case OPER_MOD + TYPE_DELTA1: if (Variable3.m_fData.IsZero()) { CTranslateError::Error(_("Divide by zero")); } Variable1.m_fData = Variable2.m_fData.Round() % Variable3.m_fData.Round(); break;
			case OPER_MULT + TYPE_DELTA1: Variable1.m_fData = Variable2.m_fData*Variable3.m_fData; break;
			case OPER_LET + TYPE_DELTA1: Variable1.m_fData = Variable2.m_fData; break;
			case OPER_NOT + TYPE_DELTA1: Variable1.m_fData = Variable2.m_fData.IsZero(); break;
			case OPER_INVERT + TYPE_DELTA1: Variable1.m_fData = -Variable2.m_fData; break;
			case OPER_EQ + TYPE_DELTA1: Variable1.m_fData = (Variable2.m_fData == Variable3.m_fData); break;
			case OPER_NE + TYPE_DELTA1: Variable1.m_fData = (Variable2.m_fData != Variable3.m_fData); break;
			case OPER_GT + TYPE_DELTA1: Variable1.m_fData = (Variable2.m_fData > Variable3.m_fData); break;
			case OPER_LS + TYPE_DELTA1: Variable1.m_fData = (Variable2.m_fData < Variable3.m_fData); break;
			case OPER_GE + TYPE_DELTA1: Variable1.m_fData = (Variable2.m_fData >= Variable3.m_fData); break;
			case OPER_LE + TYPE_DELTA1: Variable1.m_fData = (Variable2.m_fData <= Variable3.m_fData); break;
			case OPER_SET_ARRAY + TYPE_DELTA1:	SetArrayValue(Variable1, Variable2, GetValue(Variable3)); break;//установка значения массива
			case OPER_GET_ARRAY + TYPE_DELTA1: GetArrayValue(Variable1, Variable2, Variable3); break; //получение значения массива	
			case OPER_IF + TYPE_DELTA1: if (Variable1.m_fData.IsZero()) nCodeLine = Index2 - 1; break;
				//STRING
			case OPER_ADD + TYPE_DELTA2: Variable1.m_sData = Variable2.m_sData + Variable3.m_sData; break;
			case OPER_LET + TYPE_DELTA2: Variable1.m_sData = Variable2.m_sData; break;
			case OPER_SET_ARRAY + TYPE_DELTA2: SetArrayValue(Variable1, Variable2, GetValue(Variable3)); break;//установка значения массива			
			case OPER_GET_ARRAY + TYPE_DELTA2: GetArrayValue(Variable1, Variable2, Variable3); break; //получение значения массива
			case OPER_IF + TYPE_DELTA2: if (Variable1.m_sData.IsEmpty()) nCodeLine = Index2 - 1; break;
				//DATE
			case OPER_ADD + TYPE_DELTA3: Variable1.m_dData = Variable2.m_dData + Variable3.m_dData; break;
			case OPER_SUB + TYPE_DELTA3: Variable1.m_dData = Variable2.m_dData - Variable3.m_dData; break;
			case OPER_DIV + TYPE_DELTA3: if (Variable3.m_dData == 0) { CTranslateError::Error(_("Divide by zero")); } Variable1.m_dData = Variable2.m_dData / Variable3.ToInt(); break;
			case OPER_MOD + TYPE_DELTA3: if (Variable3.m_dData == 0) { CTranslateError::Error(_("Divide by zero")); } Variable1.m_dData = (int)Variable2.m_dData % Variable3.ToInt(); break;
			case OPER_MULT + TYPE_DELTA3: Variable1.m_dData = Variable2.m_dData*Variable3.m_dData; break;
			case OPER_LET + TYPE_DELTA3: Variable1.m_dData = Variable2.m_dData; break;
			case OPER_NOT + TYPE_DELTA3: Variable1.m_dData = !Variable2.m_dData; break;
			case OPER_INVERT + TYPE_DELTA3: Variable1.m_dData = -Variable2.m_dData; break;
			case OPER_EQ + TYPE_DELTA3: Variable1.m_dData = (Variable2.m_dData == Variable3.m_dData); break;
			case OPER_NE + TYPE_DELTA3: Variable1.m_dData = (Variable2.m_dData != Variable3.m_dData); break;
			case OPER_GT + TYPE_DELTA3: Variable1.m_dData = (Variable2.m_dData > Variable3.m_dData); break;
			case OPER_LS + TYPE_DELTA3: Variable1.m_dData = (Variable2.m_dData < Variable3.m_dData); break;
			case OPER_GE + TYPE_DELTA3: Variable1.m_dData = (Variable2.m_dData >= Variable3.m_dData); break;
			case OPER_LE + TYPE_DELTA3: Variable1.m_dData = (Variable2.m_dData <= Variable3.m_dData); break;
			case OPER_SET_ARRAY + TYPE_DELTA3:	SetArrayValue(Variable1, Variable2, GetValue(Variable3)); break; //установка значения массива
			case OPER_GET_ARRAY + TYPE_DELTA3: GetArrayValue(Variable1, Variable2, Variable3); break; //получение значения массива
			case OPER_IF + TYPE_DELTA3: if (!Variable1.m_dData) nCodeLine = Index2 - 1; break;
				//BOOLEAN
			case OPER_ADD + TYPE_DELTA4: Variable1.m_bData = Variable2.m_bData + Variable3.m_bData; break;
			case OPER_LET + TYPE_DELTA4: Variable1.m_bData = Variable2.m_bData; break;
			case OPER_NOT + TYPE_DELTA4: Variable1.m_bData = !Variable2.m_bData; break;
			case OPER_INVERT + TYPE_DELTA4: Variable1.m_bData = !Variable2.m_bData; break;
			case OPER_EQ + TYPE_DELTA4: Variable1.m_bData = (Variable2.m_bData == Variable3.m_bData); break;
			case OPER_NE + TYPE_DELTA4: Variable1.m_bData = (Variable2.m_bData != Variable3.m_bData); break;
			case OPER_GT + TYPE_DELTA4: Variable1.m_bData = (Variable2.m_bData > Variable3.m_bData); break;
			case OPER_LS + TYPE_DELTA4: Variable1.m_bData = (Variable2.m_bData < Variable3.m_bData); break;
			case OPER_GE + TYPE_DELTA4: Variable1.m_bData = (Variable2.m_bData >= Variable3.m_bData); break;
			case OPER_LE + TYPE_DELTA4: Variable1.m_bData = (Variable2.m_bData <= Variable3.m_bData); break;
			case OPER_IF + TYPE_DELTA4: if (!Variable1.m_bData) nCodeLine = Index2 - 1; break;
			}
			nCodeLine++;
		}
	}
	catch (const CInterruptBreak *err)
	{
		CSystemObjects::Message(err->what(),
			eStatusMessage::eStatusMessage_Error
		);

		while (nCodeLine < nFinish)
		{
			if (CurCode.m_nOper != OPER_GOTO
				&& CurCode.m_nOper != OPER_NEXT
				&& CurCode.m_nOper != OPER_NEXT_ITER)
			{
				nCodeLine++;
			}
			else
			{
				nCodeLine++;
				goto start_label;
				break;
			}
		}

		return CValue();
	}
	catch (const CTranslateError *err)
	{
		int n = aTryList.size() - 1;
		if (n >= 0) {
			s_errorPlace.Reset(); //Ошибка обрабатывается в этом модуле - стираем место ошибки

			int nLine = aTryList[n].y;

			aTryList.resize(n);
			nCodeLine = nLine;

			goto start_label;
		}

		//в этом модуле нет обработчика - сохраняем место ошибки для следующих модулей
		//Но ошибку сразу не выдаем, т.к. не знаем есть ли дальше обработчики
		if (!s_errorPlace.m_pByteCode) {
			if (m_pByteCode != s_errorPlace.pSkipByteCode) { //системная функция Ошибка выдает исключение только для дочерних модулей
				//ранее сохранили оргинально место ошибки (т.е. ошибка произошла не в этом модуле)
				s_errorPlace.m_pByteCode = m_pByteCode;
				s_errorPlace.nLine = nCodeLine;
			}
		}

		CTranslateError::ProcessError(m_pByteCode->m_aCodeList[nCodeLine], err->what());
	}

	return cRetValue;
}

//параметры nRunModule:
//false-не запускать
//true-запускать
CValue CProcUnit::Execute(CByteCode &cByteCode, bool bRunModule)
{
	if (!cByteCode.m_bCompile) {
		CTranslateError::Error(_("Module: %s not compiled!"),
			cByteCode.m_sModuleName
		);
	}

	s_nRecCount = 0;
	m_pByteCode = &cByteCode;

	//проверяем соответствия модулей (скомпилированного и запущенного)
	if (GetParent()) {
		if (GetParent()->m_pByteCode != m_pByteCode->m_pParent) {
			m_pByteCode = NULL;
			CTranslateError::Error(
				_("System error - compilation failed (#1)\nModule:%s\nParent1:%s\nParent2:%s"),
				cByteCode.m_sModuleName.wc_str(),
				cByteCode.m_pParent->m_sModuleName.wc_str(),
				GetParent()->m_pByteCode->m_sModuleName.wc_str()
			);
		}
	}
	else if (m_pByteCode->m_pParent) {
		m_pByteCode = NULL;
		CTranslateError::Error(
			_("System error - compilation failed (#2)\nModule1:%s\nParent1:%s"),
			cByteCode.m_sModuleName.wc_str(),
			cByteCode.m_pParent->m_sModuleName.wc_str()
		);
	}

	m_cCurContext.SetLocalCount(cByteCode.m_nVarCount);
	m_cCurContext.m_nStart = cByteCode.m_nStartModule;

	unsigned int nParentCount = GetParentCount();

	m_ppArrayCode = new CProcUnit*[nParentCount + 1];
	m_ppArrayCode[0] = this;

	m_pppArrayList = new CValue**[nParentCount + 2];
	m_pppArrayList[0] = m_cCurContext.m_pRefLocVars;
	m_pppArrayList[1] = m_cCurContext.m_pRefLocVars;//начинаем с 1, т.к. 0 - означает локальный контекст

	for (unsigned int i = 0; i < nParentCount; i++) {
		CProcUnit *pCurUnit = GetParent(i);
		m_ppArrayCode[i + 1] = pCurUnit;
		m_pppArrayList[i + 2] = pCurUnit->m_cCurContext.m_pRefLocVars;
	}

	//поддержка внешних переменных
	for (unsigned int i = 0; i < cByteCode.m_aExternValues.size(); i++) {
		if (cByteCode.m_aExternValues[i]) {
			m_cCurContext.m_pRefLocVars[i] = cByteCode.m_aExternValues[i];
		}
	}

	int nDelta = 1;

	//Начальная инициализация переменных модуля
	unsigned int nFinish = m_pByteCode->m_aCodeList.size();

	CByte *pCodeList = m_pByteCode->m_aCodeList.data();
	CValue **pRefLocVars = m_cCurContext.m_pRefLocVars;

	for (unsigned int nCodeLine = 0; nCodeLine < nFinish; nCodeLine++) {
		CByte &byte = pCodeList[nCodeLine];
		if (byte.m_nOper == OPER_SET_TYPE) {
			Variable1.SetType(CValue::GetVTByID(Array2));
		}
	}

	//Запрещаем на запись константы
	for (unsigned int i = 0; i < m_pByteCode->m_aConstList.size(); i++) {
		m_pByteCode->m_aConstList[i].m_bReadOnly = true;
	}

	if (bRunModule) {
		m_cCurContext.m_compileContext = &cByteCode.m_pModule->m_cContext;
		return Execute(&m_cCurContext, nDelta);
	}

	return CValue();
}

//Поиск функции в модуле по имени
//bExportOnly=0-поиск любых функций в текущем модуле + экспортные в родительских модулях
//bExportOnly=1-поиск экспортных функций в текущем и родительских модулях
//bExportOnly=2-поиск экспортных функций в только текущем модуле
int CProcUnit::FindFunction(const wxString &sName, bool bError, int bExportOnly)
{
	int nCodeLine = 0;

	if (!m_pByteCode ||
		!m_pByteCode->m_bCompile) {
		CTranslateError::Error(_("Module not compiled!"));
	}

	if (bExportOnly) {
		nCodeLine = m_pByteCode->m_aExportFuncList[StringUtils::MakeUpper(sName)] - 1;
	}
	else {
		nCodeLine = m_pByteCode->m_aFuncList[StringUtils::MakeUpper(sName)] - 1;
	}

	if (bError && nCodeLine < 0) {
		CTranslateError::Error(_("Procedure or function \"%s\" not found!"), sName.wc_str());
	}

	if (nCodeLine >= 0) {
		return nCodeLine;
	}

	if (GetParent() &&
		bExportOnly <= 1)
	{
		unsigned int nCodeSize = m_pByteCode->m_aCodeList.size();
		nCodeLine = GetParent()->FindFunction(sName, false, 1);

		if (nCodeLine >= 0) {
			return nCodeSize + nCodeLine;
		}
	}

	return wxNOT_FOUND;
}

//Поиск экспортной функций
int CProcUnit::FindExportFunction(const wxString &sName)
{
	return FindFunction(sName, false, 2);
}

//Вызов функции по ее адресу в массиве байт кодов
//Вызов осущетсвляется в т.ч. и в родительском модуле
CValue CProcUnit::CallFunction(unsigned int nCodeLine, CValue **ppParams, unsigned int nReceiveParamCount)
{
	if (!m_pByteCode || !m_pByteCode->m_bCompile) {
		CTranslateError::Error(_("Module not compiled!"));
	}

	unsigned int nCodeSize = m_pByteCode->m_aCodeList.size();

	if (nCodeLine >= nCodeSize)
	{
		if (GetParent()) {
			return GetParent()->CallFunction(nCodeLine - nCodeSize, ppParams, nReceiveParamCount);
		}

		CTranslateError::Error(_("Error calling module function!"));
	}

	CByte *pCodeList = m_pByteCode->m_aCodeList.data();

	CRunContext cRunContext(Index3);//число локальных переменных
	cRunContext.m_nParamCount = Array3;//число формальных параметров
	cRunContext.m_nStart = nCodeLine;

	cRunContext.m_compileContext = reinterpret_cast<CCompileContext *>(pCodeList[cRunContext.m_nStart].m_param1.m_nArray);

	//загружаем параметры
	memcpy(&cRunContext.m_pRefLocVars[0], &ppParams[0], std::min(nReceiveParamCount, cRunContext.m_nParamCount) * sizeof(CValue*));

	//выполним произвольный код 
	return Execute(&cRunContext, false);
}

CValue CProcUnit::CallFunction(methodArg_t &aParams)
{
	if (!m_pByteCode || !m_pByteCode->m_bCompile) {
		CTranslateError::Error(_("Module not compiled!"));
	}

	int nCodeLine = m_pByteCode->m_aFuncList.find(aParams.GetName(true)) != m_pByteCode->m_aFuncList.end() ? m_pByteCode->m_aFuncList[aParams.GetName(true)] - 1 : wxNOT_FOUND;

	wxASSERT(nCodeLine != wxNOT_FOUND);

	CByte *pCodeList = m_pByteCode->m_aCodeList.data();

	CRunContext cRunContext(Index3);//число локальных переменных
	cRunContext.m_nParamCount = Array3;//число формальных параметров
	cRunContext.m_nStart = nCodeLine;

	cRunContext.m_compileContext = reinterpret_cast<CCompileContext *>(pCodeList[cRunContext.m_nStart].m_param1.m_nArray);

	//загружаем параметры
	for (unsigned int i = 0; i < cRunContext.m_nParamCount; i++)
	{
		nCodeLine++;
		if (CurCode.m_nOper == OPER_SETCONST) {

			if (i < aParams.GetParamCount()) {
				bool hasValue = aParams[i].GetType() != eValueTypes::TYPE_EMPTY;

				if (aParams[i].m_bReadOnly || Index2 == 1) {//передача параметра по значению
					CopyValue(cRunContext.m_pLocVars[i], hasValue ? aParams[i] : m_pByteCode->m_aConstList[Index1]);
				}
				else {
					if (hasValue) {
						cRunContext.m_pRefLocVars[i] = aParams.GetAt(i);
					}
					else {
						CopyValue(cRunContext.m_pLocVars[i], m_pByteCode->m_aConstList[Index1]);
					}
				}
			}
			else {
				CopyValue(cRunContext.m_pLocVars[i], m_pByteCode->m_aConstList[Index1]);
			}
		}
		else
		{
			if (i < aParams.GetParamCount()) {
				if (aParams[i].m_bReadOnly || Index2 == 1) {//передача параметра по значению
					CopyValue(cRunContext.m_pLocVars[i], aParams[i]);
				}
				else {
					cRunContext.m_pRefLocVars[i] = aParams.GetAt(i);
				}
			}
		}
	}

	return Execute(&cRunContext, false);
}

//Вызов функции по имении
//Вызов осуществляется только в текущем модуле
CValue CProcUnit::CallFunction(const wxString &sName, CValue **ppParams, unsigned int m_nParamCount)
{
	if (m_pByteCode) {
		int nCodeLine = m_pByteCode->m_aFuncList.find(StringUtils::MakeUpper(sName)) != m_pByteCode->m_aFuncList.end() ?
			m_pByteCode->m_aFuncList[StringUtils::MakeUpper(sName)] - 1 :
			wxNOT_FOUND;
		if (nCodeLine >= 0) {
			return CallFunction(nCodeLine, ppParams, m_nParamCount);
		}
	}

	return CValue();
}

CValue CProcUnit::CallFunction(const wxString &sName)
{
	return CallFunction(sName, NULL, 0);
}

CValue CProcUnit::CallFunction(const wxString &sName, CValue &vParam1)
{
	CValue *ppParams[1];
	ppParams[0] = &vParam1;

	return CallFunction(sName, ppParams, 1);
}

CValue CProcUnit::CallFunction(const wxString &sName, CValue &vParam1, CValue &vParam2)
{
	CValue *ppParams[2];
	ppParams[0] = &vParam1;
	ppParams[1] = &vParam2;

	return CallFunction(sName, ppParams, 2);
}

CValue CProcUnit::CallFunction(const wxString &sName, CValue &vParam1, CValue &vParam2, CValue &vParam3)
{
	CValue *ppParams[3];
	ppParams[0] = &vParam1;
	ppParams[1] = &vParam2;
	ppParams[2] = &vParam3;

	return CallFunction(sName, ppParams, 3);
}

CValue CProcUnit::CallFunction(const wxString &sName, CValue &vParam1, CValue &vParam2, CValue &vParam3, CValue &vParam4)
{
	CValue *ppParams[4];
	ppParams[0] = &vParam1;
	ppParams[1] = &vParam2;
	ppParams[2] = &vParam3;
	ppParams[3] = &vParam4;

	return CallFunction(sName, ppParams, 4);
}

CValue CProcUnit::CallFunction(unsigned int nCodeLine, CValue &vParam1)
{
	CValue *ppParams[1];
	ppParams[0] = &vParam1;

	return CallFunction(nCodeLine, ppParams);
}

CValue CProcUnit::CallFunction(unsigned int nCodeLine, CValue &vParam1, CValue &vParam2)
{
	CValue *ppParams[2];
	ppParams[0] = &vParam1;
	ppParams[1] = &vParam2;

	return CallFunction(nCodeLine, ppParams);
}

void CProcUnit::SetAttribute(attributeArg_t &aParams, CValue &cVal)//установка атрибута
{
	*m_cCurContext.m_pRefLocVars[aParams.GetIndex()] = cVal;
}

void CProcUnit::SetAttribute(const wxString &sName, CValue &cVal)//установка атрибута
{
	int iName = FindAttribute(sName);
	if (iName == wxNOT_FOUND)
	{
		iName = m_cCurContext.GetLocalCount();
		m_cCurContext.SetLocalCount(iName + 1);
		m_cCurContext.m_cLocVars[iName] = CValue(sName);
		*m_cCurContext.m_pRefLocVars[iName] = cVal;
	}
	else *m_cCurContext.m_pRefLocVars[iName] = cVal;
}

CValue CProcUnit::GetAttribute(attributeArg_t &aParams)//значение атрибута
{
	return *m_cCurContext.m_pRefLocVars[aParams.GetIndex()];
}

CValue CProcUnit::GetAttribute(const wxString &sName)//установка атрибута
{
	int iName = FindAttribute(sName);
	if (iName != wxNOT_FOUND) return *m_cCurContext.m_pRefLocVars[iName];
	return CValue();
}

int CProcUnit::FindAttribute(const wxString &sName) const
{
	if (m_pByteCode->m_aExportVarList.find(StringUtils::MakeUpper(sName)) != m_pByteCode->m_aExportVarList.end()) return m_pByteCode->m_aExportVarList[StringUtils::MakeUpper(sName)] - 1;
	else return wxNOT_FOUND;
}

CValue CProcUnit::Evaluate(const wxString &sExpression, CRunContext *pRunContext, bool сompileBlock, bool *bError)
{
	CValue cRetValue;

	if (!pRunContext) {
		pRunContext = CProcUnit::GetCurrentRunContext();
	}

	if (sExpression.IsEmpty() ||
		!pRunContext)
		return CValue();

	bool isSimpleMode = CTranslateError::IsSimpleMode();

	if (!isSimpleMode) {
		CTranslateError::ActivateSimpleMode();
	}

	int nDelta = 0;

	if (pRunContext->m_aEvalString.find(StringUtils::MakeUpper(sExpression)) == pRunContext->m_aEvalString.end()) { //еще не было компиляции такого текста
		CProcUnit *m_pSimpleRun = new CProcUnit;
		CCompileModule *m_compileModule = new CCompileModule;

		if (!m_pSimpleRun->CompileExpression(sExpression, pRunContext, *m_compileModule, сompileBlock)) {
			if (bError)
				*bError = true;

			//delete from memory
			delete m_pSimpleRun;
			delete m_compileModule;

			if (!isSimpleMode) {
				CTranslateError::DeaсtivateSimpleMode();
			}

			return CTranslateError::GetLastError();
		}

		//все ОК
		pRunContext->m_aEvalString[StringUtils::MakeUpper(sExpression)] = m_pSimpleRun;
	}

	//Запускаем
	CProcUnit *m_pRunEval = pRunContext->m_aEvalString[StringUtils::MakeUpper(sExpression)];

	CCompileContext *m_compileContext = pRunContext->m_compileContext;
	wxASSERT(m_compileContext);
	CCompileModule *m_module = m_compileContext->m_compileModule;
	wxASSERT(m_module);

	if (m_module->m_bExpressionOnly) {
		CCompileContext *m_curContext = m_compileContext;
		CCompileModule *m_curModule = m_module;

		while (m_curContext) {
			if (!m_curModule->m_bExpressionOnly)
				break;
			m_curContext = m_curContext->m_parentContext;
			m_curModule = m_module->GetParent();
		}

		if (m_curContext && m_curContext->m_nReturn == RETURN_NONE) {
			nDelta = 1;
		}
	}
	else {
		if (m_compileContext->m_nReturn == RETURN_NONE) {
			nDelta = 1;
		}
	}

	try {
		cRetValue = m_pRunEval->Execute(&m_pRunEval->m_cCurContext, nDelta);
	}
	catch (const CTranslateError *)
	{
		if (bError)
			*bError = true;

		if (!isSimpleMode) {
			CTranslateError::DeaсtivateSimpleMode();
		}

		return CTranslateError::GetLastError();
	}

	if (bError)
		*bError = false;

	if (!isSimpleMode) {
		CTranslateError::DeaсtivateSimpleMode();
	}

	return cRetValue;
}

bool CProcUnit::CompileExpression(const wxString &sExpression, CRunContext *pRunContext, CCompileModule &cModule, bool bCompileBlock)
{
	CByteCode *m_pByteCode = pRunContext->GetByteCode();

	cModule.Load(sExpression);

	//задаем в качестве родителей контекст вызова выражения
	if (m_pByteCode) {
		cModule.m_cByteCode.m_pParent = m_pByteCode;
		cModule.m_pParent = m_pByteCode->m_pModule;
		cModule.m_cContext.m_parentContext = pRunContext->m_compileContext;
	}

	cModule.m_bExpressionOnly = true;
	cModule.m_cContext.m_nFindLocalInParent = 2;
	cModule.m_nCurrentCompile = wxNOT_FOUND;

	try
	{
		if (!cModule.PrepareLexem())
			return false;
	}
	catch (...)
	{
		return false;
	}

	cModule.m_cByteCode.m_pModule = &cModule;

	//дорабатываем массив байт-кодов для возвращения результата выражения
	CByte code;
	code.m_nOper = OPER_RET;

	try
	{
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

	CByte code2;
	code2.m_nOper = OPER_END;

	cModule.m_cByteCode.m_aCodeList.push_back(code2);
	cModule.m_cByteCode.m_nVarCount = cModule.m_cContext.m_cVariables.size();

	//признак завершенности компилирования
	cModule.m_cByteCode.m_bCompile = true;

	//Проецируем в памяти
	SetParent(pRunContext->m_procUnit);

	try
	{
		CCompileContext *m_compileContext = pRunContext->m_compileContext;
		wxASSERT(m_compileContext);
		CCompileModule *m_module = m_compileContext->m_compileModule;
		wxASSERT(m_module);
		Execute(cModule.m_cByteCode, false);

		if (m_module->m_bExpressionOnly)
		{
			int nParentNumber = 1;

			CCompileContext *m_curContext = m_compileContext;
			CCompileModule *m_curModule = m_module;

			while (m_curContext)
			{
				if (m_curModule->m_bExpressionOnly) nParentNumber++;
				m_curContext = m_curContext->m_parentContext; m_curModule = m_module->GetParent();
			}

			m_pppArrayList[nParentNumber] = pRunContext->m_procUnit->m_pppArrayList[nParentNumber - 1];
		}
		else
		{
			m_pppArrayList[1] = pRunContext->m_pRefLocVars;
		}

		m_cCurContext.m_compileContext = &cModule.m_cContext;
	}
	catch (...)
	{
		return false;
	}

	return true;
}

//*************************************************************************************************
//*                                        RunContext                                             *
//*************************************************************************************************

CRunContext::CRunContext(int nLocal) :
	m_nVarCount(0), m_nParamCount(0), m_nCurLine(0),
	m_procUnit(NULL), m_compileContext(NULL)
{
	if (nLocal >= 0) {
		SetLocalCount(nLocal);
	}
};

void CRunContext::SetLocalCount(int nLocal)
{
	m_nVarCount = nLocal;

	if (m_nVarCount > MAX_STATIC_VAR)
	{
		m_pLocVars = new CValue[m_nVarCount];
		m_pRefLocVars = new CValue*[m_nVarCount];
	}
	else
	{
		m_pLocVars = m_cLocVars;
		m_pRefLocVars = m_cRefLocVars;
	}

	for (unsigned int i = 0; i < m_nVarCount; i++) m_pRefLocVars[i] = &m_pLocVars[i];
};

unsigned int CRunContext::GetLocalCount()
{
	return m_nVarCount;
};

CByteCode *CRunContext::GetByteCode()
{
	return m_procUnit ? m_procUnit->GetByteCode() : NULL;
}

CRunContext::~CRunContext()
{
	if (m_nVarCount > MAX_STATIC_VAR)
	{
		delete[]m_pLocVars;
		delete[]m_pRefLocVars;
	}

	//удаление m_aEvalString
	for (auto it = m_aEvalString.begin(); it != m_aEvalString.end(); it++)
	{
		CProcUnit *procUnit = static_cast<CProcUnit*>(it->second);
		if (procUnit) {
			CCompileModule *compileModule = procUnit->m_pByteCode->m_pModule;
			delete procUnit; delete compileModule;
		}
	}
}