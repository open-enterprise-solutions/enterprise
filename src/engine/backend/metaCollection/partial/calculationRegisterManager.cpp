////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : calculation register manager
////////////////////////////////////////////////////////////////////////////

#include "calculationRegisterManager.h"
#include "backend/metaData.h"

#include "commonObject.h"


const ibValueMetaObjectSharedModule* ibValueManagerDataObjectCalculationRegister::GetManagerModule() const {
	return m_metaObject->GetManagerModule();
}

// No CreateRecordManager and no GetRecordForm: a calculation register holds movements, written as
// their recorder's set — the same surface the accumulation register offers (calculationRegister.h).
enum {
	eCreateRecordSet,
	eCreateRecordKey,
	eGet,
	eGetBase,
	eSelect,
	eGetForm,
	eGetListForm,
	eGetTemplate,
};

void ibValueManagerDataObjectCalculationRegister::FillManagerMethods(ibMemberTable& helper) const
{
	helper.AppendFunc(ibT("CreateRecordSet"), ibT("CreateRecordSet()"));
	helper.AppendFunc(ibT("CreateRecordKey"), ibT("CreateRecordKey()"));
	// TWO ARGUMENTS, because there are two forms: Get(filter) and Get(period, filter).
	helper.AppendFunc(ibT("Get"), 2, ibT("Get(Period, Filter...)"));
	helper.AppendFunc(ibT("GetBase"), 4, ibT("GetBase(Filter, Resources, Dimensions, Sections)"));
	helper.AppendFunc(ibT("Select"), ibT("Select()"));
	helper.AppendFunc(ibT("GetForm"), 3, ibT("GetForm(string, owner, guid)"));
	helper.AppendFunc(ibT("GetListForm"), 3, ibT("GetListForm(string, owner, guid)"));
	helper.AppendFunc(ibT("GetTemplate"), 1, ibT("GetTemplate(string)"));
}

#include "selector/objectSelector.h"

bool ibValueManagerDataObjectCalculationRegister::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (m_members.GetMethodData(lMethodNum, this, &ibValueManagerDataObjectCalculationRegister::FillManagerMethods))
	{
	case eCreateRecordSet:
		pvarRetValue = m_metaObject->CreateRecordSetObjectValue();
		return true;
	case eCreateRecordKey:
		pvarRetValue = new ibValueRecordKeyObject(m_metaObject);
		return true;
	case eGet:
		pvarRetValue = lSizeArray > 1 ?
			ibValueManagerDataObjectCalculationRegister::Get(*paParams[0], *paParams[1])
			: lSizeArray > 0 ?
			ibValueManagerDataObjectCalculationRegister::Get(*paParams[0]) :
			ibValueManagerDataObjectCalculationRegister::Get();
		return true;
	case eGetBase:
		pvarRetValue = ibValueManagerDataObjectCalculationRegister::GetBase(
			lSizeArray > 0 ? *paParams[0] : ibValue(), lSizeArray > 1 ? *paParams[1] : ibValue(),
			lSizeArray > 2 ? *paParams[2] : ibValue(), lSizeArray > 3 ? *paParams[3] : ibValue());
		return true;
	case eSelect:
		pvarRetValue = new ibValueSelectorRegisterDataObject(m_metaObject);
		return true;
	case eGetForm:
	{
		ibValueGuid* guidVal = lSizeArray > 2 ? paParams[2]->ConvertToType<ibValueGuid>() : nullptr;
		pvarRetValue = m_metaObject->GetListForm(ibFormRequest(paParams[0]->GetString(), guidVal ? ((ibGuid)*guidVal) : ibGuid()),
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
