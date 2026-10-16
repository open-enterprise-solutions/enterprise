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
bool ibValueMetaObjectRecordDataHierarchyMutableRef::OnPropertyChanging(ibProperty* property, const wxVariant& newValue)
{
	if (!ibValueMetaObjectRecordDataMutableRef::OnPropertyChanging(property, newValue))
		return false;

	// 1..50, the same bound a catalog's code has always lived inside once it could be chosen. Zero
	// would mean "no length", and the column would have nothing to be.
	if (property == m_propertyCodeLength) {
		const long length = newValue.GetLong();
		return length >= 1 && length <= 50;
	}
	return true;
}

void ibValueMetaObjectRecordDataHierarchyMutableRef::OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue)
{
	ibValueMetaObjectRecordDataMutableRef::OnPropertyChanged(property, oldValue, newValue);

	if (property == m_propertyHierarchyType)
		ApplyHierarchyType();

	if (property == m_propertyCodeLength || property == m_propertyCodeType)
		ApplyCodeShape();
}
