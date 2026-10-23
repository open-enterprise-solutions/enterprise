#include "propertyModule.h"
#include "core/serialize/dataBuilder.h"
#include "backend/propertyManager/property/variant/variantModule.h"

#define chunkForm 0x023456543


////////////////////////////////////////////////////////////////////////

wxVariantData* ibPropertyModule::CreateVariantData()
{
	return new ibVariantDataModule();
}

////////////////////////////////////////////////////////////////////////

wxString ibPropertyModule::GetValueAsString() const
{
	return get_cell_variant<ibVariantDataModule>()->GetModuleText();
}

void ibPropertyModule::SetValue(const wxString& val)
{
	get_cell_variant<ibVariantDataModule>()->SetModuleText(val);
}

////////////////////////////////////////////////////////////////////////

//base property for "module"
bool ibPropertyModule::SetDataValue(const ibValue& varPropVal)
{
	return false;
}

bool ibPropertyModule::GetDataValue(ibValue& pvarPropVal) const
{
	pvarPropVal = GetName();
	return true;
}

bool ibPropertyModule::ReadNodeValue(const ibDataValue& value)
{
	ibPropertyModule::SetValue(value.AsString());
	return true;
}

bool ibPropertyModule::WriteNodeValue(ibDataValue& value) const
{
	value = ibDataValue::String(ibPropertyModule::GetValueAsString());
	return true;
}

#include "backend/metaCollection/metaObject.h"   // ibValueMetaObject — the owner, the link's object
#include "protocol/protocol.h"                   // ibProtocol::ibSchema::ibProperties::Object

bool ibPropertyModule::GetProtocolValue(ibDataNode& row, ibReader reader)
{
	if (!ibProperty::GetProtocolValue(row, reader))
		return false;
	row.SetValue(ibProtocol::ibSchema::ibProperties::Object, static_cast<std::int32_t>(static_cast<const ibValueMetaObject*>(m_owner)->GetMetaID()));
	return true;
}