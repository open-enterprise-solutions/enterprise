////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : type unit
////////////////////////////////////////////////////////////////////////////

#include "valueType.h"

//////////////////////////////////////////////////////////////////////

wxIMPLEMENT_DYNAMIC_CLASS(CValueType, CValue);

//////////////////////////////////////////////////////////////////////

CValueType::CValueType() : CValue(eValueTypes::TYPE_VALUE, true), m_classType(GetTypeString())
{
	m_type_id = 9999999;
}

CValueType::CValueType(CLASS_ID type_id) : CValue(eValueTypes::TYPE_VALUE, true), m_classType(CValue::GetNameObjectFromID(type_id))
{
	m_type_id = type_id;
}

#include "metadata/metadata.h"

CValueType::CValueType(const wxString &typeName) : CValue(eValueTypes::TYPE_VALUE, true), m_classType(typeName)
{
	m_type_id = metadata->GetIDObjectFromString(typeName);
}

CValueType::CValueType(const CValue &cObject) : CValue(eValueTypes::TYPE_VALUE, true), m_classType(cObject.GetTypeString())
{
	m_type_id = cObject.GetTypeID();
}

CValueType::CValueType(const CValueType &cType) : CValue(eValueTypes::TYPE_VALUE, true), m_classType(cType.m_classType)
{
	m_type_id = cType.m_type_id;
}
