#include "propertyPoint.h"
#include "backend/propertyManager/property/variant/variantPoint.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/system/value/valuePoint.h"

wxObject* (*ibPropertyPoint::ms_propertyPoint)(const wxString&,const wxString&,const wxPoint&) = nullptr;

wxVariantData* ibPropertyPoint::CreateVariantData(const wxPoint& val)
{
	return new ibVariantDataPoint(val);
}

wxPoint ibPropertyPoint::GetValueAsPoint() const
{
	return get_cell_variant<ibVariantDataPoint>()->GetPoint();
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