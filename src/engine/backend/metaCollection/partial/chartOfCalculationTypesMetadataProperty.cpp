#include "chartOfCalculationTypes.h"
#include "backend/metaData.h"
#include "backend/objCtor.h"

void ibValueMetaObjectChartOfCalculationTypes::OnPropertyCreated(ibProperty* property)
{
	ibValueMetaObjectRecordDataMutableRef::OnPropertyCreated(property);
}

bool ibValueMetaObjectChartOfCalculationTypes::OnPropertyChanging(ibProperty* property, const ibPropertyValue& newValue)
{
	return ibValueMetaObjectRecordDataMutableRef::OnPropertyChanging(property, newValue);
}

void ibValueMetaObjectChartOfCalculationTypes::OnPropertyChanged(ibProperty* property, const ibPropertyValue& oldValue, const ibPropertyValue& newValue)
{
	// The list edited in the designer types the sections now, not at the next run — see TypeBaseAndLeading.
	if (property == m_propertyBaseCharts)
		TypeBaseAndLeading();

	ibValueMetaObjectRecordDataMutableRef::OnPropertyChanged(property, oldValue, newValue);
}
