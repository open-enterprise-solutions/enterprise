////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : acc register manager
////////////////////////////////////////////////////////////////////////////

#include "accumulationRegisterManager.h"
#include "backend/metaData.h"

#include "commonObject.h"


const ibValueMetaObjectCommonModule* ibValueManagerDataObjectAccumulationRegister::GetManagerModule() const
{
	return m_metaObject->GetManagerModule();
}

enum Func {
	eCreateRecordSet,
	eCreateRecordKey,
	eBalance,
	eTurnover,
	eSelect,
	eGetForm,
	eGetListForm,
	eGetTemplate,
};

void ibValueManagerDataObjectAccumulationRegister::FillManagerMethods(ibMemberTable& helper) const
{
	helper.AppendFunc(ibT("CreateRecordSet"), ibT("CreateRecordSet()"));
	helper.AppendFunc(ibT("CreateRecordKey"), ibT("CreateRecordKey()"));
	helper.AppendFunc(ibT("Balance"), 2, ibT("Balance(period, filter...)"));
	helper.AppendFunc(ibT("Turnovers"), 4, ibT("Turnovers(beginOfPeriod, endOfPeriod, filter, periodicity)"));
	helper.AppendFunc(ibT("Select"), ibT("Select()"));
	helper.AppendFunc(ibT("GetForm"), 3, ibT("GetForm(string, owner, guid)"));
	// NOTE: no GetRecordForm here — accumulation register has no record form
	// (the enum Func and CallAsFunc switch don't handle it). An extra AppendFunc
	// shifted every later method's index off its enum case (GetListForm/GetTemplate
	// misrouted). Method index == AppendFunc order, so this list must mirror enum Func.
	helper.AppendFunc(ibT("GetListForm"), 3, ibT("GetListForm(string, owner, guid)"));
	helper.AppendFunc(ibT("GetTemplate"), 1, ibT("GetTemplate(string)"));
}

#include "selector/objectSelector.h"

bool ibValueManagerDataObjectAccumulationRegister::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	// Our own ordinal, not the table index — ibValueManagerDataObject::BuiltinMethodNum says why.
	switch (BuiltinMethodNum(lMethodNum))
	{
	case eCreateRecordSet:
		pvarRetValue = m_metaObject->CreateRecordSetObjectValue();
		return true;
	case eCreateRecordKey:
		pvarRetValue = new ibValueRecordKeyObject(m_metaObject);
		return true;
	// ⚠ BY NAMED SLOT, AND BY VALUE. This read the arguments positionally with a `lSizeArray > n ?`
	// fork per arity — and in one branch passed `paParams[1]` where a VALUE is wanted. That compiles:
	// ibValue has an implicit constructor from ibValue*, so the argument silently became a REFFER
	// wrapping the pointer rather than the value itself. One reader out of two, in the same call.
	//
	// ibRegArg answers "absent" for a slot nobody passed, so the arity forks go too.
	case eBalance:
		pvarRetValue = Balance(ibRegArg(paParams, lSizeArray, ibRegBalanceArg::Period),
		                       ibRegArg(paParams, lSizeArray, ibRegBalanceArg::Filter));
		return true;
	case eTurnover:
		pvarRetValue = Turnovers(ibRegArg(paParams, lSizeArray, ibRegTurnoverArg::Begin),
		                         ibRegArg(paParams, lSizeArray, ibRegTurnoverArg::End),
		                         ibRegArg(paParams, lSizeArray, ibRegTurnoverArg::Filter),
		                         ibRegArg(paParams, lSizeArray, ibRegTurnoverArg::Periodicity));
		return true;
	case eSelect:
		pvarRetValue = new ibValueSelectorRegisterDataObject(m_metaObject);
		return true;
	case eGetForm:
	{
		ibValueGuid* guidVal = lSizeArray > 2 ? paParams[2]->ConvertToType<ibValueGuid>() : nullptr;
		pvarRetValue = m_metaObject->GetGenericForm(ibFormRequest(paParams[0]->GetString(), guidVal ? ((ibGuid)*guidVal) : ibGuid()),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr);
		return true;
	}
	case eGetListForm:
	{
		ibValueGuid* guidVal = lSizeArray > 2 ? paParams[2]->ConvertToType<ibValueGuid>() : nullptr;
		pvarRetValue = m_metaObject->GetListForm(ibFormRequest(paParams[0]->GetString(), guidVal ? ((ibGuid)*guidVal) : ibGuid()),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr);
		return true;
	}
	case eGetTemplate:
		pvarRetValue = m_metaObject->GetTemplate(paParams[0]->GetString());
		return true;
	}

	return ibValueManagerDataObject::CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
}