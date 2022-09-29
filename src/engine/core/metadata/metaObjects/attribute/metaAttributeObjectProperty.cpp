////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : meta-attribues
////////////////////////////////////////////////////////////////////////////

#include "metaAttributeObject.h"

void CMetaAttributeObject::OnPropertyCreated(Property* property)
{
	if (m_propertyType == property) {
		CMetaAttributeObject::SaveToVariant(m_propertyType->GetValue(), m_metaData);
	}
}

bool CMetaAttributeObject::OnPropertyChanging(Property* property, const wxVariant& newValue)
{
	if (m_propertyType == property && !CMetaAttributeObject::LoadFromVariant(newValue))
		return false;
	return IMetaAttributeObject::OnPropertyChanging(property, newValue);
}

void CMetaAttributeObject::OnPropertyChanged(Property* property)
{
	IMetaObject* metaObject = GetParent();
	wxASSERT(metaObject);

	if (metaObject->OnReloadMetaObject()) {
		IMetaObject::OnPropertyChanged(property);
	}
}