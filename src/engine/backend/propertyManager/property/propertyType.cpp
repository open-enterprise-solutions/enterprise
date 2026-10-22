#include "propertyType.h"
#include "backend/propertyManager/property/variant/variantType.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode / ibDataValue (Child + Array)
#include "core/description/typeDescription.h"   // ibTypeDescription + qualifiers


////////////////////////////////////////////////////////////////////////

wxVariantData* ibPropertyType::CreateVariantData(ibPropertyObject* property, const ibValueTypes type)
{
	const ibBackendTypeConfigFactory* propFactory = dynamic_cast<const ibBackendTypeConfigFactory*>(property);
	if (propFactory == nullptr)
		return nullptr;
	return new ibVariantDataAttribute(propFactory, type);
}

wxVariantData* ibPropertyType::CreateVariantData(ibPropertyObject* property, const ibClassID& clsid)
{
	const ibBackendTypeConfigFactory* propFactory = dynamic_cast<const ibBackendTypeConfigFactory*>(property);
	if (propFactory == nullptr)
		return nullptr;
	return new ibVariantDataAttribute(propFactory, clsid);
}

wxVariantData* ibPropertyType::CreateVariantData(ibPropertyObject* property, const ibTypeDescription& typeDesc)
{
	const ibBackendTypeConfigFactory* propFactory = dynamic_cast<const ibBackendTypeConfigFactory*>(property);
	if (propFactory == nullptr)
		return nullptr;
	return new ibVariantDataAttribute(propFactory, typeDesc);
}

////////////////////////////////////////////////////////////////////////

ibTypeDescription& ibPropertyType::GetValueAsTypeDesc() const {
	return get_cell_variant<ibVariantDataAttribute>()->GetTypeDesc();
}

ibTypeDescription& ibPropertyType::GetValueAsTypeValueDesc() const {
	return get_cell_variant<ibVariantDataAttribute>()->GetTypeValueDesc();
}

void ibPropertyType::SetValue(const ibTypeDescription& val) {
	m_propValue = CreateVariantData(m_owner, val);
}

////////////////////////////////////////////////////////////////////////

ibSelectorDataType ibPropertyType::GetFilterDataType() const {
	return get_cell_variant<ibVariantDataAttribute>()->GetFilterDataType();
}

////////////////////////////////////////////////////////////////////////

#include "backend/system/value/valueType.h"

//base property for "type"
bool ibPropertyType::SetDataValue(const ibValue& varPropVal)
{
	ibValueTypeDescription* valueType = varPropVal.ConvertToType<ibValueTypeDescription>();
	if (valueType != nullptr) {
		SetValue(valueType->GetTypeDesc());
		return true;
	}
	return false;
}

bool ibPropertyType::GetDataValue(ibValue& pvarPropVal) const
{
	pvarPropVal = ibValue::CreateObjectValue<ibValueTypeDescription>(GetValueAsTypeDesc());
	return true;
}

// composite -> Child (a struct): a "types" Array of clsids + qualifier fields.
// The conversion lives once on ibTypeDescriptionMemory (shared with predefined attrs).
bool ibPropertyType::ReadNodeValue(const ibDataValue& value)
{
	return ibTypeDescriptionMemory::ReadNode(value, GetValueAsTypeDesc());
}

bool ibPropertyType::WriteNodeValue(ibDataValue& value) const
{
	return ibTypeDescriptionMemory::WriteNode(value, GetValueAsTypeDesc());
}

////////////////////////////////////////////////////////////////////////

#include "protocol/protocol.h"         // ibProtocol::ibSchema::ibProperties — the row's keys
#include "backend/backend_picture.h"   // ibBackendPicture::SendPicture — a type's picture, by its class
#include "backend/metaData.h"          // GetAvailableCtor — a type, named as its configuration names it
#include "backend/objCtor.h"

#include <algorithm>

bool ibPropertyType::GetProtocolValue(ibDataNode& row, ibReader reader)
{
	if (!ibProperty::GetProtocolValue(row, reader))
		return false;

	const ibSelectorDataType filter = GetFilterDataType();
	const ibPropertyObject* const owner = m_owner;   // CONST: the non-const GetMetaData wxFAILs
	const ibMetaData* const metaData = owner != nullptr ? owner->GetMetaData() : nullptr;

	std::vector<ibClassID> offered;
	ibBackendTypeConfigFactory::GetTypesByFilter(filter, metaData, offered);
	for (const ibClassID& held : GetValueAsTypeDesc().GetClsidList()) {
		if (std::find(offered.begin(), offered.end(), held) == offered.end())
			offered.push_back(held);
	}

	row.SetValue(ibProtocol::ibSchema::ibProperties::Multiple, filter != ibSelectorDataType::ibSelectorDataType_table);
	for (const ibClassID& clsid : offered) {
		const ibCtorAbstractType* const so = metaData != nullptr
			? metaData->GetAvailableCtor(clsid) : ibValue::GetAvailableCtor(clsid);
		if (so == nullptr)
			continue;
		ibDataNode& type = row.AddChild(0, static_cast<ibMetaID>(row.Children().size() + 1));
		type.SetValue(ibProtocol::ibSchema::ibProperties::Type, wxString::Format(wxT("%llu"), static_cast<unsigned long long>(clsid)));
		type.SetValue(ibProtocol::ibSchema::ibProperties::Title, so->GetClassName());
		type.SetValue(ibProtocol::ibSchema::ibProperties::Picture, ibBackendPicture::SendPicture(clsid));
	}
	return true;
}
