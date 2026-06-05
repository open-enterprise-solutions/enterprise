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
	helper.AppendFunc(wxT("CreateRecordSet"), wxT("CreateRecordSet()"));
	helper.AppendFunc(wxT("CreateRecordManager"), wxT("CreateRecordManager()"));
	helper.AppendFunc(wxT("CreateRecordKey"), wxT("CreateRecordKey()"));
	helper.AppendFunc(wxT("Get"), 1, wxT("Get(Filter...)"));
	helper.AppendFunc(wxT("GetFirst"), 3, wxT("GetFirst(beginOfPeriod, filter...)"));
	helper.AppendFunc(wxT("GetLast"), 2, wxT("GetLast(endOfPeriod, filter...)"));
	helper.AppendFunc(wxT("SliceFirst"), 2, wxT("SliceFirst(beginOfPeriod, filter...)"));
	helper.AppendFunc(wxT("SliceLast"), 2, wxT("SliceLast(endOfPeriod, filter...)"));
	helper.AppendFunc(wxT("Select"), wxT("Select()"));
	helper.AppendFunc(wxT("GetForm"), 3, wxT("GetForm(string, owner, guid)"));
	helper.AppendFunc(wxT("GetRecordForm"), 3, wxT("GetRecordForm(string, owner, guid)"));
	helper.AppendFunc(wxT("GetListForm"), 3, wxT("GetListForm(string, owner, guid)"));
	helper.AppendFunc(wxT("GetTemplate"), 1, wxT("GetTemplate(string)"));
}

#include "selector/objectSelector.h"

bool ibValueManagerDataObjectInformationRegister::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case eCreateRecordSet:
		pvarRetValue = m_metaObject->CreateRecordSetObjectValue();
		return true;
	case eCreateRecordManager:
		pvarRetValue = m_metaObject->CreateRecordManagerObjectValue();
		return true;
	case eCreateRecordKey:
		pvarRetValue = ibValue::CreateAndPrepareValueRef<ibValueRecordKeyObject>(m_metaObject);
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
	case eGetRecordForm:
	{
		ibValueRecordKeyObject* keyVal = lSizeArray > 2 ? paParams[2]->ConvertToType<ibValueRecordKeyObject>() : nullptr;
		pvarRetValue = m_metaObject->GetRecordForm(paParams[0]->GetString(),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr,
			keyVal ? keyVal->GetUniqueKey() : wxNullUniquePairKey);
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