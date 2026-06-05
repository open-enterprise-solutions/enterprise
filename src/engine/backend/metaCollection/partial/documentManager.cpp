////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : document manager
////////////////////////////////////////////////////////////////////////////

#include "documentManager.h"
#include "backend/metaData.h"

#include "commonObject.h"


const ibValueMetaObjectCommonModule* ibValueManagerDataObjectDocument::GetManagerModule() const
{
	return m_metaObject->GetManagerModule();
}

#include "reference/reference.h"

ibValueReferenceDataObject* ibValueManagerDataObjectDocument::EmptyRef()
{
	return ibValueReferenceDataObject::Create(m_metaObject);
}

enum Func {
	eCreateElement = 0,
	eSelect,
	eFindByNumber,
	eGetForm,
	eGetListForm,
	eGetSelectForm,
	eGetTemplate,
	eEmptyRef
};

#include "backend/metaData.h"

void ibValueManagerDataObjectDocument::FillManagerMethods(ibMemberTable& helper) const
{
	helper.AppendFunc(wxT("CreateDocument"), wxT("CreateDocument()"));
	helper.AppendFunc(wxT("Select"), wxT("Select()"));
	helper.AppendFunc(wxT("FindByNumber"), 2, wxT("FindByNumber(number : string, date)"));
	helper.AppendFunc(wxT("GetForm"), 3, wxT("GetForm(name : string, owner, id : guid)"));
	helper.AppendFunc(wxT("GetListForm"), 3, wxT("GetListForm(name : string, owner : any, id : guid)"));
	helper.AppendFunc(wxT("GetSelectForm"), 3, wxT("GetSelectForm(name : string, owner : any, id : guid)"));
	helper.AppendFunc(wxT("GetTemplate"), 1, wxT("GetTemplate(name : string)"));
	helper.AppendFunc(wxT("EmptyRef"), wxT("EmptyRef()"));
}

#include "selector/objectSelector.h"

bool ibValueManagerDataObjectDocument::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case eCreateElement:
		pvarRetValue = m_metaObject->CreateObjectValue();
		return true;
	case eSelect:
		pvarRetValue = ibValue::CreateAndPrepareValueRef<ibValueSelectorRecordDataObject>(m_metaObject);
		return true;
	case eFindByNumber:
	{
		pvarRetValue = FindByNumber(*paParams[0],
			lSizeArray > 1 ? *paParams[1] : ibValue()
		);
		return true;
	}
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
	case eEmptyRef:
		pvarRetValue = EmptyRef();
		return true;
	}

	return ibValueManagerDataObject::CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
}