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
	helper.AppendFunc(wxT("CreateRecordSet"), wxT("CreateRecordSet()"));
	helper.AppendFunc(wxT("CreateRecordKey"), wxT("CreateRecordKey()"));
	helper.AppendFunc(wxT("Balance"), 2, wxT("Balance(period, filter...)"));
	helper.AppendFunc(wxT("Turnovers"), 4, wxT("Turnovers(beginOfPeriod, endOfPeriod, filter...)"));
	helper.AppendFunc(wxT("Select"), wxT("Select()"));
	helper.AppendFunc(wxT("GetForm"), 3, wxT("GetForm(string, owner, guid)"));
	// NOTE: no GetRecordForm here — accumulation register has no record form
	// (the enum Func and CallAsFunc switch don't handle it). An extra AppendFunc
	// shifted every later method's index off its enum case (GetListForm/GetTemplate
	// misrouted). Method index == AppendFunc order, so this list must mirror enum Func.
	helper.AppendFunc(wxT("GetListForm"), 3, wxT("GetListForm(string, owner, guid)"));
	helper.AppendFunc(wxT("GetTemplate"), 1, wxT("GetTemplate(string)"));
}

#include "selector/objectSelector.h"

bool ibValueManagerDataObjectAccumulationRegister::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case eCreateRecordSet:
		pvarRetValue = m_metaObject->CreateRecordSetObjectValue();
		return true;
	case eCreateRecordKey:
		pvarRetValue = ibValue::CreateAndPrepareValueRef<ibValueRecordKeyObject>(m_metaObject);
		return true;
	case eBalance: pvarRetValue = lSizeArray > 1 ?
		ibValueManagerDataObjectAccumulationRegister::Balance(*paParams[0], *paParams[1]) : ibValueManagerDataObjectAccumulationRegister::Balance(*paParams[0]);
		return true;
	case eTurnover: pvarRetValue = lSizeArray > 2 ?
		ibValueManagerDataObjectAccumulationRegister::Turnovers(*paParams[0], paParams[1], paParams[2]) : ibValueManagerDataObjectAccumulationRegister::Turnovers(*paParams[0], *paParams[1]);
		return true;
	case eSelect:
		pvarRetValue = ibValue::CreateAndPrepareValueRef<ibValueSelectorRegisterDataObject>(m_metaObject);
		return true;
	case eGetForm:
	{
		ibValueGuid* guidVal = lSizeArray > 2 ? paParams[2]->ConvertToType<ibValueGuid>() : nullptr;
		pvarRetValue = m_metaObject->GetGenericForm(paParams[0]->GetString(),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr,
			guidVal ? ((ibGuid)*guidVal) : ibGuid());
		return true;
	}
	case eGetListForm:
	{
		ibValueGuid* guidVal = lSizeArray > 2 ? paParams[2]->ConvertToType<ibValueGuid>() : nullptr;
		pvarRetValue = m_metaObject->GetListForm(paParams[0]->GetString(),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr,
			guidVal ? ((ibGuid)*guidVal) : ibGuid());
		return true;
	}
	case eGetTemplate:
		pvarRetValue = m_metaObject->GetTemplate(paParams[0]->GetString());
		return true;
	}

	return ibValueManagerDataObject::CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
}