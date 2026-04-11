////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, 2�-team
//	Description : Processor unit 
////////////////////////////////////////////////////////////////////////////

#include "compileCode.h"
#include "procUnit.h"

#include "debugger/debugServer.h"
#include "system/systemManager.h"

#include "appData.h"

#define curCode	m_pByteCode->m_listCode[lCodeLine]

#define index1	curCode.m_param1.m_numIndex
#define index2	curCode.m_param2.m_numIndex
#define index3	curCode.m_param3.m_numIndex
#define index4	curCode.m_param4.m_numIndex

#define array1	curCode.m_param1.m_numArray
#define array2	curCode.m_param2.m_numArray
#define array3	curCode.m_param3.m_numArray
#define array4	curCode.m_param4.m_numArray

#define locVariable1 *m_pRefLocVars[index1]
#define locVariable2 *m_pRefLocVars[index2]
#define locVariable3 *m_pRefLocVars[index3]
#define locVariable4 *m_pRefLocVars[index4]

#define variable(x) (array##x<=0?*pRefLocVars[index##x]:(array##x==DEF_VAR_CONST?m_pByteCode->m_listConst[index##x]:*m_pppArrayList[array##x+(bDelta?1:0)][index##x]))

#define variable1 variable(1)
#define variable2 variable(2)
#define variable3 variable(3)
#define variable4 variable(4)

//**************************************************************************************************************
//*                                              support error place                                           *
//**************************************************************************************************************

ibProcUnit* ibProcUnit::m_currentRunModule = nullptr;

struct ibErrorPlace {

	bool IsEmpty() const { return m_errorLine == wxNOT_FOUND; }

	void Reset() {
		m_byteCode = m_skipByteCode = nullptr;
		m_errorLine = wxNOT_FOUND;
	}

	long m_errorLine = wxNOT_FOUND;

	ibByteCode* m_byteCode = nullptr;
	ibByteCode* m_skipByteCode = nullptr;
};

static ibErrorPlace s_errorPlace;

void ibProcUnit::Raise() {

	s_errorPlace.Reset(); //initialize the error place	
	s_errorPlace.m_skipByteCode = ibProcUnit::GetCurrentByteCode(); //return back to the called module (if any)
}

std::vector <ibRunContext*> ibProcUnit::ms_runContext;

//**************************************************************************************************************
//*                                              stack support                                                 *
//**************************************************************************************************************

static short s_nRecCount = 0; //control looping

inline void BeginByteCode(ibRunContext* pCode) { ibProcUnit::AddRunContext(pCode); }
inline bool EndByteCode()
{
	unsigned int n = ibProcUnit::GetCountRunContext();
	if (n > 0)
		ibProcUnit::BackRunContext();
	n--;
	if (n <= 0)
		return false;
	return true;
}

//Stack reset
inline void ResetByteCode() { while (EndByteCode()); }

struct ibProcStackGuard {

	ibProcStackGuard(ibRunContext* runContext) {
		if (s_nRecCount > MAX_REC_COUNT) { //critical error
			wxString strError;
			for (unsigned int i = 0; i < ibProcUnit::GetCountRunContext(); i++) {
				const ibRunContext* stackContext = ibProcUnit::GetRunContext(i);
				wxASSERT(stackContext);
				const ibByteCode* stackByteCode = stackContext->GetByteCode();
				wxASSERT(stackByteCode);
				strError += wxString::Format(wxT("\n%s (#line %d)"),
					stackByteCode->m_strModuleName,
					stackByteCode->m_listCode[stackContext->m_lCurLine].m_numLine + 1
				);
			}
			ibBackendCoreException::Error(_("Number of recursive calls exceeded the maximum allowed value!\nCall stack :") + strError);
		}
		s_nRecCount++;
		m_currentContext = runContext;
		BeginByteCode(runContext);
	}

	~ibProcStackGuard() { s_nRecCount--; EndByteCode(); }

private:
	ibRunContext* m_currentContext;
};

//**************************************************************************************************************
//*                                              inline functions                                              *
//**************************************************************************************************************

//checking variable availability
#define CHECK_READONLY(Operation)\
if(cValue1.m_bReadOnly)\
{\
 ibValue cVal;\
 Operation(cVal,cValue2,cValue3);\
 cValue1.SetValue(cVal);\
 return;\
}\
if(cValue1.m_typeClass==ibValueTypes::TYPE_REFFER)\
 cValue1.m_pRef->DecrRef();\

//Functions for quickly working with the ibValue type
inline void AddValue(ibValue& cValue1, const ibValue& cValue2, const ibValue& cValue3)
{
	CHECK_READONLY(AddValue);
	cValue1.m_typeClass = cValue2.GetType();
	if (cValue1.m_typeClass == ibValueTypes::TYPE_NUMBER) {
		cValue1.m_fData = cValue2.GetNumber() + cValue3.GetNumber();
	}
	else if (cValue1.m_typeClass == ibValueTypes::TYPE_DATE) {
		if (cValue3.m_typeClass == ibValueTypes::TYPE_DATE) { //date + date -> number
			cValue1.m_typeClass = ibValueTypes::TYPE_NUMBER;
			cValue1.m_fData = cValue2.GetDate() + cValue3.GetDate();
		}
		else {
			cValue1.m_dData = cValue2.m_dData + cValue3.GetDate();
		}
	}
	else {
		cValue1.m_typeClass = ibValueTypes::TYPE_STRING;
		cValue1.m_sData = cValue2.GetString() + cValue3.GetString();
	}
}

inline void SubValue(ibValue& cValue1, const ibValue& cValue2, const ibValue& cValue3)
{
	CHECK_READONLY(SubValue);
	cValue1.m_typeClass = cValue2.GetType();
	if (cValue1.m_typeClass == ibValueTypes::TYPE_NUMBER) {
		cValue1.m_fData = cValue2.GetNumber() - cValue3.GetNumber();
	}
	else if (cValue1.m_typeClass == ibValueTypes::TYPE_DATE) {
		if (cValue3.m_typeClass == ibValueTypes::TYPE_DATE) { //date - date -> number
			cValue1.m_typeClass = ibValueTypes::TYPE_NUMBER;
			cValue1.m_fData = cValue2.GetDate() - cValue3.GetDate();
		}
		else {
			cValue1.m_dData = cValue2.m_dData - cValue3.GetDate();
		}
	}
	else {
		ibBackendCoreException::Error(_("Subtraction operation cannot be applied for this type (%s)"), cValue2.GetClassName());
	}
}

inline void MultValue(ibValue& cValue1, const ibValue& cValue2, const ibValue& cValue3)
{
	CHECK_READONLY(MultValue);
	cValue1.m_typeClass = cValue2.GetType();
	if (cValue1.m_typeClass == ibValueTypes::TYPE_NUMBER) {
		cValue1.m_fData = cValue2.GetNumber() * cValue3.GetNumber();
	}
	else if (cValue1.m_typeClass == ibValueTypes::TYPE_DATE) {
		if (cValue3.m_typeClass == ibValueTypes::TYPE_DATE) { //date * date -> number
			cValue1.m_typeClass = ibValueTypes::TYPE_NUMBER;
			cValue1.m_fData = cValue2.GetDate() * cValue3.GetDate();
		}
		else {
			cValue1.m_dData = cValue2.m_dData * cValue3.GetDate();
		}
	}
	else {
		ibBackendCoreException::Error(_("Multiplication operation cannot be applied for this type (%s)"), cValue2.GetClassName());
	}
}

inline void DivValue(ibValue& cValue1, const ibValue& cValue2, const ibValue& cValue3)
{
	CHECK_READONLY(DivValue);
	cValue1.m_typeClass = cValue2.GetType();
	if (cValue1.m_typeClass == ibValueTypes::TYPE_NUMBER) {
		const ibNumber& flNumber3 = cValue3.GetNumber();
		if (flNumber3.IsZero())
			ibBackendCoreException::Error(_("Divide by zero"));
		cValue1.m_fData = cValue2.GetNumber() / flNumber3;
	}
	else {
		ibBackendCoreException::Error(_("Division operation cannot be applied for this type (%s)"), cValue2.GetClassName());
	};
}

inline void ModValue(ibValue& cValue1, const ibValue& cValue2, const ibValue& cValue3)
{
	CHECK_READONLY(ModValue);
	cValue1.m_typeClass = cValue2.GetType();
	if (cValue1.m_typeClass == ibValueTypes::TYPE_NUMBER) {
		ttmath::Int<TTMATH_BITS(128)> val128_2, val128_3;
		const ibNumber& flNumber3 = cValue3.GetNumber(); flNumber3.ToInt(val128_3);
		if (val128_3.IsZero())
			ibBackendCoreException::Error(_("Divide by zero"));
		const ibNumber& flNumber2 = cValue2.GetNumber(); flNumber2.ToInt(val128_2);
		cValue1.m_fData = val128_2 % val128_3;
	}
	else {
		ibBackendCoreException::Error(_("Modulo operation cannot be applied for this type (%s)"), cValue2.GetClassName());
	}
}

//Implementation of comparison operators
inline void CompareValueGT(ibValue& cValue1, const ibValue& cValue2, const ibValue& cValue3)
{
	CHECK_READONLY(CompareValueGT);
	cValue1.m_typeClass = ibValueTypes::TYPE_BOOLEAN;
	cValue1.m_bData = cValue2.CompareValueGT(cValue3);
}

inline void CompareValueGE(ibValue& cValue1, const ibValue& cValue2, const ibValue& cValue3)
{
	CHECK_READONLY(CompareValueGE);
	cValue1.m_typeClass = ibValueTypes::TYPE_BOOLEAN;
	cValue1.m_bData = cValue2.CompareValueGE(cValue3);
}

inline void CompareValueLS(ibValue& cValue1, const ibValue& cValue2, const ibValue& cValue3)
{
	CHECK_READONLY(CompareValueLS);
	cValue1.m_typeClass = ibValueTypes::TYPE_BOOLEAN;
	cValue1.m_bData = cValue2.CompareValueLS(cValue3);
}

inline void CompareValueLE(ibValue& cValue1, const ibValue& cValue2, const ibValue& cValue3)
{
	CHECK_READONLY(CompareValueLE);
	cValue1.m_typeClass = ibValueTypes::TYPE_BOOLEAN;
	cValue1.m_bData = cValue2.CompareValueLE(cValue3);
}

inline void CompareValueEQ(ibValue& cValue1, const ibValue& cValue2, const ibValue& cValue3)
{
	CHECK_READONLY(CompareValueEQ);
	cValue1.m_typeClass = ibValueTypes::TYPE_BOOLEAN;
	cValue1.m_bData = cValue2.CompareValueEQ(cValue3);
}

inline void CompareValueNE(ibValue& cValue1, const ibValue& cValue2, const ibValue& cValue3)
{
	CHECK_READONLY(CompareValueNE);
	cValue1.m_typeClass = ibValueTypes::TYPE_BOOLEAN;
	cValue1.m_bData = cValue2.CompareValueNE(cValue3);
}

inline void CopyValue(ibValue& cValue1, ibValue& cValue2)
{
	if (&cValue1 == &cValue2)
		return;

	//checking variable availability and checking references
	if (cValue1.m_bReadOnly) {
		cValue1.SetValue(cValue2);
		return;
	}
	else {//Reset
		if (cValue1.m_pRef && cValue1.m_typeClass == ibValueTypes::TYPE_REFFER)
			cValue1.m_pRef->DecrRef();

		cValue1.m_typeClass = ibValueTypes::TYPE_EMPTY;
		cValue1.m_sData = wxEmptyString;

		cValue1.m_pRef = nullptr;
	}

	if (cValue2.m_typeClass == ibValueTypes::TYPE_REFFER) {
		cValue1 = cValue2.GetValue();
		return;
	}

	cValue1.m_typeClass = cValue2.m_typeClass;

	switch (cValue2.m_typeClass)
	{
	case ibValueTypes::TYPE_NULL:
		break;
	case ibValueTypes::TYPE_BOOLEAN:
		cValue1.m_bData = cValue2.m_bData;
		break;
	case ibValueTypes::TYPE_NUMBER:
		cValue1.m_fData = cValue2.m_fData;
		break;
	case ibValueTypes::TYPE_STRING:
		cValue1.m_sData = cValue2.m_sData;
		break;
	case ibValueTypes::TYPE_DATE:
		cValue1.m_dData = cValue2.m_dData;
		break;
	case ibValueTypes::TYPE_REFFER:
		cValue1.m_pRef = cValue2.m_pRef; cValue1.m_pRef->IncrRef();
		break;
	case ibValueTypes::TYPE_OLE:
	case ibValueTypes::TYPE_ENUM:
	case ibValueTypes::TYPE_VALUE:
		cValue1.m_typeClass = ibValueTypes::TYPE_REFFER;
		cValue1.m_pRef = &cValue2; cValue1.m_pRef->IncrRef();
		break;
	default: cValue1.m_typeClass = ibValueTypes::TYPE_EMPTY;
	}
}

inline void CopyValue(ibValue& cValue1, ibValue&& cValue2)
{
	// For temporaries: store into local lvalue, then delegate to lvalue version
	ibValue tmp = std::move(cValue2);
	CopyValue(cValue1, tmp);
}

inline void MoveValue(ibValue&& cValue1, ibValue&& cValue2)
{
	if (&cValue1 == &cValue2)
		return;

	cValue1.m_typeClass = cValue2.m_typeClass;

	switch (cValue2.m_typeClass)
	{
	case ibValueTypes::TYPE_NULL:
		break;
	case ibValueTypes::TYPE_BOOLEAN:
		cValue1.m_bData = std::move(cValue2.m_bData);
		break;
	case ibValueTypes::TYPE_NUMBER:
		cValue1.m_fData = std::move(cValue2.m_fData);
		break;
	case ibValueTypes::TYPE_STRING:
		cValue1.m_sData = std::move(cValue2.m_sData);
		break;
	case ibValueTypes::TYPE_DATE:
		cValue1.m_dData = std::move(cValue2.m_dData);
		break;
	case ibValueTypes::TYPE_REFFER:
		cValue1.m_pRef = cValue2.m_pRef;
		cValue1.m_pRef->IncrRef();
		break;
	case ibValueTypes::TYPE_OLE:
	case ibValueTypes::TYPE_ENUM:
	case ibValueTypes::TYPE_VALUE:
		cValue1.m_typeClass = ibValueTypes::TYPE_REFFER;
		cValue1.m_pRef = &cValue2;
		cValue1.m_pRef->IncrRef();
		break;
	default: cValue1.m_typeClass = ibValueTypes::TYPE_EMPTY;
	}

	cValue2.Reset();
}

inline bool IsEmptyValue(const ibValue& cValue1)
{
	return cValue1.IsEmpty();
}

#define IsHasValue(cValue1) (!IsEmptyValue(cValue1))

inline void SetTypeBoolean(ibValue& cValue1, bool bValue)
{
	//check variable availability and reference check
	if (cValue1.m_bReadOnly) {
		cValue1.SetValue(bValue);
		return;
	}
	cValue1.Reset();
	cValue1.m_typeClass = ibValueTypes::TYPE_BOOLEAN;
	cValue1.m_bData = bValue;
}

inline void SetTypeNumber(ibValue& cValue1, const ibNumber& fValue)
{
	//check variable availability and reference check
	if (cValue1.m_bReadOnly) {
		cValue1.SetValue(fValue);
		return;
	}
	cValue1.Reset();
	cValue1.m_typeClass = ibValueTypes::TYPE_NUMBER;
	cValue1.m_fData = fValue;
}

#define CheckAndError(variable, name)\
{\
 if(variable.m_typeClass!=ibValueTypes::TYPE_REFFER)\
 ibBackendCoreException::Error(_("No attribute or method found '%s' - a variable is not an aggregate object"), name);\
 else\
 ibBackendCoreException::Error(_("Aggregate object field not found '%s'"), name);\
}

//Index arrays
inline bool SetArrayValue(ibValue& cValue1, const ibValue& cValue2, ibValue& cValue3)
{
	return cValue1.SetAt(cValue2, cValue3);
}

inline bool SetArrayValue(ibValue& cValue1, const ibValue& cValue2, ibValue&& cValue3)
{
	ibValue tmp = std::move(cValue3);
	return cValue1.SetAt(cValue2, tmp);
}

inline bool GetArrayValue(ibValue& cValue1, ibValue& cValue2, const ibValue& cValue3)
{
	ibValue retValue;
	if (cValue2.GetAt(cValue3, retValue)) {
		CopyValue(cValue1, retValue);
		return true;
	}
	return false;
}

inline ibValue GetValue(ibValue& cValue1)
{
	if (cValue1.m_bReadOnly
		&& cValue1.m_typeClass != ibValueTypes::TYPE_REFFER) {
		ibValue cVal;
		CopyValue(cVal, cValue1);
		return cVal;
	}
	return cValue1;
}

#pragma region iterator_support
class ibValueIterator : public ibValue {
	wxDECLARE_DYNAMIC_CLASS(ibValueIterator);
	static ibValue s_defaultOwner;
public:
	ibValueIterator() : ibValue(ibValueTypes::TYPE_VALUE),
		m_ownerValue(s_defaultOwner), m_currentPos(0) {
	}
	ibValueIterator(ibValue& ownerValue) : ibValue(ibValueTypes::TYPE_VALUE),
		m_ownerValue(ownerValue), m_currentPos(0) {
		ResetIterator();
	}
	virtual ~ibValueIterator() { Reset(); }
	bool GetCurrentValue(ibValue& pvarParamValue) const {
		if (m_currentPos >= m_ownerValue.GetIteratorCount())
			return false;
		CopyValue(pvarParamValue, m_ownerValue.GetIteratorAt(m_currentPos));
		return true;
	}
	bool NextIterator() {
		if (m_currentPos >= m_ownerValue.GetIteratorCount())
			return false;
		m_currentPos++;
		return true;
	};
	void ResetIterator() { m_currentPos = 0; };
protected:
	unsigned long m_currentPos = 0;
private:
	ibValue& m_ownerValue;
};

//**************************************************************************************************************
ibValue ibValueIterator::s_defaultOwner;
wxIMPLEMENT_DYNAMIC_CLASS(ibValueIterator, ibValue);
//**************************************************************************************************************

const ibClassID g_valueIterator = string_to_clsid("SO_ITER");
#pragma endregion

//////////////////////////////////////////////////////////////////////
//						Construction/Destruction                    //
//////////////////////////////////////////////////////////////////////

void ibProcUnit::Execute(ibRunContext* pContext, ibValue* pvarRetValue, bool bDelta)
{
	struct ibTryLabel {

		ibTryLabel() :m_lStartLine(0), m_lEndLine(0) {}
		ibTryLabel(const long& lStartLine, const long& lEndLine) :m_lStartLine(lStartLine), m_lEndLine(lEndLine) {}

		long m_lStartLine, m_lEndLine;
	};

#ifdef DEBUG
	if (pContext == nullptr) {
		ibBackendCoreException::Error(_("No execution context defined!"));
		if (m_pByteCode == nullptr)
			ibBackendCoreException::Error(_("No execution code set!"));
	}
#endif

	pContext->SetProcUnit(this);

	ibProcStackGuard stackGuard(pContext);

	ibValue* pLocVars = pContext->m_pLocVars;
	ibValue** pRefLocVars = pContext->m_pRefLocVars;

	ibByteUnit* pCodeList = m_pByteCode->m_listCode.data();

	long lCodeLine = pContext->m_lStart;
	long lFinish = m_pByteCode->m_listCode.size();
	long lPrevLine = wxNOT_FOUND;

	std::vector<ibTryLabel> tryList;

start_label:

	try { //slower by 2-3% for each nested module
		while (lCodeLine < lFinish) {

			if (!ibBackendException::IsEvalMode()) {
				pContext->m_lCurLine = lCodeLine;
				m_currentRunModule = this;
			}

			//if force exit - terminate 
			if (ibApplicationData::IsForceExit())
				break;

			//enter in debugger
			if (debugServer != nullptr && !ibBackendException::IsEvalMode())
				debugServer->EnterDebugger(pContext, curCode, lPrevLine);

			switch (curCode.m_numOper)
			{
			case OPER_CONST: CopyValue(variable1, m_pByteCode->m_listConst[index2]); break;
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
				if (variable1.m_typeClass != ibValueTypes::TYPE_NUMBER)
					ibBackendCoreException::Error(_("Only variables with type can be used to organize the loop \"number\""));
				if (variable1.m_fData == variable2.m_fData)
					lCodeLine = index3 - 1;
				break;
			case OPER_FOREACH:
			{
				if (!variable2.HasIterator())
					ibBackendCoreException::Error(_("Undefined value iterator"));
				if (g_valueIterator != variable3.GetClassType())
					CopyValue(variable3, ibValue(new ibValueIterator(variable2)));
				ibValueIterator* iterator = variable3.ConvertToType<ibValueIterator>();
				if (!iterator->GetCurrentValue(variable1)) {
					variable3.Reset(); lCodeLine = index4 - 1;
				}
			} break;
			case OPER_NEXT:
			{
				if (variable1.m_typeClass == ibValueTypes::TYPE_NUMBER)
					variable1.m_fData++;
				lCodeLine = index2 - 1;
			} break;
			case OPER_NEXT_ITER:
			{
				ibValueIterator* value_iterator =
					variable1.ConvertToType<ibValueIterator>();
				value_iterator->NextIterator();
				lCodeLine = index2 - 1;
			} break;
			case OPER_ITER:
			{
				if (IsHasValue(variable2))
					CopyValue(variable1, variable3);
				else
					CopyValue(variable1, variable4);
			}  break;
			case OPER_NEW:
			{
				ibValue* pRetValue = &variable1;
				ibRunContextSmall cRunContext(array2);
				cRunContext.m_lParamCount = array2;
				const wxString className = m_pByteCode->m_listConst[index2].m_sData;
				//load parameters
				for (long i = 0; i < cRunContext.m_lParamCount; i++) {
					lCodeLine++;
					if (index1 >= 0) {
						if (variable1.m_bReadOnly && variable1.m_typeClass != ibValueTypes::TYPE_REFFER) {
							CopyValue(cRunContext.m_pLocVars[i], variable1);
						}
						else {
							cRunContext.m_pRefLocVars[i] = &variable1;
						}
					}
				}
				CopyValue(*pRetValue, ibValue::CreateObject(className, cRunContext.m_lParamCount > 0 ? cRunContext.m_pRefLocVars : nullptr, cRunContext.m_lParamCount));
			} break;
			case OPER_SET_A:
			{//setting attribute
				const wxString& strPropName = m_pByteCode->m_listConst[index2].m_sData;
				const long lPropNum = variable1.FindProp(strPropName);
				if (lPropNum < 0) CheckAndError(variable1, strPropName);
				if (!variable1.IsPropWritable(lPropNum)) ibBackendCoreException::Error(_("Object field not writable (%s)"), strPropName);
				variable1.SetPropVal(lPropNum, GetValue(variable3));
			} break;
			case OPER_GET_A://get attribute
			{
				ibValue* pRetValue = &variable1;
				ibValue* pVariable2 = &variable2;
				const wxString& strPropName = m_pByteCode->m_listConst[index3].m_sData;
				const long lPropNum = variable2.FindProp(strPropName);
				if (lPropNum < 0) CheckAndError(variable2, strPropName);
				if (!variable2.IsPropReadable(lPropNum)) ibBackendCoreException::Error(_("Object field not readable (%s)"), strPropName);
				ibValue vRet; bool result = variable2.GetPropVal(lPropNum, vRet);
				if (result && vRet.m_typeClass == ibValueTypes::TYPE_REFFER)
					*pRetValue = vRet;
				else if (result)
					CopyValue(*pRetValue, vRet);
				break;
			}
			case OPER_CALL_M://method call
			{
				ibValue* pRetValue = &variable1;
				ibValue* pVariable2 = &variable2;

				const wxString& funcName = m_pByteCode->m_listConst[index3].m_sData;
				long lMethodNum = wxNOT_FOUND;
				//call optimization
				ibValue* storageValue = reinterpret_cast<ibValue*>(array4);
				if (storageValue && storageValue == pVariable2->GetRef()) { //previously there were calls
					lMethodNum = index4;
#ifdef DEBUG
					lMethodNum = pVariable2->FindMethod(funcName);
					if (lMethodNum != index4) ibBackendCoreException::Error(_("Error value %d must %d (It is recommended to turn off method optimization)"), index4, lMethodNum);
#endif
				}
				else {//there were no calls
					lMethodNum = pVariable2->FindMethod(funcName);
					index4 = lMethodNum;
					array4 = reinterpret_cast<wxLongLong_t>(pVariable2->GetRef());
				}

				if (lMethodNum < 0)
					CheckAndError(variable2, funcName);

				ibRunContextSmall cRunContext(std::max(array3, MAX_STATIC_VAR));
				cRunContext.m_lParamCount = array3;

				// too many parameters
				const long paramCount = pVariable2->GetNParams(lMethodNum);

				if (paramCount < cRunContext.m_lParamCount)
					ibBackendCoreException::Error(ERROR_MANY_PARAMS, funcName, funcName);
				else if (paramCount == wxNOT_FOUND && cRunContext.m_lParamCount == 0)
					ibBackendCoreException::Error(ERROR_MANY_PARAMS, funcName, funcName);

				//load parameters
				for (long i = 0; i < cRunContext.m_lParamCount; i++) {
					lCodeLine++;
					if (index1 >= 0 && !pVariable2->GetParamDefValue(lMethodNum, i, *cRunContext.m_pRefLocVars[i])) {
						if (variable1.m_bReadOnly && variable1.m_typeClass != ibValueTypes::TYPE_REFFER) {
							CopyValue(cRunContext.m_pLocVars[i], variable1);
						}
						else {
							cRunContext.m_pRefLocVars[i] = &variable1;
						}
					}
				}

				if (pVariable2->HasRetVal(lMethodNum)) {
					pVariable2->CallAsFunc(lMethodNum, *pRetValue, cRunContext.m_pRefLocVars, cRunContext.m_lParamCount);
				}
				else {
					// operator =
					if (m_pByteCode->m_listCode[lCodeLine + 1].m_numOper == OPER_LET) {
						lCodeLine++;
						ibValue* pNextVariable2 = &variable2;
						lCodeLine--;
						if (pRetValue == pNextVariable2)
							ibBackendCoreException::Error(ERROR_USE_PROCEDURE_AS_FUNCTION, funcName, funcName);
					}
					else if (m_pByteCode->m_listCode[lCodeLine + 1].m_numOper == OPER_RET) {
						lCodeLine++;
						ibValue* pNextVariable1 = &variable1;
						lCodeLine--;
						if (pRetValue == pNextVariable1)
							ibBackendCoreException::Error(ERROR_USE_PROCEDURE_AS_FUNCTION, funcName, funcName);
					}
					pVariable2->CallAsProc(lMethodNum, cRunContext.m_pRefLocVars, cRunContext.m_lParamCount);
				} break;
			}
			case OPER_CALL:
			{ //call a regular function
				const long lModuleNumber = array2;
				ibRunContext cRunContext(index3);
				cRunContext.m_lStart = index2;
				cRunContext.m_lParamCount = array3;
				ibByteCode* pLocalByteCode = m_ppArrayCode[lModuleNumber]->m_pByteCode;
				cRunContext.m_compileContext = reinterpret_cast<ibCompileContext*>(pLocalByteCode->m_listCode[cRunContext.m_lStart].m_param1.m_numArray);
				ibValue* pRetValue = &variable1;
				//load parameters
				for (long i = 0; i < cRunContext.m_lParamCount; i++) {
					lCodeLine++;
					if (curCode.m_numOper == OPER_SETCONST) {
						CopyValue(cRunContext.m_pLocVars[i], pLocalByteCode->m_listConst[index1]);
					}
					else {
						if (variable1.m_bReadOnly || index2 == 1) {//pass parameter by value
							CopyValue(cRunContext.m_pLocVars[i], variable1);
						}
						else {
							cRunContext.m_pRefLocVars[i] = &variable1;
						}
					}
				}
				m_ppArrayCode[lModuleNumber]->Execute(&cRunContext, pRetValue, false);
				break;
			}
			case OPER_SET_ARRAY:
				if (!SetArrayValue(variable1, variable2, GetValue(variable3)))
					ibBackendCoreException::Error(_("Cannot set array value '%s'"), variable3.GetString());
				break; //setting the array value
			case OPER_GET_ARRAY:
				if (!GetArrayValue(variable1, variable2, variable3))
					ibBackendCoreException::Error(_("Cannot get array value '%s'"), variable3.GetString());
				break; //getting the array value
			case OPER_GOTO: case OPER_ENDTRY:
			{
				const long tryCodeLine = index1;
				const long trySize = tryList.size() - 1;
				if (trySize >= 0) {
					if (tryCodeLine >= tryList[trySize].m_lEndLine ||
						tryCodeLine <= tryList[trySize].m_lStartLine) {
						tryList.resize(trySize);//exit from try..catch scope
					}
				}
				lCodeLine = tryCodeLine - 1;//since we'll add 1 later
			} break;
			case OPER_TRY:
				tryList.emplace_back(lCodeLine, index1);
				break; //transition on error
			case OPER_RAISE: ibBackendCoreException::Error(ibBackendException::GetLastError()); break;
			case OPER_RAISE_T: ibBackendCoreException::Error(m_pByteCode->m_listConst[index1].GetString()); break;
			case OPER_RET:
				if (index1 != DEF_VAR_NORET) {
					if (pvarRetValue == nullptr)
						ibBackendCoreException::Error(_("Cannot set return value in procedure!"));
					CopyValue(*pvarRetValue, variable1);
				}
			case OPER_ENDFUNC:
			case OPER_END:
				lCodeLine = lFinish;
				break; //exit
			case OPER_FUNC: if (bDelta) {
				while (lCodeLine < lFinish) {
					if (curCode.m_numOper != OPER_ENDFUNC) {
						lCodeLine++;
					}
					else break;
				}
			} break; //this is the initial run - skip the bodies of procedures and functions
			case OPER_SET_TYPE:
				variable1.SetType(ibValue::GetVTByID(array2));
				break;
				//Operators for working with typed data
				//NUMBER
			case OPER_ADD + TYPE_DELTA1: variable1.m_fData = variable2.m_fData + variable3.m_fData; break;
			case OPER_SUB + TYPE_DELTA1: variable1.m_fData = variable2.m_fData - variable3.m_fData; break;
			case OPER_DIV + TYPE_DELTA1: if (variable3.m_fData.IsZero()) { ibBackendCoreException::Error(_("Divide by zero")); } variable1.m_fData = variable2.m_fData / variable3.m_fData; break;
			case OPER_MOD + TYPE_DELTA1: if (variable3.m_fData.IsZero()) { ibBackendCoreException::Error(_("Divide by zero")); } variable1.m_fData = variable2.m_fData.Round() % variable3.m_fData.Round(); break;
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
			case OPER_SET_ARRAY + TYPE_DELTA1:
				if (!SetArrayValue(variable1, variable2, GetValue(variable3)))
					ibBackendCoreException::Error(_("Cannot set array value '%s'"), variable3.GetString());
				break;//set array value
			case OPER_GET_ARRAY + TYPE_DELTA1:
				if (!GetArrayValue(variable1, variable2, variable3))
					ibBackendCoreException::Error(_("Cannot get array value '%s'"), variable3.GetString());
				break; //getting the array value
			case OPER_IF + TYPE_DELTA1: if (variable1.m_fData.IsZero()) lCodeLine = index2 - 1; break;
				//STRING
			case OPER_ADD + TYPE_DELTA2: variable1.m_sData = variable2.m_sData + variable3.m_sData; break;
			case OPER_LET + TYPE_DELTA2: variable1.m_sData = variable2.m_sData; break;
			case OPER_SET_ARRAY + TYPE_DELTA2:
				if (!SetArrayValue(variable1, variable2, GetValue(variable3)))
					ibBackendCoreException::Error(_("Cannot set array value '%s'"), variable3.GetString());
				break; //set array value
			case OPER_GET_ARRAY + TYPE_DELTA2:
				if (!GetArrayValue(variable1, variable2, variable3))
					ibBackendCoreException::Error(_("Cannot get array value '%s'"), variable3.GetString());
				break; //getting the array value
			case OPER_IF + TYPE_DELTA2: if (variable1.m_sData.IsEmpty()) lCodeLine = index2 - 1; break;
				//DATE
			case OPER_ADD + TYPE_DELTA3: variable1.m_dData = variable2.m_dData + variable3.m_dData; break;
			case OPER_SUB + TYPE_DELTA3: variable1.m_dData = variable2.m_dData - variable3.m_dData; break;
			case OPER_DIV + TYPE_DELTA3: if (variable3.m_dData == 0) { ibBackendCoreException::Error(_("Divide by zero")); } variable1.m_dData = variable2.m_dData / variable3.GetInteger(); break;
			case OPER_MOD + TYPE_DELTA3: if (variable3.m_dData == 0) { ibBackendCoreException::Error(_("Divide by zero")); } variable1.m_dData = (int)variable2.m_dData % variable3.GetInteger(); break;
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
			case OPER_SET_ARRAY + TYPE_DELTA3:
				if (!SetArrayValue(variable1, variable2, GetValue(variable3)))
					ibBackendCoreException::Error(_("Cannot set array value '%s'"), variable3.GetString());
				break; //setting the array value
			case OPER_GET_ARRAY + TYPE_DELTA3:
				if (!GetArrayValue(variable1, variable2, variable3))
					ibBackendCoreException::Error(_("Cannot get array value '%s'"), variable3.GetString());
				break; //getting the array value
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
	catch (const ibBackendInterruptException* err) {

		ibValueSystemFunction::Message(err->GetErrorDescription(),
			ibStatusMessage::ibStatusMessage_Error);

		while (lCodeLine < lFinish) {
			if (curCode.m_numOper != OPER_GOTO
				&& curCode.m_numOper != OPER_NEXT
				&& curCode.m_numOper != OPER_NEXT_ITER) {
				lCodeLine++;
			}
			else {
				lCodeLine++;
				goto start_label;
				break;
			}
		}

		s_errorPlace.Reset(); //Error is handled in this module - erase the error location

	}
	catch (const ibBackendException* err) {

		const long trySize = tryList.size() - 1;
		if (trySize >= 0) {

			s_errorPlace.Reset(); //Error is handled in this module - erase the error location

			const long tryCodeLine = tryList[trySize].m_lEndLine;
			tryList.resize(trySize);
			lCodeLine = tryCodeLine;
			goto start_label;
		}

		//there is no handler in this module - save the error location for the following modules
		//But we don't throw an error right away, because we don't know if there are any handlers further
		if (s_errorPlace.m_byteCode == nullptr && m_pByteCode != s_errorPlace.m_skipByteCode) { //the Error system function throws an exception only for child modules

			//previously saved the original error location (i.e. the error didn't occur in this module)
			s_errorPlace.m_byteCode = m_pByteCode;
			s_errorPlace.m_errorLine = lCodeLine;
		}

		//show and throw error message
		ibBackendException::ProcessError(err, m_pByteCode->m_listCode[lCodeLine]);
	}
}

//nRunModule parameters:
//false-do not run
//true-run
void ibProcUnit::Execute(ibByteCode& cByteCode, ibValue* pvarRetValue, bool bRunModule)
{
	Reset();

	if (!cByteCode.m_bCompile)
		ibBackendCoreException::Error(_("Module: %s not compiled!"), cByteCode.m_strModuleName);

	s_nRecCount = 0;
	m_pByteCode = &cByteCode;

	//check the conformity of modules (compiled and running)
	if (GetParent() && GetParent()->m_pByteCode != m_pByteCode->m_parent) {
		m_pByteCode = nullptr;
		ibBackendCoreException::Error(_("System error - compilation failed (#1)\nModule:%s\nParent1:%s\nParent2:%s"),
			cByteCode.m_strModuleName,
			cByteCode.m_parent->m_strModuleName,
			GetParent()->m_pByteCode->m_strModuleName
		);
	}
	else if (!GetParent() && m_pByteCode->m_parent) {
		m_pByteCode = nullptr;
		ibBackendCoreException::Error(_("System error - compilation failed (#2)\nModule1:%s\nParent1:%s"),
			cByteCode.m_strModuleName,
			cByteCode.m_parent->m_strModuleName
		);
	}

	m_cCurContext.SetLocalCount(cByteCode.m_lVarCount);
	m_cCurContext.m_lStart = cByteCode.m_lStartModule;

	unsigned int nParentCount = GetParentCount();

	m_ppArrayCode = new ibProcUnit * [nParentCount + 1];
	m_ppArrayCode[0] = this;

	m_pppArrayList = new ibValue * *[nParentCount + 2];
	m_pppArrayList[0] = m_cCurContext.m_pRefLocVars;
	m_pppArrayList[1] = m_cCurContext.m_pRefLocVars;//start with 1, because 0 means local context

	for (unsigned int i = 0; i < nParentCount; i++) {
		ibProcUnit* pCurUnit = GetParent(i);
		m_ppArrayCode[i + 1] = pCurUnit;
		m_pppArrayList[i + 2] = pCurUnit->m_cCurContext.m_pRefLocVars;
	}

	//support for external variables
	for (unsigned int i = 0; i < cByteCode.m_listExternValue.size(); i++) {
		if (cByteCode.m_listExternValue[i]) {
			m_cCurContext.m_pRefLocVars[i] = cByteCode.m_listExternValue[i];
		}
	}

	bool bDelta = true;

	//Initial initialization of module variables
	unsigned int lFinish = m_pByteCode->m_listCode.size();
	ibValue** pRefLocVars = m_cCurContext.m_pRefLocVars;

	for (unsigned int lCodeLine = 0; lCodeLine < lFinish; lCodeLine++) {
		ibByteUnit& byte = m_pByteCode->m_listCode[lCodeLine];
		if (byte.m_numOper == OPER_SET_TYPE) {
			variable1.SetType(ibValue::GetVTByID(array2));
		}
	}

	//Disable writing constants
	for (unsigned int i = 0; i < m_pByteCode->m_listConst.size(); i++) {
		m_pByteCode->m_listConst[i].m_bReadOnly = true;
	}

	if (bRunModule) {
		m_cCurContext.m_compileContext = cByteCode.m_compileModule->m_rootContext;
		Execute(&m_cCurContext, pvarRetValue, bDelta);
	}
}

//Search for a function in a module by name
//bExportOnly=0-search for any functions in the current module + exported ones in parent modules
//bExportOnly=1-search for exported functions in the current and parent modules
//bExportOnly=2-search for exported functions in the current module only
long ibProcUnit::FindMethod(const wxString& strMethodName, bool bError, int bExportOnly) const
{
	if (m_pByteCode == nullptr ||
		!m_pByteCode->m_bCompile) {
		ibBackendCoreException::Error(_("Module not compiled!"));
	}

	long lCodeLine = bExportOnly ?
		m_pByteCode->FindExportMethod(strMethodName) :
		m_pByteCode->FindMethod(strMethodName);

	if (bError && lCodeLine < 0)
		ibBackendCoreException::Error(_("Procedure or function \"%s\" not found!"), strMethodName);

	if (lCodeLine >= 0) {
		return lCodeLine;
	}
	if (GetParent() &&
		bExportOnly <= 1) {
		unsigned int nCodeSize = m_pByteCode->m_listCode.size();
		lCodeLine = GetParent()->FindMethod(strMethodName, false, 1);
		if (lCodeLine >= 0) {
			return nCodeSize + lCodeLine;
		}
	}
	return wxNOT_FOUND;
}

long ibProcUnit::FindFunction(const wxString& strMethodName, bool bError, int bExportOnly) const
{
	if (m_pByteCode == nullptr ||
		!m_pByteCode->m_bCompile) {
		ibBackendCoreException::Error(_("Module not compiled!"));
	}

	long lCodeLine = bExportOnly ?
		m_pByteCode->FindExportFunction(strMethodName) :
		m_pByteCode->FindFunction(strMethodName);

	if (bError && lCodeLine < 0)
		ibBackendCoreException::Error(_("Function \"%s\" not found!"), strMethodName);

	if (lCodeLine >= 0)
		return lCodeLine;

	if (GetParent() &&
		bExportOnly <= 1) {
		unsigned int nCodeSize = m_pByteCode->m_listCode.size();
		lCodeLine = GetParent()->FindFunction(strMethodName, false, 1);
		if (lCodeLine >= 0) {
			return nCodeSize + lCodeLine;
		}
	}
	return wxNOT_FOUND;
}

long ibProcUnit::FindProcedure(const wxString& strMethodName, bool bError, int bExportOnly) const
{
	if (m_pByteCode == nullptr ||
		!m_pByteCode->m_bCompile) {
		ibBackendCoreException::Error(_("Module not compiled!"));
	}

	long lCodeLine = bExportOnly ?
		m_pByteCode->FindExportProcedure(strMethodName) :
		m_pByteCode->FindProcedure(strMethodName);

	if (bError && lCodeLine < 0)
		ibBackendCoreException::Error(_("Procedure \"%s\" not found!"), strMethodName);

	if (lCodeLine >= 0) {
		return lCodeLine;
	}
	if (GetParent() &&
		bExportOnly <= 1) {
		unsigned int nCodeSize = m_pByteCode->m_listCode.size();
		lCodeLine = GetParent()->FindProcedure(strMethodName, false, 1);
		if (lCodeLine >= 0) {
			return nCodeSize + lCodeLine;
		}
	}
	return wxNOT_FOUND;
}

//Calling a procedure by name
//The call is made only in the current module
bool ibProcUnit::CallAsProc(const wxString& funcName, ibValue** ppParams, const long lSizeArray)
{
	if (m_pByteCode != nullptr) {
		const long lCodeLine = m_pByteCode->FindMethod(funcName);
		if (lCodeLine != wxNOT_FOUND) {
			CallAsProc(lCodeLine, ppParams, lSizeArray);
			return true;
		}
	}
	return false;
}

//Calling a function by name
//The call is made only in the current module
bool ibProcUnit::CallAsFunc(const wxString& funcName, ibValue& pvarRetValue, ibValue** ppParams, const long lSizeArray)
{
	if (m_pByteCode != nullptr) {
		const long lCodeLine = m_pByteCode->FindMethod(funcName);
		if (lCodeLine != wxNOT_FOUND) {
			CallAsFunc(lCodeLine, pvarRetValue, ppParams, lSizeArray);
			return true;
		}
	}
	return false;
}

//Calling a procedure by its address in the byte code array
//The call is made incl. and in the parent module
void ibProcUnit::CallAsProc(const long lCodeLine, ibValue** ppParams, const long lSizeArray)
{
	if (m_pByteCode == nullptr || !m_pByteCode->m_bCompile)
		ibBackendCoreException::Error(_("Module not compiled!"));

	const long lCodeSize = m_pByteCode->m_listCode.size();
	if (lCodeLine >= lCodeSize) {
		if (!GetParent())
			ibBackendCoreException::Error(_("Error calling module procedure!"));
		GetParent()->CallAsProc(lCodeLine - lCodeSize, ppParams, lSizeArray);
	}

	ibRunContext cRunContext(index3);// number of local variables

	cRunContext.m_lParamCount = array3;//number of formal parameters
	cRunContext.m_lStart = lCodeLine;
	cRunContext.m_compileContext = reinterpret_cast<ibCompileContext*>(m_pByteCode->m_listCode[cRunContext.m_lStart].m_param1.m_numArray);

	//load parameters
	memcpy(&cRunContext.m_pRefLocVars[0], &ppParams[0], std::min(lSizeArray, cRunContext.m_lParamCount) * sizeof(ibValue*));

	//execute arbitrary code
	Execute(&cRunContext, nullptr, false);
}

//Calling a function by its address in the byte code array
//The call is made incl. and in the parent module
void ibProcUnit::CallAsFunc(const long lCodeLine, ibValue& pvarRetValue, ibValue** ppParams, const long lSizeArray)
{
	if (m_pByteCode == nullptr || !m_pByteCode->m_bCompile)
		ibBackendCoreException::Error(_("Module not compiled!"));

	const long lCodeSize = m_pByteCode->m_listCode.size();
	if (lCodeLine >= lCodeSize) {
		if (!GetParent())
			ibBackendCoreException::Error(_("Error calling module function!"));
		GetParent()->CallAsFunc(lCodeLine - lCodeSize, pvarRetValue, ppParams, lSizeArray);
	}

	ibRunContext cRunContext(index3);// number of local variables

	cRunContext.m_lParamCount = array3;//number of formal parameters
	cRunContext.m_lStart = lCodeLine;
	cRunContext.m_compileContext = reinterpret_cast<ibCompileContext*>(m_pByteCode->m_listCode[cRunContext.m_lStart].m_param1.m_numArray);

	//load parameters
	memcpy(&cRunContext.m_pRefLocVars[0], &ppParams[0], std::min(lSizeArray, cRunContext.m_lParamCount) * sizeof(ibValue*));

	//execute arbitrary code
	Execute(&cRunContext, &pvarRetValue, false);
}

long ibProcUnit::FindProp(const wxString& strPropName) const
{
	auto iterator = std::find_if(m_pByteCode->m_listExportVar.begin(), m_pByteCode->m_listExportVar.end(),
		[strPropName](const auto pair) {return stringUtils::CompareString(strPropName, pair.first); });
	if (iterator != m_pByteCode->m_listExportVar.end())
		return (long)iterator->second;
	return wxNOT_FOUND;
}

bool ibProcUnit::SetPropVal(const long lPropNum, const ibValue& varPropVal)//setting attribute
{
	*m_cCurContext.m_pRefLocVars[lPropNum] = varPropVal;
	return true;
}

bool ibProcUnit::SetPropVal(const wxString& strPropName, const ibValue& varPropVal)//setting attribute
{
	long lPropNum = FindProp(strPropName);
	if (lPropNum != wxNOT_FOUND) {
		*m_cCurContext.m_pRefLocVars[lPropNum] = varPropVal;
	}
	else {
		const long lPropPos = m_cCurContext.GetLocalCount();
		m_cCurContext.SetLocalCount(lPropPos + 1);
		m_cCurContext.m_cLocVars[lPropPos] = ibValue(strPropName);
		*m_cCurContext.m_pRefLocVars[lPropPos] = varPropVal;
	}
	return true;
}

bool ibProcUnit::GetPropVal(const long lPropNum, ibValue& pvarPropVal) //attribute value
{
	pvarPropVal = m_cCurContext.m_pRefLocVars[lPropNum];
	return true;
}

bool ibProcUnit::GetPropVal(const wxString& strPropName, ibValue& pvarPropVal) //setting attribute
{
	const long lPropNum = FindProp(strPropName);
	if (lPropNum != wxNOT_FOUND) {
		pvarPropVal = m_cCurContext.m_pRefLocVars[lPropNum];
		return true;
	}
	return false;
}

bool ibProcUnit::Evaluate(const wxString& strExpression, ibRunContext* pRunContext, ibValue& pvarRetValue, bool compileBlock)
{
	if (pRunContext == nullptr)
		pRunContext = ibProcUnit::GetCurrentRunContext();

	if (strExpression.IsEmpty() || pRunContext == nullptr)
		return false;

	bool isEvalMode = ibBackendException::IsEvalMode();
	if (!isEvalMode) ibBackendException::SetEvalMode(true);

	auto iterator = std::find_if(pRunContext->m_listEval.begin(), pRunContext->m_listEval.end(),
		[strExpression](const auto pair) {return stringUtils::CompareString(strExpression, pair.first); });

	std::shared_ptr<ibProcUnitEvaluate> runEvaluate = nullptr;
	if (iterator == pRunContext->m_listEval.end()) { //this text has not yet been compiled

		ibCompileCode* compileExpression = new ibCompileCode;
		compileExpression->Load(strExpression);

		ibProcUnitEvaluate* evalUnit = new ibProcUnitEvaluate;
		if (!evalUnit->CompileExpression(pRunContext, pvarRetValue, *compileExpression, compileBlock)) {
			//delete from memory
			wxDELETE(evalUnit);
			wxDELETE(compileExpression);
			if (!isEvalMode) ibBackendException::SetEvalMode(false);
			return false;
		}

		runEvaluate = std::shared_ptr<ibProcUnitEvaluate>(evalUnit);

		//everything is OK
		pRunContext->m_listEval.insert_or_assign(stringUtils::MakeUpper(strExpression), runEvaluate);
	}
	else {
		runEvaluate = iterator->second;
	}

	//Launch
	bool bDelta = false;

	ibCompileContext* compileContext = pRunContext->m_compileContext;
	wxASSERT(compileContext);
	ibCompileCode* compileCode = compileContext->m_compileModule;
	wxASSERT(compileCode);
	if (compileCode->m_bExpressionOnly) {
		ibCompileContext* curContext = compileContext;
		ibCompileCode* curModule = compileCode;
		while (curContext != nullptr) {
			if (!curModule->m_bExpressionOnly)
				break;
			curContext = curContext->m_parentContext;
			curModule = compileCode->GetParent();
		}
		if (curContext && curContext->m_numReturn == RETURN_NONE) {
			bDelta = true;
		}
	}
	else if (compileContext->m_numReturn == RETURN_NONE) {
		bDelta = true;
	}

	try {
		runEvaluate->Execute(&runEvaluate->m_cCurContext, &pvarRetValue, bDelta);
	}
	catch (const ibBackendException*) {
		if (!isEvalMode) ibBackendException::SetEvalMode(false);
		return false;
	}

	if (!isEvalMode) ibBackendException::SetEvalMode(false);
	return true;
}

bool ibProcUnit::CompileExpression(ibRunContext* pRunContext, ibValue& pvarRetValue, ibCompileCode& cModule, bool bCompileBlock)
{
	ibByteCode* const byteCode = pRunContext->GetByteCode();

	//set the expression calling context as parents
	if (byteCode != nullptr) {
		cModule.m_cByteCode.m_parent = byteCode;
		cModule.m_parent = byteCode->m_compileModule;
		cModule.m_rootContext->m_parentContext = pRunContext->m_compileContext;
	}

	cModule.m_bExpressionOnly = true;
	cModule.m_rootContext->m_numFindLocalInParent = 2;
	cModule.m_numCurrentCompile = wxNOT_FOUND;

	try {
		if (!cModule.PrepareLexem()) {
			return false;
		}
	}
	catch (...) {
		return false;
	}

	cModule.m_cByteCode.m_compileModule = &cModule;

	//process the bytecode array to return the expression result
	ibByteUnit code;
	code.m_numOper = OPER_RET;

	try {
		if (bCompileBlock)
			cModule.CompileBlock(cModule.GetContext());
		else
			code.m_param1 = cModule.GetExpression(cModule.GetContext());
	}
	catch (...)
	{
		return false;
	}

	if (!bCompileBlock) {
		cModule.m_cByteCode.m_listCode.push_back(code);
	}

	ibByteUnit code2;
	code2.m_numOper = OPER_END;

	cModule.m_cByteCode.m_listCode.push_back(code2);
	cModule.m_cByteCode.m_lVarCount = cModule.m_rootContext->m_listVariable.size();

	//flag of compilation completion
	cModule.m_cByteCode.m_bCompile = true;

	//Project in memory
	SetParent(pRunContext->m_procUnit);

	try {
		ibCompileContext* compileContext = pRunContext->m_compileContext;
		wxASSERT(compileContext);
		ibCompileCode* compleModule = compileContext->m_compileModule;
		wxASSERT(compleModule);
		Execute(cModule.m_cByteCode, pvarRetValue, false);
		if (compleModule->m_bExpressionOnly) {
			int nParentNumber = 1;
			ibCompileContext* curContext = compileContext;
			ibCompileCode* curModule = compleModule;
			while (curContext != nullptr) {
				if (curModule->m_bExpressionOnly)
					nParentNumber++;
				curContext = curContext->m_parentContext; curModule = compleModule->GetParent();
			}
			m_pppArrayList[nParentNumber] = pRunContext->m_procUnit->m_pppArrayList[nParentNumber - 1];
		}
		else {
			m_pppArrayList[1] = pRunContext->m_pRefLocVars;
		}
		m_cCurContext.m_compileContext = cModule.m_rootContext;
	}
	catch (...) {
		return false;
	}

	return true;
}

//////////////////////////////////////////////////////////////////////
//						Construction/Destruction                    //
//////////////////////////////////////////////////////////////////////

ibProcUnitEvaluate::~ibProcUnitEvaluate()
{
	if (m_pByteCode != nullptr) {
		delete m_pByteCode->m_compileModule;
	}
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

SYSTEM_TYPE_REGISTER(ibValueIterator, "Iterator", g_valueIterator);
