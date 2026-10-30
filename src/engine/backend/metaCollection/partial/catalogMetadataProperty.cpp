#include "catalog.h"
#include "backend/metaData.h"
#include "backend/objCtor.h"

void ibValueMetaObjectCatalog::OnPropertyCreated(ibProperty* property)
{
	ibValueMetaObjectRecordDataMutableRef::OnPropertyCreated(property);
}

bool ibValueMetaObjectCatalog::OnPropertyChanging(ibProperty* property, const ibVariant& newValue)
{
	return ibValueMetaObjectRecordDataMutableRef::OnPropertyChanging(property, newValue);
}

void ibValueMetaObjectCatalog::OnPropertyChanged(ibProperty* property, const ibVariant& oldValue, const ibVariant& newValue)
{
	if (m_propertyOwner == property) {
		const ibMetaDescription& metaDesc = m_propertyOwner->GetValueAsMetaDesc(); ibTypeDescription typeDesc;
		for (unsigned int idx = 0; idx < metaDesc.GetTypeCount(); idx++) {
			const ibValueMetaObject* catalog = m_metaData->FindAnyObjectByFilter(metaDesc.GetByIdx(idx));
			if (catalog != nullptr) {
				const ibCtorMetaValueType* so = m_metaData->GetTypeCtor(catalog, ibCtorObjectMetaType::ibCtorObjectMetaType_Reference);
				wxASSERT(so);
				typeDesc.AppendMetaType(so->GetClassType());
			}
		}
		(*m_propertyAttributeOwner)->SetDefaultMetaType(typeDesc);
	}

	if ((*m_propertyAttributeOwner)->GetClsidCount() > 0) (*m_propertyAttributeOwner)->ClearFlag(metaDisableFlag);
	else (*m_propertyAttributeOwner)->SetFlag(metaDisableFlag);

	// Through the hierarchy, not past it — it restates Parent and IsFolder for a new HierarchyType.
	ibValueMetaObjectRecordDataHierarchyMutableRef::OnPropertyChanged(property, oldValue, newValue);
}