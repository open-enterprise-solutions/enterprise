////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : calculation register manager
////////////////////////////////////////////////////////////////////////////

#include "calculationRegisterManager.h"
#include "backend/metaData.h"

#include "commonObject.h"


const ibValueMetaObjectCommonModule* ibValueManagerDataObjectCalculationRegister::GetManagerModule() const {
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
	// Our own ordinal, not the table index — ibValueManagerDataObject::BuiltinMethodNum says why. The
	// eleven managers that existed on 2026-09-09 moved here in 16e8324c; this one was imported from a
	// snapshot of 2026-08-16 and still switched on the raw index, so the first procedure its manager
	// module declared would have shifted every built-in verb onto its neighbour.
	switch (BuiltinMethodNum(lMethodNum))
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
