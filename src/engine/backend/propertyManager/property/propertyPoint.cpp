#include "propertyPoint.h"
#include "backend/propertyManager/property/variant/variantPoint.h"
#include "core/serialize/dataBuilder.h"
#include "backend/system/value/valuePoint.h"


ibPropertyValue ibPropertyPoint::CreateVariantData(const ibPoint& val)
{
	return ibPropertyValue(new ibPropertyValuePoint(val));
}

ibPoint ibPropertyPoint::GetValueAsPoint() const
{
	return get_cell_variant<ibPropertyValuePoint>()->GetPoint();
}

//base property for "point"
bool ibPropertyPoint::SetDataValue(const ibValue& varPropVal)
{
	ibValuePoint* valuePoint = varPropVal.ConvertToType<ibValuePoint>();
	if (valuePoint == nullptr)
		return false;
	SetValue(valuePoint->m_point);
	return true;
}

bool ibPropertyPoint::GetDataValue(ibValue& pvarPropVal) const
{
	pvarPropVal = ibValue::CreateObjectValue<ibValuePoint>(GetValueAsPoint());
	return true;
}

bool ibPropertyPoint::ReadNodeValue(const ibDataValue& value)
{
	ibPropertyPoint::SetValue(value.AsString());
	return true;
}

bool ibPropertyPoint::WriteNodeValue(ibDataValue& value) const
{
	value = ibDataValue::String(ibPropertyPoint::GetValueAsString());
	return true;
}