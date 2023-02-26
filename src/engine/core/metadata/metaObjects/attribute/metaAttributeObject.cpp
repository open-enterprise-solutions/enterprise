////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : meta-attribues
////////////////////////////////////////////////////////////////////////////

#include "metaAttributeObject.h"
#include "core/metadata/metadata.h"

////////////////////////////////////////////////////////////////////////////

wxIMPLEMENT_ABSTRACT_CLASS(IMetaAttributeObject, IMetaObject);

wxIMPLEMENT_DYNAMIC_CLASS(CMetaAttributeObject, IMetaAttributeObject);
wxIMPLEMENT_DYNAMIC_CLASS(CMetaDefaultAttributeObject, IMetaAttributeObject);

//***********************************************************************
//*                         Attributes                                  * 
//***********************************************************************

#include "core/metadata/singleClass.h"

bool IMetaAttributeObject::ContainMetaType(eMetaObjectType type) const
{
	for (auto clsid : GetClsids()) {
		IMetaTypeObjectValueSingle* singleObject = m_metaData->GetTypeObject(clsid);
		if (singleObject && singleObject->GetMetaType() == type) {
			return true;
		}
	}

	return false;
}

/////////////////////////////////////////////////////////////////////////

CMetaAttributeObject::CMetaAttributeObject(const eValueTypes& valType) :
	IMetaAttributeObject(valType)
{
}

eItemMode CMetaAttributeObject::GetItemMode() const {
	IMetaObjectRecordDataFolderMutableRef* metaObject =
		dynamic_cast<IMetaObjectRecordDataFolderMutableRef*>(m_parent);
	if (metaObject != NULL)
		return (eItemMode)m_propertyItemMode->GetValueAsInteger();
	return eItemMode::eItemMode_Item;
}

eSelectMode CMetaAttributeObject::GetSelectMode() const
{
	if (GetClsidCount() > 1)
		return eSelectMode::eSelectMode_Items;
	IMetaTypeObjectValueSingle* so = GetMetadata()->GetTypeObject(GetFirstClsid());
	if (so != NULL) {
		IMetaObjectRecordDataFolderMutableRef* metaObject = dynamic_cast<IMetaObjectRecordDataFolderMutableRef*>(so->GetMetaObject());
		if (so->GetMetaType() == eMetaObjectType::enReference && metaObject != NULL)
			return (eSelectMode)m_propertySelectMode->GetValueAsInteger();
		return eSelectMode::eSelectMode_Items;
	}	
	return eSelectMode::eSelectMode_Items;
}

/////////////////////////////////////////////////////////////////////////

CValue IMetaAttributeObject::CreateValue() const
{
	CValue* refData = CreateValueRef();
	if (refData == NULL)
		return CValue();
	return refData;
}

CValue* IMetaAttributeObject::CreateValueRef() const
{
	if (m_defValue.IsEmpty())
		return ITypeAttribute::CreateValueRef();

	return new CValue(m_defValue);
}

//***********************************************************************
//*								Events								    *
//***********************************************************************

bool IMetaAttributeObject::OnCreateMetaObject(IMetadata* metaData)
{
	IMetaObject* metaObject = GetParent();
	wxASSERT(metaObject);

	if (IMetaObject::OnCreateMetaObject(metaData)) {
		return metaObject->OnReloadMetaObject();
	}

	return false;
}

bool IMetaAttributeObject::OnDeleteMetaObject()
{
	return IMetaObject::OnDeleteMetaObject();
}

#include "appData.h"
#include "3rdparty/databaseLayer/databaseLayer.h"

wxString IMetaAttributeObject::GetSQLTypeObject(const CLASS_ID& clsid) const
{
	switch (CValue::GetVTByID(clsid))
	{
	case eValueTypes::TYPE_BOOLEAN:
		return wxString::Format("SMALLINT");
	case eValueTypes::TYPE_NUMBER:
		if (GetScale() > 0)
			return wxString::Format("NUMERIC(%i,%i)", GetPrecision(), GetScale());
		else
			return wxString::Format("NUMERIC(%i)", GetPrecision());
	case eValueTypes::TYPE_DATE:
		if (GetDateFraction() == eDateFractions::eDateFractions_Date)
			return wxString::Format("DATE");
		else if (GetDateFraction() == eDateFractions::eDateFractions_DateTime)
			return wxString::Format("TIMESTAMP");
		else
			return wxString::Format("TIME");
	case eValueTypes::TYPE_STRING:
		if (GetAllowedLength() == eAllowedLength::eAllowedLength_Variable)
			return wxString::Format("VARCHAR(%i)", GetLength());
		else
			return wxString::Format("CHAR(%i)", GetLength());
	case eValueTypes::TYPE_ENUM:
		return wxString::Format("INTEGER");
	default:
		if (databaseLayer->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL)
			return wxString::Format("BYTEA"); 
		return wxString::Format("BLOB");
	}

	return wxEmptyString;
}

//***********************************************************************
//*                               Data				                    *
//***********************************************************************

bool CMetaAttributeObject::LoadData(CMemoryReader& reader)
{
	if (!ITypeWrapper::LoadTypeData(reader))
		return false;

	m_propertyFillCheck->SetValue(reader.r_u8());
	m_propertyItemMode->SetValue(reader.r_u16());
	m_propertySelectMode->SetValue(reader.r_u16());
	return true;
}

bool CMetaAttributeObject::SaveData(CMemoryWriter& writer)
{
	if (!ITypeWrapper::SaveTypeData(writer))
		return false;

	writer.w_u8(m_propertyFillCheck->GetValueAsBoolean());
	writer.w_u16(m_propertyItemMode->GetValueAsInteger());
	writer.w_u16(m_propertySelectMode->GetValueAsInteger());
	return true;
}

bool CMetaDefaultAttributeObject::LoadData(CMemoryReader& reader)
{
	if (!ITypeWrapper::LoadTypeData(reader))
		return false;

	m_fillCheck = reader.r_u8();
	return true;
}

bool CMetaDefaultAttributeObject::SaveData(CMemoryWriter& writer)
{
	if (!ITypeWrapper::SaveTypeData(writer))
		return false;

	writer.w_u8(m_fillCheck);
	return true;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_REGISTER(CMetaAttributeObject, "attribute", g_metaAttributeCLSID);
METADATA_REGISTER(CMetaDefaultAttributeObject, "defaultAttribute", g_metaDefaultAttributeCLSID);