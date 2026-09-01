#include "propertyRecord.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/propertyManager/property/variant/variantRecord.h"


////////////////////////////////////////////////////////////////////

wxVariantData* ibPropertyRecord::CreateVariantData(ibPropertyObject* property, const ibMetaDescription& typeDesc) const
{
	// No cast: the variant needs the owner only to reach GetMetaData, which ibPropertyObject answers.
	return new ibVariantDataRecord(property, typeDesc);
}

ibMetaDescription& ibPropertyRecord::GetValueAsMetaDesc() const {
	return get_cell_variant<ibVariantDataRecord>()->GetMetaDesc();
}

ibMetaDescription& ibPropertyRecord::GetValueAsMetaDesc(const wxVariant& val) const {
	return get_cell_variant<ibVariantDataRecord>(val)->GetMetaDesc();
}

void ibPropertyRecord::SetValue(const ibMetaDescription& val)
{
	m_propValue = CreateVariantData(m_owner, val);
}

// Every kind of register a document can post to. The list used to sit in advpropRecord.cpp.
ibPropertyChoiceMode ibPropertyRecord::GetValueList(ibPropertyChoiceList& list)
{
	return CreateValueList(list, ibPropertyChoiceMode::Mult, {
			g_metaInformationRegisterCLSID,
			g_metaAccumulationRegisterCLSID,
			g_metaAccountingRegisterCLSID },
		// ONLY A REGISTER WITH A RECORDER. A document posts by recording itself as the recorder; a
		// register that has none cannot hold its movements, so offering it would be offering an
		// impossible binding. This rule was inside the front editor's fill loop and would have been
		// lost by moving only the classes down.
		[](const ibPropertyObject* object) {
			const ibValueMetaObjectRegisterData* reg = dynamic_cast<const ibValueMetaObjectRegisterData*>(object);
			return reg != nullptr && reg->HasRecorder();
		});
}

//base property for "record"
bool ibPropertyRecord::SetDataValue(const ibValue& varPropVal)
{
	return false;
}

bool ibPropertyRecord::GetDataValue(ibValue& pvarPropVal) const
{
	const ibVariantDataRecord* gen = get_cell_variant<ibVariantDataRecord>();
	wxASSERT(gen);
	pvarPropVal = gen->GetDataValue();
	return true;
}

bool ibPropertyRecord::ReadNodeValue(const ibDataValue& value)
{
	return ibMetaDescriptionMemory::ReadNode(value, GetValueAsMetaDesc());
}

bool ibPropertyRecord::WriteNodeValue(ibDataValue& value) const
{
	return ibMetaDescriptionMemory::WriteNode(value, GetValueAsMetaDesc());
}