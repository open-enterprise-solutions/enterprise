#include "propertyMetaObjectChoice.h"
#include "core/serialize/dataBuilder.h"
#include "backend/propertyManager/property/variant/variantOwner.h"

ibVariant ibPropertyMetaObjectChoice::CreateVariantData(ibPropertyObject* property, const ibMetaDescription& typeDesc)
{
	return ibVariant(new ibVariantOwner(property, typeDesc));
}

ibMetaDescription& ibPropertyMetaObjectChoice::GetValueAsMetaDesc() const
{
	return get_cell_variant<ibVariantMetaDesc>()->GetMetaDesc();
}

void ibPropertyMetaObjectChoice::SetValue(const ibMetaDescription& val)
{
	m_propValue = CreateVariantData(m_owner, val);
}

void ibPropertyMetaObjectChoice::DoSetValue(const ibVariant& val)
{
	if (const ibVariantMetaDesc* carried = find_cell_variant<ibVariantMetaDesc>(val)) {
		SetValue(carried->GetMetaDesc());
		return;
	}
	ibProperty::DoSetValue(val);
}

ibPropertyChoiceMode ibPropertyMetaObjectChoice::GetValueList(ibPropertyChoiceList& list)
{
	return CreateValueList(list, ibPropertyChoiceMode::Single, { m_kind });
}

bool ibPropertyMetaObjectChoice::SetDataValue(const ibValue& varPropVal) { return false; }

bool ibPropertyMetaObjectChoice::GetDataValue(ibValue& pvarPropVal) const
{
	const ibVariantOwner* owner = get_cell_variant<ibVariantOwner>();
	wxASSERT(owner);
	pvarPropVal = owner->GetDataValue();
	return true;
}

bool ibPropertyMetaObjectChoice::ReadNodeValue(const ibDataValue& value)
{
	ibBackendMetaDescriptionMemory::ReadNode(value, GetValueAsMetaDesc());
	return true;
}

bool ibPropertyMetaObjectChoice::WriteNodeValue(ibDataValue& value) const
{
	const ibPropertyObject* owner = m_owner;   // CONST overload — the non-const one returns null (see propertyObject.h)
	return ibBackendMetaDescriptionMemory::WriteNode(value, GetValueAsMetaDesc(), owner->GetMetaData());
}
