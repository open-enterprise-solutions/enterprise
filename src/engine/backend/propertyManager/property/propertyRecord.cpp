#include "propertyRecord.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/propertyManager/property/variant/variantRecord.h"

wxObject* (*ibPropertyRecord::ms_propertyRecord)(ibPropertyObject*, const wxString&, const wxString&, const wxVariant&) = nullptr;

////////////////////////////////////////////////////////////////////

wxVariantData* ibPropertyRecord::CreateVariantData(ibPropertyObject* property, const ibMetaDescription& typeDesc) const
{
	const ibValueMetaObjectGenericData* propFactory = dynamic_cast<const ibValueMetaObjectGenericData*>(property);
	if (propFactory == nullptr)
		return nullptr;
	return new ibVariantDataRecord(propFactory, typeDesc);
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