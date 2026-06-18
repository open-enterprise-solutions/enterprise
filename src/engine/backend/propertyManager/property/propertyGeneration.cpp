#include "propertyGeneration.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/propertyManager/property/variant/variantGen.h"

// get property for grid
wxObject* (*ibPropertyGeneration::ms_propertyGeneration)(ibPropertyObject*, const wxString&, const wxString&, const wxVariant&) = nullptr;

/////////////////////////////////////////////////////////////////////

wxVariantData* ibPropertyGeneration::CreateVariantData(ibPropertyObject* property, const ibMetaDescription& typeDesc) const
{
	const ibValueMetaObjectGenericData* propFactory = dynamic_cast<const ibValueMetaObjectGenericData*>(property);
	if (propFactory == nullptr)
		return nullptr;
	return new ibVariantDataGeneration(propFactory, typeDesc);
}

ibMetaDescription& ibPropertyGeneration::GetValueAsMetaDesc() const {
	return get_cell_variant<ibVariantDataGeneration>()->GetMetaDesc();
}

void ibPropertyGeneration::SetValue(const ibMetaDescription& val)
{
	m_propValue = CreateVariantData(m_owner, val);
}

//base property for "generation"
bool ibPropertyGeneration::SetDataValue(const ibValue& varPropVal)
{
	return false;
}

bool ibPropertyGeneration::GetDataValue(ibValue& pvarPropVal) const
{
	const ibVariantDataGeneration* gen = get_cell_variant<ibVariantDataGeneration>();
	wxASSERT(gen);
	pvarPropVal = gen->GetDataValue();
	return true;
}

bool ibPropertyGeneration::ReadNodeValue(const ibDataValue& value)
{
	return ibMetaDescriptionMemory::ReadNode(value, GetValueAsMetaDesc());
}

bool ibPropertyGeneration::WriteNodeValue(ibDataValue& value) const
{
	return ibMetaDescriptionMemory::WriteNode(value, GetValueAsMetaDesc());
}