#include "metaModuleObject.h"

bool ibValueMetaObjectCommonModule::OnPropertyChanging(ibProperty* property, const ibVariant& newValue)
{
	// Privileged switched on switches global off (OnPropertyChanged), and the module is compiled by global:
	// it closes for that as for global itself.
	if (m_propertyGlobalModule == property ||
		(m_propertyPrivilegedModule == property && newValue.GetBoolean() && IsGlobalModule())) {
		return ibValueMetaObjectCommonModule::OnBeforeCloseMetaObject() &&
			ibValueMetaObjectCommonModule::OnAfterCloseMetaObject();
	}

	return ibValueMetaObjectModuleBase::OnPropertyChanging(property, newValue);
}

void ibValueMetaObjectCommonModule::OnPropertyChanged(ibProperty* property, const ibVariant& oldValue, const ibVariant& newValue)
{
	// Global or privileged, never both: the one switched on switches the other off.
	bool globalChanged = m_propertyGlobalModule == property;
	if (globalChanged && IsGlobalModule())
		m_propertyPrivilegedModule->SetValue(false);
	if (m_propertyPrivilegedModule == property && newValue.GetBoolean() && IsGlobalModule()) {
		m_propertyGlobalModule->SetValue(false);
		globalChanged = true;
	}

	if (globalChanged) {
		ibValueMetaObjectCommonModule::OnBeforeRunMetaObject(newObjectFlag);
		ibValueMetaObjectCommonModule::OnAfterRunMetaObject(newObjectFlag);
	}

	ibValueMetaObjectModuleBase::OnPropertyChanged(property, oldValue, newValue);
}
