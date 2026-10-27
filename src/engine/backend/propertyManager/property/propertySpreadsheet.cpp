#include "propertySpreadsheet.h"
#include "backend/propertyManager/property/variant/variantSpreadsheet.h"
#include "core/serialize/dataBuilder.h"   // ibDataValue — node value (Binary, transitional)


////////////////////////////////////////////////////////////////////////

ibVariant ibPropertySpreadsheet::CreateVariantData(const ibSpreadsheetDescription& val)
{
	return ibVariant(new ibVariantSpreadsheet(val));
}

////////////////////////////////////////////////////////////////////////

ibSpreadsheetDescription& ibPropertySpreadsheet::GetValueAsSpreadsheetDesc() const
{
	return get_cell_variant<ibVariantSpreadsheet>()->GetSpreadsheetDesc();
}

void ibPropertySpreadsheet::SetValue(const ibSpreadsheetDescription& val)
{
	m_propValue = CreateVariantData(val);
}

////////////////////////////////////////////////////////////////////////

bool ibPropertySpreadsheet::IsEmptyProperty() const
{
	return get_cell_variant<ibVariantSpreadsheet>()->IsEmptySpreadsheet();
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

#include "backend/metaCollection/metaObject.h"   // ibValueMetaObject — the owner, the link's object
#include "protocol/protocol.h"                   // ibProtocol::ibSchema::ibProperties::Object

bool ibPropertySpreadsheet::GetProtocolValue(ibDataNode& row, ibReader reader)
{
	if (!ibProperty::GetProtocolValue(row, reader))
		return false;
	row.SetValue(ibProtocol::ibSchema::ibProperties::Object, static_cast<std::int32_t>(static_cast<const ibValueMetaObject*>(m_owner)->GetMetaID()));
	return true;
}