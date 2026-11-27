////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : chart of calculation types manager
////////////////////////////////////////////////////////////////////////////

#include "chartOfCalculationTypesManager.h"
#include "backend/metaData.h"

#include "commonObject.h"


const ibValueMetaObjectSharedModule* ibValueManagerDataObjectChartOfCalculationTypes::GetManagerModule() const { return m_metaObject->GetManagerModule(); }

#include "reference/reference.h"
#include "backend/system/value/valueGuid.h"   // GuidOf - GetRef reads its identifier

ibValueReferenceDataObject* ibValueManagerDataObjectChartOfCalculationTypes::EmptyRef() const
{
	return ibValueReferenceDataObject::Create(m_metaObject);
}

enum Func {
	eCreateCalculationType = 0,
	eCreateFolder,
	eSelect,
	eFindByCode,
	eFindByDescription,
	eGetForm,
	eGetListForm,
	eGetSelectForm,
	eGetTemplate,
	eEmptyRef,
	eGetRef
};

void ibValueManagerDataObjectChartOfCalculationTypes::FillManagerMethods(ibMemberTable& helper) const
{
	helper.AppendFunc(ibT("CreateCalculationType"), 1, ibT("CreateCalculationType(ref? : reference)"));
	helper.AppendFunc(ibT("CreateFolder"), 1, ibT("CreateFolder(ref? : reference)"));
	helper.AppendFunc(ibT("Select"), ibT("Select()"));
	helper.AppendFunc(ibT("FindByCode"), 1, ibT("FindByCode(code : string)"));
	helper.AppendFunc(ibT("FindByDescription"), 2, ibT("FindByDescription(descr : string, exact = true : boolean)"));
	helper.AppendFunc(ibT("GetForm"), 3, ibT("GetForm(name : string, owner : any, id : guid)"));
	helper.AppendFunc(ibT("GetListForm"), 3, ibT("GetListForm(name : string, owner : any, id : guid)"));
	helper.AppendFunc(ibT("GetSelectForm"), 3, ibT("GetSelectForm(name : string, owner : any, id : guid)"));
	helper.AppendFunc(ibT("GetTemplate"), 1, ibT("GetTemplate(name : string)"));
	helper.AppendFunc(ibT("EmptyRef"), ibT("EmptyRef()"));
	helper.AppendFunc(ibT("GetRef"), 1, ibT("GetRef(id : guid)"));
}

#include "selector/objectSelector.h"

bool ibValueManagerDataObjectChartOfCalculationTypes::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (m_members.GetMethodData(lMethodNum, this, &ibValueManagerDataObjectChartOfCalculationTypes::FillManagerMethods))
	{
	case eCreateCalculationType:
		pvarRetValue = m_metaObject->CreateNewObjectValue(ibObjectMode::OBJECT_ITEM, lSizeArray > 0 ? *paParams[0] : ibValue());
		return true;
	case eCreateFolder:
		pvarRetValue = m_metaObject->CreateNewObjectValue(ibObjectMode::OBJECT_FOLDER, lSizeArray > 0 ? *paParams[0] : ibValue());
		return true;
	case eSelect:
		pvarRetValue = new ibValueSelectorRecordDataObject(m_metaObject);
		return true;
	case eFindByCode:
		pvarRetValue = FindByCode(*paParams[0]);
		return true;
	case eFindByDescription:
		pvarRetValue = FindByDescription(*paParams[0], lSizeArray > 1 ? *paParams[1] : ibValue());
		return true;
	case eGetForm: {
		ibValueGuid* guidVal = lSizeArray > 2 ? paParams[2]->ConvertToType<ibValueGuid>() : nullptr;
		pvarRetValue = m_metaObject->GetObjectForm(ibFormRequest(lSizeArray > 0 ? paParams[0]->GetString() : ibString(), guidVal ? ((ibGuid)*guidVal) : ibGuid()),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr);
		return true;
	}
	case eGetListForm: {
		ibValueGuid* guidVal = lSizeArray > 2 ? paParams[2]->ConvertToType<ibValueGuid>() : nullptr;
		pvarRetValue = m_metaObject->GetListForm(ibFormRequest(lSizeArray > 0 ? paParams[0]->GetString() : ibString(), guidVal ? ((ibGuid)*guidVal) : ibGuid()),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr);
		return true;
	}
	case eGetSelectForm: {
		ibValueGuid* guidVal = lSizeArray > 2 ? paParams[2]->ConvertToType<ibValueGuid>() : nullptr;
		pvarRetValue = m_metaObject->GetSelectForm(ibFormRequest(lSizeArray > 0 ? paParams[0]->GetString() : ibString(), guidVal ? ((ibGuid)*guidVal) : ibGuid()),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr);
		return true;
	}
	case eGetTemplate:
		pvarRetValue = m_metaObject->GetTemplate(paParams[0]->GetString());
		return true;
	case eEmptyRef:
		pvarRetValue = EmptyRef();
		return true;
	case eGetRef:
		pvarRetValue = ibValueReferenceDataObject::Create(m_metaObject, GuidOf(*paParams[0]));
		return true;
	}

	return ibValueManagerDataObject::CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
}
