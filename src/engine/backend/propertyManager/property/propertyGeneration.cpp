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
	return get_cell_variant<ibVariantDataMetaDesc>()->GetMetaDesc();
}

void ibPropertyGeneration::SetValue(const ibMetaDescription& val)
{
	m_propValue = CreateVariantData(m_owner, val);
}

// See propertyRecord.cpp: the description is right and the wrapper is the neighbour's, so it is
// taken apart and re-wrapped here, where the class this property holds is known.
void ibPropertyGeneration::DoSetValue(const wxVariant& val)
{
	// Unconditionally, and that is the point: "is this already mine" is a question worth not
	// asking. A relationship IS its description, so taking it out and wrapping it in this
	// property's own class is right whichever wrapper it arrived in - and costs one copy of a
	// short list of ids.
	if (const ibVariantDataMetaDesc* carried = find_cell_variant<ibVariantDataMetaDesc>(val)) {
		SetValue(carried->GetMetaDesc());
		return;
	}

	ibProperty::DoSetValue(val);
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