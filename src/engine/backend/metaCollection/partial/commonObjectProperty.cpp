#include "commonObject.h"

void ibValueMetaObjectRecordDataMutableRef::OnPropertyCreated(ibProperty* property)
{
	ibValueMetaObjectRecordDataRef::OnPropertyCreated(property);
}

void ibValueMetaObjectRecordDataMutableRef::OnPropertyRefresh()
{
	ibValueMetaObjectRecordDataRef::OnPropertyRefresh();
	HideProperty(m_propertyQuickChoice, true);
}

bool ibValueMetaObjectRecordDataMutableRef::OnPropertyChanging(ibProperty* property, const wxVariant& newValue)
{
	return ibValueMetaObjectRecordDataRef::OnPropertyChanging(property, newValue);
}

void ibValueMetaObjectRecordDataMutableRef::OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue)
{
	ibValueMetaObjectRecordDataRef::OnPropertyChanged(property, oldValue, newValue);
}