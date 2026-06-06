////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : catalog manager
////////////////////////////////////////////////////////////////////////////

#include "catalogManager.h"
#include "backend/metaData.h"

#include "commonObject.h"


const ibValueMetaObjectCommonModule* ibValueManagerDataObjectCatalog::GetManagerModule() const { return m_metaObject->GetManagerModule(); }

#include "reference/reference.h"

ibValueReferenceDataObject* ibValueManagerDataObjectCatalog::EmptyRef() const
{
	return ibValueReferenceDataObject::Create(m_metaObject);
}

enum Func {
	eCreateElement = 0,
	eCreateGroup,
	eSelect,
	eFindByCode,
	eFindByDescription,
	eGetForm,
	eGetListForm,
	eGetSelectForm,
	eGetTemplate,
	eEmptyRef
};

void ibValueManagerDataObjectCatalog::FillManagerMethods(ibMemberTable& helper) const
{
	// Composes after FillMembers (common-module methods) + FillPredefined (props),
	// bound earlier in the ctor chain. Order is load-bearing — CallAsFunc switches
	// on the method index (eCreateElement = 0 …, relative to these appends).
	helper.AppendFunc(wxT("CreateElement"), wxT("CreateElement()"));
	helper.AppendFunc(wxT("CreateGroup"), wxT("CreateGroup()"));
	helper.AppendFunc(wxT("Select"), wxT("Select()"));
	helper.AppendFunc(wxT("FindByCode"), 1, wxT("FindByCode(code : string)"));
	helper.AppendFunc(wxT("FindByDescription"), 1, wxT("FindByDescription(descr : string)"));
	helper.AppendFunc(wxT("GetForm"), 3, wxT("GetForm(name : string, owner : any, id : guid)"));
	helper.AppendFunc(wxT("GetListForm"), 3, wxT("GetListForm(name : string, owner : any, id : guid)"));
	helper.AppendFunc(wxT("GetSelectForm"), 3, wxT("GetSelectForm(name : string, owner : any, id : guid)"));
	helper.AppendFunc(wxT("GetTemplate"), 1, wxT("GetTemplate(name : string)"));
	helper.AppendFunc(wxT("EmptyRef"), wxT("EmptyRef()"));
}

#include "selector/objectSelector.h"

bool ibValueManagerDataObjectCatalog::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case eCreateElement:
		pvarRetValue = m_metaObject->CreateObjectValue(ibObjectMode::OBJECT_ITEM);
		return true;
	case eCreateGroup:
		pvarRetValue = m_metaObject->CreateObjectValue(ibObjectMode::OBJECT_FOLDER);
		return true;
	case eSelect:
		pvarRetValue = new ibValueSelectorRecordDataObject(m_metaObject);
		return true;
	case eFindByCode:
		pvarRetValue = FindByCode(*paParams[0]);
		return true;
	case eFindByDescription:
		pvarRetValue = FindByDescription(*paParams[0]);
		return true;
	case eGetForm: {
		ibValueGuid* guidVal = lSizeArray > 2 ? paParams[2]->ConvertToType<ibValueGuid>() : nullptr;
		pvarRetValue = m_metaObject->GetGenericForm(lSizeArray > 0 ? paParams[0]->GetString() : wxString(wxEmptyString),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr,
			guidVal ? ((ibGuid)*guidVal) : ibGuid());
		return true;
	}
	case eGetListForm: {
		ibValueGuid* guidVal = lSizeArray > 2 ? paParams[2]->ConvertToType<ibValueGuid>() : nullptr;
		pvarRetValue = m_metaObject->GetListForm(lSizeArray > 0 ? paParams[0]->GetString() : wxString(wxEmptyString),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr,
			guidVal ? ((ibGuid)*guidVal) : ibGuid());
		return true;
	}
	case eGetSelectForm: {
		ibValueGuid* guidVal = lSizeArray > 2 ? paParams[2]->ConvertToType<ibValueGuid>() : nullptr;
		pvarRetValue = m_metaObject->GetSelectForm(lSizeArray > 0 ? paParams[0]->GetString() : wxString(wxEmptyString),
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