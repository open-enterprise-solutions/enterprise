#include "chartOfCalculationTypes.h"
#include "backend/metaData.h"
#include "backend/objCtor.h"

void ibValueMetaObjectChartOfCalculationTypes::OnPropertyCreated(ibProperty* property)
{
	ibValueMetaObjectRecordDataMutableRef::OnPropertyCreated(property);
}

bool ibValueMetaObjectChartOfCalculationTypes::OnPropertyChanging(ibProperty* property, const ibVariant& newValue)
{
	return ibValueMetaObjectRecordDataMutableRef::OnPropertyChanging(property, newValue);
}

void ibValueMetaObjectChartOfCalculationTypes::OnPropertyChanged(ibProperty* property, const ibVariant& oldValue, const ibVariant& newValue)
{
	// The list edited in the designer types the sections now, not at the next run — see TypeBaseAndLeading.
	if (property == m_propertyBaseCharts)
		TypeBaseAndLeading();

	ibValueMetaObjectRecordDataMutableRef::OnPropertyChanged(property, oldValue, newValue);
}
