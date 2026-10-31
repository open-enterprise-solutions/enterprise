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
	helper.AppendFunc(ibT("CreateDocument"), ibT("CreateDocument()"));
	helper.AppendFunc(ibT("Select"), ibT("Select()"));
	helper.AppendFunc(ibT("FindByNumber"), 2, ibT("FindByNumber(number : string, date)"));
	helper.AppendFunc(ibT("GetForm"), 3, ibT("GetForm(name : string, owner, id : guid)"));
	helper.AppendFunc(ibT("GetListForm"), 3, ibT("GetListForm(name : string, owner : any, id : guid)"));
	helper.AppendFunc(ibT("GetSelectForm"), 3, ibT("GetSelectForm(name : string, owner : any, id : guid)"));
	helper.AppendFunc(ibT("GetTemplate"), 1, ibT("GetTemplate(name : string)"));
	helper.AppendFunc(ibT("EmptyRef"), ibT("EmptyRef()"));
}

#include "selector/objectSelector.h"

bool ibValueManagerDataObjectDocument::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	// 🛑 OUR OWN ORDINAL, NOT THE TABLE INDEX — see ibValueManagerDataObject::BuiltinMethodNum.
	// The manager module's exported methods share this table, so the raw index means a
	// different verb for every configuration that declares one.
	switch (BuiltinMethodNum(lMethodNum))
	{
	case eCreateElement:
		pvarRetValue = m_metaObject->CreateObjectValue();
		return true;
	case eSelect:
		pvarRetValue = new ibValueSelectorRecordDataObject(m_metaObject);
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
	case eGetSelectForm:
	{
		ibValueGuid* guidVal = lSizeArray > 2 ? paParams[2]->ConvertToType<ibValueGuid>() : nullptr;
		pvarRetValue = m_metaObject->GetSelectForm(ibFormRequest(paParams[0]->GetString(), guidVal ? ((ibGuid)*guidVal) : ibGuid()),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr);
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