////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : meta-attribues
////////////////////////////////////////////////////////////////////////////

#include "metaAttributeObject.h"
#include "backend/metadataConfiguration.h"

////////////////////////////////////////////////////////////////////////////

wxIMPLEMENT_ABSTRACT_CLASS(IMetaObjectAttribute, IMetaObject);

wxIMPLEMENT_DYNAMIC_CLASS(CMetaObjectAttribute, IMetaObjectAttribute);
wxIMPLEMENT_DYNAMIC_CLASS(CMetaObjectAttributeDefault, IMetaObjectAttribute);

//***********************************************************************
//*                         Attributes                                  * 
//***********************************************************************

#include "backend/objCtor.h"

bool IMetaObjectAttribute::ContainMetaType(eCtorMetaType type) const
{
	for (auto& clsid : GetClsids()) {
		IMetaValueTypeCtor* typeCtor = m_metaData->GetTypeCtor(clsid);
		if (typeCtor != nullptr && typeCtor->GetMetaTypeCtor() == type) {
			return true;
		}
	}

	return false;
}

/////////////////////////////////////////////////////////////////////////

CMetaObjectAttribute::CMetaObjectAttribute(const eValueTypes& valType) :
	IMetaObjectAttribute(valType)
{
}

eItemMode CMetaObjectAttribute::GetItemMode() const {
	IMetaObjectRecordDataFolderMutableRef* metaObject =
		dynamic_cast<IMetaObjectRecordDataFolderMutableRef*>(m_parent);
	if (metaObject != nullptr)
		return (eItemMode)m_propertyItemMode->GetValueAsInteger();
	return eItemMode::eItemMode_Item;
}

eSelectMode CMetaObjectAttribute::GetSelectMode() const
{
	if (GetClsidCount() > 1)
		return eSelectMode::eSelectMode_Items;
	IMetaValueTypeCtor* so = GetMetaData()->GetTypeCtor(GetFirstClsid());
	if (so != nullptr) {
		IMetaObjectRecordDataFolderMutableRef* metaObject = dynamic_cast<IMetaObjectRecordDataFolderMutableRef*>(so->GetMetaObject());
		if (so->GetMetaTypeCtor() == eCtorMetaType::eCtorMetaType_Reference && metaObject != nullptr)
			return (eSelectMode)m_propertySelectMode->GetValueAsInteger();
		return eSelectMode::eSelectMode_Items;
	}
	return eSelectMode::eSelectMode_Items;
}

/////////////////////////////////////////////////////////////////////////

CValue IMetaObjectAttribute::CreateValue() const
{
	CValue* refData = CreateValueRef();
	if (refData == nullptr)
		return CValue();
	return refData;
}

CValue* IMetaObjectAttribute::CreateValueRef() const
{
	if (m_defValue.IsEmpty())
		return ITypeAttribute::CreateValueRef();

	return new CValue(m_defValue);
}

//***********************************************************************
//*								Events								    *
//***********************************************************************

bool IMetaObjectAttribute::OnCreateMetaObject(IMetaData* metaData)
{
	return IMetaObject::OnCreateMetaObject(metaData);
}

bool IMetaObjectAttribute::OnDeleteMetaObject()
{
	return IMetaObject::OnDeleteMetaObject();
}

bool IMetaObjectAttribute::OnReloadMetaObject()
{
	IMetaObject* metaObject = GetParent();
	wxASSERT(metaObject);
	if (metaObject->OnReloadMetaObject())
		return IMetaObject::OnReloadMetaObject();
	return false;
}

///////////////////////////////////////////////////////////////////////////

bool IMetaObjectAttribute::OnBeforeRunMetaObject(int flags)
{
	return IMetaObject::OnBeforeRunMetaObject(flags);
}

bool IMetaObjectAttribute::OnAfterRunMetaObject(int flags)
{
	if ((flags & newObjectFlag) != 0) OnReloadMetaObject();
	return IMetaObject::OnAfterRunMetaObject(flags);
}

//***********************************************************************
//*                               Data				                    *
//***********************************************************************

bool CMetaObjectAttribute::LoadData(CMemoryReader& reader)
{
	if (!ITypeWrapper::LoadTypeData(reader))
		return false;

	m_propertyFillCheck->SetValue(reader.r_u8());
	m_propertyItemMode->SetValue(reader.r_u16());
	m_propertySelectMode->SetValue(reader.r_u16());
	return true;
}

bool CMetaObjectAttribute::SaveData(CMemoryWriter& writer)
{
	if (!ITypeWrapper::SaveTypeData(writer))
		return false;

	writer.w_u8(m_propertyFillCheck->GetValueAsBoolean());
	writer.w_u16(m_propertyItemMode->GetValueAsInteger());
	writer.w_u16(m_propertySelectMode->GetValueAsInteger());
	return true;
}

bool CMetaObjectAttributeDefault::LoadData(CMemoryReader& reader)
{
	if (!ITypeWrapper::LoadTypeData(reader))
		return false;

	m_fillCheck = reader.r_u8();
	return true;
}

bool CMetaObjectAttributeDefault::SaveData(CMemoryWriter& writer)
{
	if (!ITypeWrapper::SaveTypeData(writer))
		return false;

	writer.w_u8(m_fillCheck);
	return true;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(CMetaObjectAttribute, "attribute", g_metaAttributeCLSID);
METADATA_TYPE_REGISTER(CMetaObjectAttributeDefault, "defaultAttribute", g_metaDefaultAttributeCLSID);