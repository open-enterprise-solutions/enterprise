#include "propertySpreadsheet.h"
#include "backend/propertyManager/property/variant/variantSpreadsheet.h"
#include "backend/serialize/dataBuilder.h"   // ibDataValue — node value (Binary, transitional)


////////////////////////////////////////////////////////////////////////

wxVariantData* ibPropertySpreadsheet::CreateVariantData(const ibSpreadsheetDescription& val)
{
	return new ibVariantDataSpreadsheet(val);
}

////////////////////////////////////////////////////////////////////////

ibSpreadsheetDescription& ibPropertySpreadsheet::GetValueAsSpreadsheetDesc() const
{
	return get_cell_variant<ibVariantDataSpreadsheet>()->GetSpreadsheetDesc();
}

void ibPropertySpreadsheet::SetValue(const ibSpreadsheetDescription& val)
{
	m_propValue = CreateVariantData(val);
}

////////////////////////////////////////////////////////////////////////

bool ibPropertySpreadsheet::IsEmptyProperty() const
{
	return get_cell_variant<ibVariantDataSpreadsheet>()->IsEmptySpreadsheet();
}

////////////////////////////////////////////////////////////////////////

#include "backend/system/value/valueSpreadsheet.h"

//base property for "template"
bool ibPropertySpreadsheet::SetDataValue(const ibValue& varPropVal)
{
	ibValueSpreadsheetDocument* valueType = varPropVal.ConvertToType<ibValueSpreadsheetDocument>();
	if (valueType != nullptr) {
		SetValue(valueType->GetSpreadsheetDesc());
		return true;
	}
	return false;
}

bool ibPropertySpreadsheet::GetDataValue(ibValue& pvarPropVal) const
{
	pvarPropVal = ibValue::CreateObjectValue<ibValueSpreadsheetDocument>(GetValueAsSpreadsheetDesc());
	return true;
}

bool ibPropertySpreadsheet::ReadNodeValue(const ibDataValue& value)
{
	return ibSpreadsheetDescriptionMemory::ReadNode(value, GetValueAsSpreadsheetDesc());
}

bool ibPropertySpreadsheet::WriteNodeValue(ibDataValue& value) const
{
	return ibSpreadsheetDescriptionMemory::WriteNode(value, GetValueAsSpreadsheetDesc());
}