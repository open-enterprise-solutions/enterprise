////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : constants manager
////////////////////////////////////////////////////////////////////////////

#include "constantManager.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibValueManagerDataObjectConstant, ibValue);

#include "backend/metaData.h"
#include "backend/objCtor.h"

ibClassID ibValueManagerDataObjectConstant::GetClassType() const
{
	const ibMetaData* metaData = m_metaObject->GetMetaData();
	wxASSERT(metaData);
	const ibCtorMetaValueType* clsFactory =
		metaData->GetTypeCtor(m_metaObject, ibCtorObjectMetaType::ibCtorObjectMetaType_Manager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString ibValueManagerDataObjectConstant::GetClassName() const
{
	const ibMetaData* metaData = m_metaObject->GetMetaData();
	wxASSERT(metaData);
	const ibCtorMetaValueType* clsFactory =
		metaData->GetTypeCtor(m_metaObject, ibCtorObjectMetaType::ibCtorObjectMetaType_Manager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString ibValueManagerDataObjectConstant::GetString() const
{
	const ibMetaData* metaData = m_metaObject->GetMetaData();
	wxASSERT(metaData);
	const ibCtorMetaValueType* clsFactory =
		metaData->GetTypeCtor(m_metaObject, ibCtorObjectMetaType::ibCtorObjectMetaType_Manager);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

ibValue::ibValueMethodHelper ibValueManagerDataObjectConstant::m_methodHelper;

enum Func {
	enSet = 0,
	enGet
};

void ibValueManagerDataObjectConstant::PrepareNames() const
{
	m_methodHelper.ClearHelper();
	m_methodHelper.AppendFunc(wxT("Set"), 1, wxT("Set(value : any)"));
	m_methodHelper.AppendFunc(wxT("Get"), wxT("Get()"));
}

bool ibValueManagerDataObjectConstant::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	ibValuePtr<ibValueRecordDataObjectConstant> recordDataObjectValue(m_metaObject->CreateRecordDataObjectValue());

	switch (lMethodNum)
	{
	case enSet:
		recordDataObjectValue->SetConstValue(*paParams[0]);
		return true;
	case enGet:
		pvarRetValue = recordDataObjectValue->GetConstValue();
		return true;
	}

	return false;
}