////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : enumeration manager
////////////////////////////////////////////////////////////////////////////

#include "enumerationManager.h"
#include "backend/metaData.h"

#include "commonObject.h"
#include "reference/reference.h"


const ibValueMetaObjectCommonModule* ibValueManagerDataObjectEnumeration::GetManagerModule() const { return m_metaObject->GetManagerModule(); }

enum Func {
	eGetForm,
	eGetListForm,
	eGetSelectForm,
	eGetTemplate,
};

void ibValueManagerDataObjectEnumeration::FillManagerMethods(ibMemberTable& helper) const
{
	helper.AppendFunc(wxT("GetForm"), 3, wxT("GetForm(name : string, owner : any, id : guid)"));
	helper.AppendFunc(wxT("GetListForm"), 3, wxT("GetListForm(name : string, owner : any, id : guid)"));
	helper.AppendFunc(wxT("GetSelectForm"), 3, wxT("GetSelectForm(name : string, owner : any, id : guid)"));
	helper.AppendFunc(wxT("GetTemplate"), 1, wxT("GetTemplate(name : string)"));

	//fill custom attributes
	for (auto object : m_metaObject->GetEnumObjectArray()) {

		helper.AppendProp(
			object->GetName(),
			true, false,
			object->GetMetaID(),
			wxNOT_FOUND
		);
	}
}

//****************************************************************************
//*                              Override attribute                          *
//****************************************************************************
bool ibValueManagerDataObjectEnumeration::SetPropVal(const long lPropNum, ibValue& cValue)
{
	return false;
}

bool ibValueManagerDataObjectEnumeration::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const ibValueMetaObject* valueObject =
		m_metaObject->FindEnumObjectByFilter<ibMetaID>(m_members.GetPropData(lPropNum));

	if (valueObject == nullptr)
		return false;

	pvarPropVal = ibValueReferenceDataObject::Create(m_metaObject, valueObject->GetGuid());
	return true;
}

bool ibValueManagerDataObjectEnumeration::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
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
	case eGetSelectForm:
	{
		ibValueGuid* guidVal = lSizeArray > 2 ? paParams[2]->ConvertToType<ibValueGuid>() : nullptr;
		pvarRetValue = m_metaObject->GetSelectForm(paParams[0]->GetString(),
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