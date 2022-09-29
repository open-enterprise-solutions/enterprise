#include "tableBox.h"

void CValueTableBoxColumn::OnPropertyCreated(Property* property)
{
	if (m_propertySource == property) {
		CValueTableBoxColumn::SaveToVariant(m_propertySource->GetValue(), GetMetaData());
	}
}

bool CValueTableBoxColumn::OnPropertyChanging(Property* property, const wxVariant& newValue)
{
	if (m_propertySource == property && !CValueTableBoxColumn::LoadFromVariant(newValue))
		return false;
	return IValueControl::OnPropertyChanging(property, newValue);
}