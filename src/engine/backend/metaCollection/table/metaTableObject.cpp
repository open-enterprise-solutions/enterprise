////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : meta-tables
////////////////////////////////////////////////////////////////////////////

#include "metaTableObject.h"
#include "backend/metaData.h"


//***********************************************************************
//*                         Attributes                                  * 
//***********************************************************************

#include "backend/objCtor.h"
#include "backend/metaCollection/partial/commonObject.h"

ibTypeDescription ibValueMetaObjectTableData::GetTypeDesc() const
{
	const ibCtorMetaValueType* typeCtor = m_metaData->GetTypeCtor(this, ibCtorObjectMetaType::ibCtorObjectMetaType_TabularSection);
	wxASSERT(typeCtor);
	if (typeCtor != nullptr) return ibTypeDescription(typeCtor->GetClassType());
	return ibTypeDescription();
}

////////////////////////////////////////////////////////////////////////////

ibValueMetaObjectTableData::ibValueMetaObjectTableData() : ibValueMetaObjectCompositeData()
{
}

ibValueMetaObjectTableData::~ibValueMetaObjectTableData()
{
	//wxDELETE(m_numberLine);
}

bool ibValueMetaObjectTableData::LoadData(ibReaderMemory& dataReader)
{
	m_propertyUse->SetValue(dataReader.r_u16());

	//load default attributes:
	return (*m_propertyNumberLine)->LoadMeta(dataReader);
}

bool ibValueMetaObjectTableData::SaveData(ibWriterMemory& dataWritter)
{
	dataWritter.w_u16(m_propertyUse->GetValueAsInteger());

	//save default attributes:
	return (*m_propertyNumberLine)->SaveMeta(dataWritter);;
}

//***********************************************************************
//*								Events								    *
//***********************************************************************

bool ibValueMetaObjectTableData::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObject::OnCreateMetaObject(metaData, flags))
		return false;
	if (!(*m_propertyNumberLine)->OnCreateMetaObject(metaData, flags)) {
		return false;
	}
	return true;
}

bool ibValueMetaObjectTableData::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyNumberLine)->OnLoadMetaObject(metaData))
		return false;

	return ibValueMetaObject::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectTableData::OnSaveMetaObject(int flags)
{
	if (!(*m_propertyNumberLine)->OnSaveMetaObject(flags))
		return false;

	return ibValueMetaObject::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectTableData::OnDeleteMetaObject()
{
	if (!(*m_propertyNumberLine)->OnDeleteMetaObject())
		return false;

	return ibValueMetaObject::OnDeleteMetaObject();
}

bool ibValueMetaObjectTableData::OnReloadMetaObject()
{
	ibValueMetaObject* metaObject = GetParent();
	wxASSERT(metaObject);
	if (metaObject->OnReloadMetaObject())
		return ibValueMetaObject::OnReloadMetaObject();
	return false;
}

bool ibValueMetaObjectTableData::OnBeforeRunMetaObject(int flags)
{
	if (!(*m_propertyNumberLine)->OnBeforeRunMetaObject(flags))
		return false;
	ibValueMetaObjectRecordData* metaObject = dynamic_cast<ibValueMetaObjectRecordData*>(GetParent());
	wxASSERT(metaObject);
	if (metaObject != nullptr) {
		registerTabularSection();
		registerTabularSection_String();
	}

	return ibValueMetaObject::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectTableData::OnAfterRunMetaObject(int flags)
{
	if ((flags & newObjectFlag) != 0 || (flags & pasteObjectFlag) != 0) 
		OnReloadMetaObject();
	return ibValueMetaObject::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectTableData::OnAfterCloseMetaObject()
{
	if (!(*m_propertyNumberLine)->OnAfterCloseMetaObject())
		return false;
	ibValueMetaObjectRecordData* metaObject = dynamic_cast<ibValueMetaObjectRecordData*>(GetParent());
	wxASSERT(metaObject);
	if (metaObject != nullptr) {
		unregisterTabularSection();
		unregisterTabularSection_String();
	}
	return ibValueMetaObject::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectTableData, "TabularSection", g_metaTableCLSID);