#include "catalog.h"

void CMetaObjectCatalog::OnPropertyChanged(Property* property)
{
	if (m_attributeOwner->GetTypeCount() > 0) {
		m_attributeOwner->ClearFlag(metaDisableObjectFlag);
	}
	else {
		m_attributeOwner->SetFlag(metaDisableObjectFlag);
	}

	IMetaObjectRecordDataMutableRef::OnPropertyChanged(property);
}