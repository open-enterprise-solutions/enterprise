////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : meta-attribues
////////////////////////////////////////////////////////////////////////////

#include "metaAttributeObject.h"
#include "metadata/metadata.h"

////////////////////////////////////////////////////////////////////////////

wxIMPLEMENT_ABSTRACT_CLASS(IMetaAttributeObject, IMetaObject);

wxIMPLEMENT_DYNAMIC_CLASS(CMetaAttributeObject, IMetaAttributeObject);
wxIMPLEMENT_DYNAMIC_CLASS(CMetaDefaultAttributeObject, IMetaAttributeObject);

//***********************************************************************
//*                         Attributes                                  * 
//***********************************************************************

#include "metadata/singleMetaTypes.h"

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

/////////////////////////////////////////////////////////////////////////

CValue IMetaAttributeObject::CreateValue() const
{
	CValue* refData = CreateValueRef();
	return refData ? 
		refData : CValue();
}

CValue* IMetaAttributeObject::CreateValueRef() const
{
	if (m_defValue.IsEmpty()) {
		return IAttributeInfo::CreateValueRef();
	}

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
		if (GetDateTime() == eDateFractions::eDateFractions_Date)
			return wxString::Format("DATE");
		else if (GetDateTime() == eDateFractions::eDateFractions_DateTime)
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
		return wxString::Format("BLOB");
	}

	return wxEmptyString;
}

//***********************************************************************
//*                               Data				                    *
//***********************************************************************

bool CMetaAttributeObject::LoadData(CMemoryReader& reader)
{
	if (!IAttributeWrapper::LoadTypeData(reader))
		return false;

	m_propertyFillCheck->SetValue(reader.r_u8());
	return true;
}

bool CMetaAttributeObject::SaveData(CMemoryWriter& writer)
{
	if (!IAttributeWrapper::SaveTypeData(writer))
		return false;

	writer.w_u8(m_propertyFillCheck->GetValueAsBoolean());
	return true;
}

bool CMetaDefaultAttributeObject::LoadData(CMemoryReader& reader)
{
	if (!IAttributeWrapper::LoadTypeData(reader))
		return false;

	m_fillCheck = reader.r_u8();
	return true;
}

bool CMetaDefaultAttributeObject::SaveData(CMemoryWriter& writer)
{
	if (!IAttributeWrapper::SaveTypeData(writer))
		return false;

	writer.w_u8(m_fillCheck);
	return true;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_REGISTER(CMetaAttributeObject, "attribute", g_metaAttributeCLSID);
METADATA_REGISTER(CMetaDefaultAttributeObject, "defaultAttribute", g_metaDefaultAttributeCLSID);