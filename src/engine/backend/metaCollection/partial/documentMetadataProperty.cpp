////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : meta-attribues
////////////////////////////////////////////////////////////////////////////

#include "document.h"

void CMetaObjectDocument::OnPropertyCreated(Property* property)
{
	if (m_propertyRecordData == property) {
		CMetaObjectDocument::SaveToVariant(m_propertyRecordData->GetValue(), m_metaData);
	}
	IMetaObjectRecordDataMutableRef::OnPropertyCreated(property);
}

bool CMetaObjectDocument::OnPropertyChanging(Property* property, const wxVariant& newValue)
{
	if (m_propertyRecordData == property && !CMetaObjectDocument::LoadFromVariant(newValue))
		return true;

	return IMetaObjectRecordDataMutableRef::OnPropertyChanging(property, newValue);
}

void CMetaObjectDocument::OnPropertyChanged(Property* property, const wxVariant& oldValue, const wxVariant& newValue)
{
	if (CMetaObjectDocument::OnReloadMetaObject()) {
		IMetaObject::OnPropertyChanged(property, oldValue, newValue);
	}
}