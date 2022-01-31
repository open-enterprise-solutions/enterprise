////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : meta-attribues
////////////////////////////////////////////////////////////////////////////

#include "metaAttributeObject.h"
#include "metadata/metadata.h"

wxIMPLEMENT_ABSTRACT_CLASS(IMetaAttributeObject, IMetaObject)
wxIMPLEMENT_DYNAMIC_CLASS(CMetaAttributeObject, IMetaAttributeObject)
wxIMPLEMENT_DYNAMIC_CLASS(CMetaDefaultAttributeObject, IMetaAttributeObject)

//***********************************************************************
//*                         Attributes                                  * 
//***********************************************************************

bool IMetaAttributeObject::ProcessChoice(IValueFrame *ownerValue, meta_identifier_t id)
{
	IMetaObject* metaObject
		= m_metaData->GetMetaObject(GetTypeObject());
	if (metaObject) {
		return metaObject->ProcessChoice(ownerValue, id);
	}
	return false;
}

CMetaAttributeObject::CMetaAttributeObject() : IMetaAttributeObject()
{
	//types of category 
	PropertyContainer *categoryType = IObjectBase::CreatePropertyContainer("Type");
	categoryType->AddProperty("type", PropertyType::PT_TYPE_SELECT);
	categoryType->AddProperty("precision", PropertyType::PT_UINT);
	categoryType->AddProperty("scale", PropertyType::PT_UINT);
	categoryType->AddProperty("date_time", PropertyType::PT_OPTION, &CMetaAttributeObject::GetDateTimeFormat);
	categoryType->AddProperty("length", PropertyType::PT_UINT);
	m_category->AddCategory(categoryType);

	PropertyContainer *categoryAttribute = IObjectBase::CreatePropertyContainer("Attribute");
	categoryAttribute->AddProperty("fill_check", PropertyType::PT_BOOL);
	m_category->AddCategory(categoryAttribute);
}

//***********************************************************************
//*								Events								    *
//***********************************************************************

bool IMetaAttributeObject::OnCreateMetaObject(IMetadata *metaData)
{
	IMetaObject *metaObject = GetParent();
	wxASSERT(metaObject);

	if (IMetaObject::OnCreateMetaObject(metaData)) {
		if (metaObject->OnReloadMetaObject()) {
			return true;
		}
	}

	return false;
}

bool IMetaAttributeObject::OnDeleteMetaObject()
{
	return IMetaObject::OnDeleteMetaObject();
}

wxString IMetaAttributeObject::GetSQLTypeObject()
{
	switch (IMetaAttributeObject::GetTypeObject())
	{
	case eValueTypes::TYPE_BOOLEAN: return wxString::Format("SMALLINT"); break;
	case eValueTypes::TYPE_NUMBER:
	{
		if (m_typeDescription.GetScale() > 0) {
			return wxString::Format("NUMERIC(%i,%i)", m_typeDescription.GetPrecision(), m_typeDescription.GetScale());
		}
		else {
			return wxString::Format("NUMERIC(%i)", m_typeDescription.GetPrecision());
		}
		break;
	}
	case eValueTypes::TYPE_DATE: return wxString::Format("TIMESTAMP"); break;
	case eValueTypes::TYPE_STRING: return wxString::Format("VARCHAR(%i)", m_typeDescription.GetLength()); break;
	default: return wxString::Format("BLOB"); break;
	}

	return wxEmptyString;
}

//***********************************************************************
//*                               Data				                    *
//***********************************************************************

bool IMetaAttributeObject::LoadData(CMemoryReader &reader)
{
	m_bFillCheck = reader.r_u8();
	reader.r(&m_typeDescription, sizeof(typeDescription_t));
	return true;
}

bool IMetaAttributeObject::SaveData(CMemoryWriter &writer)
{
	writer.w_u8(m_bFillCheck);
	writer.w(&m_typeDescription, sizeof(typeDescription_t));
	return true;
}

//***********************************************************************
//*                          Read&save property                         *
//***********************************************************************

void CMetaAttributeObject::ReadProperty()
{
	IMetaObject::ReadProperty();
	IObjectBase::SetPropertyValue("type", m_typeDescription.GetTypeObject());

	switch (m_typeDescription.GetTypeObject())
	{
	case eValueTypes::TYPE_NUMBER:
		IObjectBase::SetPropertyValue("precision", m_typeDescription.GetPrecision(), true);
		IObjectBase::SetPropertyValue("scale", m_typeDescription.GetScale(), true);
		break;
	case eValueTypes::TYPE_DATE:
		IObjectBase::SetPropertyValue("date_time", m_typeDescription.GetDateTime(), true);
		break;
	case eValueTypes::TYPE_STRING:
		IObjectBase::SetPropertyValue("length", m_typeDescription.GetLength(), true);
		break;
	}

	IObjectBase::SetPropertyValue("fill_check", m_bFillCheck);
}

void CMetaAttributeObject::SaveProperty()
{
	IMetaObject::SaveProperty();

	meta_identifier_t metaType = 0;
	if (IObjectBase::GetPropertyValue("type", metaType)) {

		if (metaType != m_typeDescription.GetTypeObject()) {
			m_typeDescription.SetDefaultMetatype(metaType);
			switch (m_typeDescription.GetTypeObject())
			{
			case eValueTypes::TYPE_NUMBER:
				IObjectBase::SetPropertyValue("precision", m_typeDescription.GetPrecision(), true);
				IObjectBase::SetPropertyValue("scale", m_typeDescription.GetScale(), true);
				break;
			case eValueTypes::TYPE_DATE:
				IObjectBase::SetPropertyValue("date_time", m_typeDescription.GetDateTime(), true);
				break;
			case eValueTypes::TYPE_STRING:
				IObjectBase::SetPropertyValue("length", m_typeDescription.GetLength(), true);
				break;
			}
		}

		switch (m_typeDescription.GetTypeObject())
		{
		case eValueTypes::TYPE_NUMBER:
		{
			unsigned char precision = 10, scale = 0;
			IObjectBase::GetPropertyValue("precision", precision, true);
			IObjectBase::GetPropertyValue("scale", scale, true);
			m_typeDescription.SetNumber(precision, scale);
			break;
		}
		case eValueTypes::TYPE_DATE:
		{
			eDateFractions dateTime = eDateFractions::eDateTime;
			IObjectBase::GetPropertyValue("date_time", dateTime, true);
			m_typeDescription.SetDate(dateTime);
			break;
		}
		case eValueTypes::TYPE_STRING:
		{
			unsigned short length = 0;
			IObjectBase::GetPropertyValue("length", length, true);
			m_typeDescription.SetString(length);
			break;
		}
		}
	}

	IObjectBase::GetPropertyValue("fill_check", m_bFillCheck);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_REGISTER(CMetaAttributeObject, "metaAttribute", TEXT2CLSID("MD_ATTR"));
METADATA_REGISTER(CMetaDefaultAttributeObject, "metaDefaultAttribute", TEXT2CLSID("MD_DATT"));
