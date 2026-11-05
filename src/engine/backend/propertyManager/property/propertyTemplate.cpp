#include "propertyTemplate.h"
#include "backend/propertyManager/property/variant/variantTemplate.h"
#include "core/serialize/dataBuilder.h"    // ibDataValue — the node value
#include "backend/backend_composition.h"   // ibBackendTemplateDescriptionMemory — a composition read against the configuration

////////////////////////////////////////////////////////////////////////

ibVariant ibPropertyTemplate::CreateVariantData(const ibTemplateDescription& val)
{
	return ibVariant(new ibVariantTemplate(val));
}

////////////////////////////////////////////////////////////////////////

ibTemplateDescription& ibPropertyTemplate::GetValueAsTemplateDesc() const
{
	return get_cell_variant<ibVariantTemplate>()->GetTemplateDesc();
}

// A NEW CELL — whoever holds the old one goes on holding it, the variant being reference-counted.
void ibPropertyTemplate::SetValue(const ibTemplateDescription& val)
{
	m_propValue = CreateVariantData(val);
}

////////////////////////////////////////////////////////////////////////

bool ibPropertyTemplate::IsEmptyProperty() const
{
	return GetValueAsTemplateDesc().GetType() == ibTemplateType::None;
}

ibPropertyChoiceMode ibPropertyTemplate::GetValueList(ibPropertyChoiceList& list)
{
	if (!IsEmptyProperty())
		return ibPropertyChoiceMode::None;

	const std::pair<ibTemplateType, const ibChar*> types[] = {
		{ ibTemplateType::Spreadsheet, ibT("Spreadsheet") },
		{ ibTemplateType::Composer,    ibT("Composer") },
	};
	for (const auto& [type, name] : types) {
		const ibVariant value = CreateVariantData(ibTemplateDescription(type));
		list.Add(static_cast<long>(type), name, value.GetString(), value);
	}
	return ibPropertyChoiceMode::Single;
}

////////////////////////////////////////////////////////////////////////

#include "backend/system/value/valueSpreadsheet.h"
#include "backend/metaCollection/metaTemplateObject.h"   // ibTemplateProvider — the value a script is handed, of its kind

bool ibPropertyTemplate::SetDataValue(const ibValue& varPropVal)
{
	// A sheet is replaced; a composition is CONFIGURED, not replaced (see ibValueDataComposition).
	ibValueSpreadsheetDocument* valueType = varPropVal.ConvertToType<ibValueSpreadsheetDocument>();
	if (valueType != nullptr && GetValueAsTemplateDesc().GetType() == ibTemplateType::Spreadsheet) {
		SetValue(valueType->GetSpreadsheetDesc());
		return true;
	}
	return false;
}

// ⭐ THE OWNER'S GetTemplate(name) ROAD — the provider of the kind makes it, over the owner's configuration: through a
// CONST owner, the non-const GetMetaData asserts and answers null (propertyObject.h). No kind chosen — nothing.
bool ibPropertyTemplate::GetDataValue(ibValue& pvarPropVal) const
{
	if (IsEmptyProperty())
		return false;
	ibTemplateDescription& templateDesc = GetValueAsTemplateDesc();
	const ibPropertyObject* owner = m_owner;
	pvarPropVal = ibTemplateProvider::Get(templateDesc.GetType()).GetDataValue(templateDesc,
		owner != nullptr ? owner->GetMetaData() : nullptr);
	return true;
}

////////////////////////////////////////////////////////////////////////

// ⚠ THROUGH A CONST OWNER, as above: what a composition reads back holds references and enum members, and those
// types exist only in the configuration's own registry.
bool ibPropertyTemplate::ReadNodeValue(const ibDataValue& value)
{
	const ibPropertyObject* owner = m_owner;
	return ibBackendTemplateDescriptionMemory::ReadNode(value, GetValueAsTemplateDesc(),
		owner != nullptr ? owner->GetMetaData() : nullptr);
}

bool ibPropertyTemplate::WriteNodeValue(ibDataValue& value) const
{
	return ibTemplateDescriptionMemory::WriteNode(value, GetValueAsTemplateDesc());
}

#include "backend/metaCollection/metaObject.h"   // ibValueMetaObject — the owner, the link's object
#include "protocol/protocolDesigner.h"                   // ibProtocol::ibSchema::ibProperties::Object

bool ibPropertyTemplate::GetProtocolValue(ibDataNode& row, ibReader reader)
{
	if (!ibProperty::GetProtocolValue(row, reader))
		return false;
	row.SetValue(ibProtocol::ibSchema::ibProperties::Object, static_cast<std::int32_t>(static_cast<const ibValueMetaObject*>(m_owner)->GetMetaID()));
	return true;
}
