////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : information register manager
////////////////////////////////////////////////////////////////////////////

#include "informationRegisterManager.h"
#include "backend/metaData.h"

#include "commonObject.h"


const ibValueMetaObjectCommonModule* ibValueManagerDataObjectInformationRegister::GetManagerModule() const {
	return m_metaObject->GetManagerModule();
}

enum {
	eCreateRecordSet,
	eCreateRecordManager,
	eCreateRecordKey,
	eGet,
	eGetFirst,
	eGetLast,
	eSliceFirst,
	eSliceLast,
	eSelect,
	eGetForm,
	eGetRecordForm,
	eGetListForm,
	eGetTemplate,
};

void ibValueManagerDataObjectInformationRegister::FillManagerMethods(ibMemberTable& helper) const
{
	helper.AppendFunc(ibT("CreateRecordSet"), ibT("CreateRecordSet()"));
	helper.AppendFunc(ibT("CreateRecordManager"), ibT("CreateRecordManager()"));
	helper.AppendFunc(ibT("CreateRecordKey"), ibT("CreateRecordKey()"));
	// TWO ARGUMENTS, because there are two forms: Get(filter) and Get(period, filter). Declared as
	// one, the periodic form was unreachable — the interpreter refuses a call with more arguments
	// than the helper declares, so the branch existed and could not be entered.
	helper.AppendFunc(ibT("Get"), 2, ibT("Get(Period, Filter...)"));
	helper.AppendFunc(ibT("GetFirst"), 3, ibT("GetFirst(beginOfPeriod, filter...)"));
	helper.AppendFunc(ibT("GetLast"), 2, ibT("GetLast(endOfPeriod, filter...)"));
	helper.AppendFunc(ibT("SliceFirst"), 2, ibT("SliceFirst(beginOfPeriod, filter...)"));
	helper.AppendFunc(ibT("SliceLast"), 2, ibT("SliceLast(endOfPeriod, filter...)"));
	helper.AppendFunc(ibT("Select"), ibT("Select()"));
	helper.AppendFunc(ibT("GetForm"), 3, ibT("GetForm(string, owner, guid)"));
	helper.AppendFunc(ibT("GetRecordForm"), 3, ibT("GetRecordForm(string, owner, guid)"));
	helper.AppendFunc(ibT("GetListForm"), 3, ibT("GetListForm(string, owner, guid)"));
	helper.AppendFunc(ibT("GetTemplate"), 1, ibT("GetTemplate(string)"));
}

#include "selector/objectSelector.h"

bool ibValueManagerDataObjectInformationRegister::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	// Our own ordinal, not the table index — ibValueManagerDataObject::BuiltinMethodNum says why.
	switch (BuiltinMethodNum(lMethodNum))
	{
	case eCreateRecordSet:
		pvarRetValue = m_metaObject->CreateRecordSetObjectValue();
		return true;
	case eCreateRecordManager:
		pvarRetValue = m_metaObject->CreateRecordManagerObjectValue();
		return true;
	case eCreateRecordKey:
		pvarRetValue = new ibValueRecordKeyObject(m_metaObject);
		return true;
	case eGet:
		pvarRetValue = lSizeArray > 1 ?
			ibValueManagerDataObjectInformationRegister::Get(*paParams[0], *paParams[1])
			: lSizeArray > 0 ?
			ibValueManagerDataObjectInformationRegister::Get(*paParams[0]) :
			ibValueManagerDataObjectInformationRegister::Get();
		return true;
	case eGetFirst:
		pvarRetValue = lSizeArray > 1 ?
			ibValueManagerDataObjectInformationRegister::GetFirst(*paParams[0], *paParams[1])
			: ibValueManagerDataObjectInformationRegister::GetFirst(*paParams[0]);
		return true;
	case eGetLast:
		pvarRetValue = lSizeArray > 1 ?
			ibValueManagerDataObjectInformationRegister::GetLast(*paParams[0], *paParams[1])
			: ibValueManagerDataObjectInformationRegister::GetLast(*paParams[0]);
		return true;
	case eSliceFirst:
		pvarRetValue = lSizeArray > 1 ?
			ibValueManagerDataObjectInformationRegister::SliceFirst(*paParams[0], *paParams[1])
			: ibValueManagerDataObjectInformationRegister::SliceFirst(*paParams[0]);
		return true;
	case eSliceLast:
		pvarRetValue = lSizeArray > 1 ?
			ibValueManagerDataObjectInformationRegister::SliceLast(*paParams[0], *paParams[1])
			: ibValueManagerDataObjectInformationRegister::SliceLast(*paParams[0]);
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
	case eGetRecordForm:
	{
		ibValueRecordKeyObject* keyVal = lSizeArray > 2 ? paParams[2]->ConvertToType<ibValueRecordKeyObject>() : nullptr;
		pvarRetValue = m_metaObject->GetRecordForm(
			ibFormRequest(paParams[0]->GetString(), keyVal ? keyVal->GetUniqueKey() : wxNullUniquePairKey),
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