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

bool ibValueMetaObjectRecordDataMutableRef::OnPropertyChanging(ibProperty* property, const ibVariant& newValue)
{
	return ibValueMetaObjectRecordDataRef::OnPropertyChanging(property, newValue);
}

void ibValueMetaObjectRecordDataMutableRef::OnPropertyChanged(ibProperty* property, const ibVariant& oldValue, const ibVariant& newValue)
{
	ibValueMetaObjectRecordDataRef::OnPropertyChanged(property, oldValue, newValue);
}

// A length the column cannot be declared with is refused here, where it is typed, rather than at the
// apply. The ceiling of a code depends on its type, so a type switched to Number asks the length that
// stands, and a length asks the type that stands.
bool ibValueMetaObjectRecordDataHierarchyMutableRef::OnPropertyChanging(ibProperty* property, const ibVariant& newValue)
{
	if (property == m_propertyCodeLength) {
		if (!CheckCodeShape(GetCodeType(), newValue.GetNumber().ToUInt(), GetDescriptionLength(), nullptr))
			return false;
	}
	else if (property == m_propertyCodeType) {
		if (!CheckCodeShape(static_cast<ibCodeType>(newValue.GetNumber().ToInt()), GetCodeLength(), GetDescriptionLength(), nullptr))
			return false;
	}
	else if (property == m_propertyDescriptionLength) {
		if (!CheckCodeShape(GetCodeType(), GetCodeLength(), newValue.GetNumber().ToUInt(), nullptr))
			return false;
	}

	return ibValueMetaObjectRecordDataMutableRef::OnPropertyChanging(property, newValue);
}

// The hierarchy type decides what a Parent may point at, and the Parent field is where everyone
// asks. Restate it the moment the declaration changes — waiting for the next configuration run
// would leave the picker offering folders on a chart that no longer has any. The code's shape, for
// the same reason: the column and the generator read it off the attribute.
void ibValueMetaObjectRecordDataHierarchyMutableRef::OnPropertyChanged(ibProperty* property, const ibVariant& oldValue, const ibVariant& newValue)
{
	ibValueMetaObjectRecordDataMutableRef::OnPropertyChanged(property, oldValue, newValue);

	if (property == m_propertyHierarchyType)
		ApplyHierarchyType();
	else if (property == m_propertyCodeType || property == m_propertyCodeLength || property == m_propertyDescriptionLength)
		ApplyCodeShape();
}
