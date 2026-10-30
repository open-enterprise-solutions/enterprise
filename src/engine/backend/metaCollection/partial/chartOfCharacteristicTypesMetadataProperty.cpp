#include "chartOfCharacteristicTypes.h"
#include "backend/metaData.h"
#include "backend/objCtor.h"

void ibValueMetaObjectChartOfCharacteristicTypes::OnPropertyCreated(ibProperty* property)
{
	ibValueMetaObjectRecordDataMutableRef::OnPropertyCreated(property);
}

bool ibValueMetaObjectChartOfCharacteristicTypes::OnPropertyChanging(ibProperty* property, const ibVariant& newValue)
{
	return ibValueMetaObjectRecordDataMutableRef::OnPropertyChanging(property, newValue);
}

void ibValueMetaObjectChartOfCharacteristicTypes::OnPropertyChanged(ibProperty* property, const ibVariant& oldValue, const ibVariant& newValue)
{
	// Through the hierarchy, not past it — it restates Parent and IsFolder for a new HierarchyType.
	ibValueMetaObjectRecordDataHierarchyMutableRef::OnPropertyChanged(property, oldValue, newValue);
}
