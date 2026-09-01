#include "propertyGeneration.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/propertyManager/property/variant/variantGen.h"


/////////////////////////////////////////////////////////////////////

wxVariantData* ibPropertyGeneration::CreateVariantData(ibPropertyObject* property, const ibMetaDescription& typeDesc) const
{
	// No cast: the variant needs the owner only to reach GetMetaData, which ibPropertyObject answers.
	return new ibVariantDataGeneration(property, typeDesc);
}

ibMetaDescription& ibPropertyGeneration::GetValueAsMetaDesc() const {
	return get_cell_variant<ibVariantDataGeneration>()->GetMetaDesc();
}

void ibPropertyGeneration::SetValue(const ibMetaDescription& val)
{
	m_propValue = CreateVariantData(m_owner, val);
}

// Everything a document can be generated into. The list used to sit in advpropGeneration.cpp.
ibPropertyChoiceMode ibPropertyGeneration::GetValueList(ibPropertyChoiceList& list)
{
	return CreateValueList(list, ibPropertyChoiceMode::Mult, {
		g_metaCatalogCLSID,
		g_metaDocumentCLSID,
		g_metaChartOfCharacteristicTypesCLSID,
		g_metaChartOfAccountsCLSID });
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