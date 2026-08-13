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

// The hierarchy type decides what a Parent may point at, and the Parent field is where everyone
// asks. Restate it the moment the declaration changes — waiting for the next configuration run
// would leave the picker offering folders on a chart that no longer has any.
void ibValueMetaObjectRecordDataHierarchyMutableRef::OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue)
{
	ibValueMetaObjectRecordDataMutableRef::OnPropertyChanged(property, oldValue, newValue);

	if (property == m_propertyHierarchyType)
		ApplyHierarchyType();
}
