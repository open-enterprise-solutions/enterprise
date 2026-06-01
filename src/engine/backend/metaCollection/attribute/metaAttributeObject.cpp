////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : meta-attribues
////////////////////////////////////////////////////////////////////////////

#include "metaAttributeObject.h"
#include "backend/metadata.h"

////////////////////////////////////////////////////////////////////////////



//***********************************************************************
//*                         Attributes                                  * 
//***********************************************************************

#include "backend/objCtor.h"

bool ibValueMetaObjectAttributeBase::ContainType(const ibValueTypes& valType) const
{
	return GetTypeDesc().ContainType(valType);
}

bool ibValueMetaObjectAttributeBase::ContainType(const ibClassID& clsid) const
{
	return GetTypeDesc().ContainType(clsid);
}

bool ibValueMetaObjectAttributeBase::EqualType(const ibClassID& clsid, const ibTypeDescription& rhs) const
{
	return GetTypeDesc().EqualType(clsid, rhs);
}

bool ibValueMetaObjectAttributeBase::ContainMetaType(ibCtorObjectMetaType type) const
{
	for (auto& clsid : GetTypeDesc().GetClsidList()) {
		const ibCtorMetaValueType* typeCtor = m_metaData->GetTypeCtor(clsid);
		if (typeCtor != nullptr && typeCtor->GetMetaTypeCtor() == type)
			return true;
	}

	return false;
}

/////////////////////////////////////////////////////////////////////////

ibItemMode ibValueMetaObjectAttribute::GetItemMode() const {
	ibValueMetaObjectRecordDataHierarchyMutableRef* metaObject =
		dynamic_cast<ibValueMetaObjectRecordDataHierarchyMutableRef*>(m_parent);
	if (metaObject != nullptr)
		return m_propertyItemMode->GetValueAsEnum();
	return ibItemMode::ibItemMode_Item;
}

ibSelectMode ibValueMetaObjectAttribute::GetSelectMode() const
{
	if (GetTypeDesc().GetClsidCount() > 1)
		return ibSelectMode::ibSelectMode_Items;
	const ibCtorMetaValueType* so = m_metaData->GetTypeCtor(GetTypeDesc().GetFirstClsid());
	if (so != nullptr) {
		const ibValueMetaObjectRecordDataHierarchyMutableRef* metaObject = dynamic_cast<const ibValueMetaObjectRecordDataHierarchyMutableRef*>(so->GetMetaObject());
		if (so->GetMetaTypeCtor() == ibCtorObjectMetaType::ibCtorObjectMetaType_Reference && metaObject != nullptr)
			return (ibSelectMode)m_propertySelectMode->GetValueAsInteger();
		return ibSelectMode::ibSelectMode_Items;
	}
	return ibSelectMode::ibSelectMode_Items;
}

/////////////////////////////////////////////////////////////////////////

ibSelectorDataType ibValueMetaObjectAttributeBase::GetFilterDataType() const
{
	ibValueMetaObjectGenericData* metaObject = dynamic_cast<ibValueMetaObjectGenericData*>(m_parent);
	if (metaObject != nullptr) return metaObject->GetFilterDataType();
	return ibSelectorDataType::ibSelectorDataType_reference;
}

/////////////////////////////////////////////////////////////////////////

ibValue ibValueMetaObjectAttributeBase::CreateValue() const
{
	ibValue* refData = CreateValueRef();
	if (refData == nullptr)
		return ibValue();
	return refData;
}

ibValue* ibValueMetaObjectAttributeBase::CreateValueRef() const
{
	if (m_defValue.IsEmpty()) 
		return ibBackendTypeConfigFactory::CreateValueRef();
	return new ibValue(m_defValue);
}

//***********************************************************************
//*								Events								    *
//***********************************************************************

bool ibValueMetaObjectAttributeBase::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	return ibValueMetaObject::OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectAttributeBase::OnDeleteMetaObject()
{
	return ibValueMetaObject::OnDeleteMetaObject();
}

bool ibValueMetaObjectAttributeBase::OnReloadMetaObject()
{
	ibValueMetaObject* metaObject = GetParent();
	wxASSERT(metaObject);
	if (metaObject->OnReloadMetaObject())
		return ibValueMetaObject::OnReloadMetaObject();
	return false;
}

///////////////////////////////////////////////////////////////////////////

bool ibValueMetaObjectAttributeBase::OnBeforeRunMetaObject(int flags)
{
	return ibValueMetaObject::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectAttributeBase::OnAfterRunMetaObject(int flags)
{
	if ((flags & newObjectFlag) != 0 || (flags & pasteObjectFlag) != 0) OnReloadMetaObject();
	return ibValueMetaObject::OnAfterRunMetaObject(flags);
}

//***********************************************************************
//*                               Data				                    *
//***********************************************************************

bool ibValueMetaObjectAttribute::LoadData(ibReaderMemory& reader)
{
	if (!m_propertyType->LoadData(reader))
		return false;

	m_propertyFillCheck->LoadData(reader);
	m_propertyItemMode->LoadData(reader);
	m_propertySelectMode->LoadData(reader);

	return true;
}

bool ibValueMetaObjectAttribute::SaveData(ibWriterMemory& writer)
{
	if (!m_propertyType->SaveData(writer))
		return false;

	m_propertyFillCheck->SaveData(writer);
	m_propertyItemMode->SaveData(writer);
	m_propertySelectMode->SaveData(writer);
	return true;
}

bool ibValueMetaObjectAttributePredefined::LoadData(ibReaderMemory& reader)
{
	if (!ibTypeDescriptionMemory::LoadData(reader, m_typeDesc))
		return false;

	m_fillCheck = reader.r_u8();
	return true;
}

bool ibValueMetaObjectAttributePredefined::SaveData(ibWriterMemory& writer)
{
	if (!ibTypeDescriptionMemory::SaveData(writer, m_typeDesc))
		return false;

	writer.w_u8(m_fillCheck);
	return true;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectAttribute, "Attribute", g_metaAttributeCLSID);
METADATA_TYPE_REGISTER(ibValueMetaObjectAttributePredefined, "PredefinedAttribute", g_metaPredefinedAttributeCLSID);