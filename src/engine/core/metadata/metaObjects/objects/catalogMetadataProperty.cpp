#include "catalog.h"

void CMetaObjectCatalog::OnPropertyCreated(Property* property)
{
	if (m_propertyOwners == property) {
		CMetaObjectCatalog::SaveToVariant(m_propertyOwners->GetValue(), m_metaData);
	}
	IMetaObjectRecordDataMutableRef::OnPropertyCreated(property);
}

bool CMetaObjectCatalog::OnPropertyChanging(Property* property, const wxVariant& newValue)
{
	if (m_propertyOwners == property && !CMetaObjectCatalog::LoadFromVariant(newValue))
		return true;
	return IMetaObjectRecordDataMutableRef::OnPropertyChanging(property, newValue);
}

void CMetaObjectCatalog::OnPropertyChanged(Property* property)
{
	if (m_attributeOwner->GetClsidCount() > 0) {
		m_attributeOwner->ClearFlag(metaDisableObjectFlag);
	}
	else {
		m_attributeOwner->SetFlag(metaDisableObjectFlag);
	}

	IMetaObjectRecordDataMutableRef::OnPropertyChanged(property);
}