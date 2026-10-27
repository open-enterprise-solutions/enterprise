#include "metaModuleObject.h"

bool ibValueMetaObjectCommonModule::OnPropertyChanging(ibProperty* property, const ibPropertyValue& newValue)
{
	if (m_propertyGlobalModule == property) {
		return ibValueMetaObjectCommonModule::OnBeforeCloseMetaObject() &&
			ibValueMetaObjectCommonModule::OnAfterCloseMetaObject();
	}

	return ibValueMetaObjectModuleBase::OnPropertyChanging(property, newValue);
}

void ibValueMetaObjectCommonModule::OnPropertyChanged(ibProperty* property, const ibPropertyValue& oldValue, const ibPropertyValue& newValue)
{
	if (m_propertyGlobalModule == property) {
		ibValueMetaObjectCommonModule::OnBeforeRunMetaObject(newObjectFlag);
		ibValueMetaObjectCommonModule::OnAfterRunMetaObject(newObjectFlag);
	}

	ibValueMetaObjectModuleBase::OnPropertyChanged(property, oldValue, newValue);
}