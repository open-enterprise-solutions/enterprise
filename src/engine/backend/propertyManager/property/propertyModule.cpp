#include "propertyModule.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/propertyManager/property/variant/variantModule.h"

#define chunkForm 0x023456543

// get property for grid
wxObject* (*ibPropertyModule::ms_propertyModule)(ibPropertyObject*, const wxString&, const wxString&, const wxVariant&) = nullptr;

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