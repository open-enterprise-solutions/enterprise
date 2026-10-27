#include "chartOfCharacteristicTypes.h"
#include "backend/metaData.h"
#include "backend/objCtor.h"

void ibValueMetaObjectChartOfCharacteristicTypes::OnPropertyCreated(ibProperty* property)
{
	ibValueMetaObjectRecordDataMutableRef::OnPropertyCreated(property);
}

bool ibValueMetaObjectChartOfCharacteristicTypes::OnPropertyChanging(ibProperty* property, const ibPropertyValue& newValue)
{
	return ibValueMetaObjectRecordDataMutableRef::OnPropertyChanging(property, newValue);
}

void ibValueMetaObjectChartOfCharacteristicTypes::OnPropertyChanged(ibProperty* property, const ibPropertyValue& oldValue, const ibPropertyValue& newValue)
{
	ibValueMetaObjectRecordDataMutableRef::OnPropertyChanged(property, oldValue, newValue);
}
