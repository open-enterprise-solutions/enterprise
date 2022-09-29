#include "object.h"

void IMetaObjectRecordDataMutableRef::OnPropertyCreated(Property* property)
{
	if (m_propertyGeneration == property) {
		m_genData.SaveToVariant(m_propertyGeneration->GetValue(), m_metaData);
	}
	IMetaObjectRecordDataRef::OnPropertyCreated(property);
}

bool IMetaObjectRecordDataMutableRef::OnPropertyChanging(Property* property, const wxVariant& newValue)
{
	if (m_propertyGeneration == property && !m_genData.LoadFromVariant(newValue))
		return true;
	return IMetaObjectRecordDataRef::OnPropertyChanging(property, newValue);
}

void IMetaObjectRecordDataMutableRef::OnPropertyChanged(Property* property)
{
	IMetaObjectRecordDataRef::OnPropertyChanged(property);
}