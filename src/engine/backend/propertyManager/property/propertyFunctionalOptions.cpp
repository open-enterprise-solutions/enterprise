#include "propertyFunctionalOptions.h"
#include "core/serialize/dataBuilder.h"
#include "backend/propertyManager/property/variant/variantOwner.h"
#include "backend/metaCollection/metaObject.h"   // g_metaFunctionalOptionCLSID — the candidates' kind
#include "backend/functionalOption/functionalOptionGate.h"   // IsAvailable — the rule a member is available by

wxVariantData* ibPropertyFunctionalOptions::CreateVariantData(ibPropertyObject* property, const ibMetaDescription& metaDesc)
{
	return new ibVariantDataOwner(property, metaDesc);
}

ibMetaDescription& ibPropertyFunctionalOptions::GetValueAsMetaDesc() const {
	return get_cell_variant<ibVariantDataMetaDesc>()->GetMetaDesc();
}

void ibPropertyFunctionalOptions::SetValue(const ibMetaDescription& val)
{
	m_propValue = CreateVariantData(m_owner, val);
}

// The family rule — see propertyRecord.cpp.
void ibPropertyFunctionalOptions::DoSetValue(const wxVariant& val)
{
	if (const ibVariantDataMetaDesc* carried = find_cell_variant<ibVariantDataMetaDesc>(val)) {
		SetValue(carried->GetMetaDesc());
		return;
	}

	ibProperty::DoSetValue(val);
}

// The functional options of this configuration — any set of them, or one, as this property says.
ibPropertyChoiceMode ibPropertyFunctionalOptions::GetValueList(ibPropertyChoiceList& list)
{
	return CreateValueList(list, m_choiceMode, { g_metaFunctionalOptionCLSID });
}

bool ibPropertyFunctionalOptions::IsAvailable() const
{
	const ibMetaDescription& named = GetValueAsMetaDesc();
	if (named.GetTypeCount() == 0)
		return true;

	const ibPropertyObject* owner = m_owner;   // CONST overload — the non-const one returns null (see propertyObject.h)
	const std::set<ibMetaID> options(named.m_listMetaClass.begin(), named.m_listMetaClass.end());
	return ibFunctionalOptionGate::IsAvailable(owner != nullptr ? owner->GetMetaData() : nullptr, options);
}

bool ibPropertyFunctionalOptions::SetDataValue(const ibValue& varPropVal) { return false; }

bool ibPropertyFunctionalOptions::GetDataValue(ibValue& pvarPropVal) const
{
	const ibVariantDataOwner* owner = get_cell_variant<ibVariantDataOwner>();
	wxASSERT(owner);
	pvarPropVal = owner->GetDataValue();
	return true;
}

bool ibPropertyFunctionalOptions::ReadNodeValue(const ibDataValue& value)
{
	ibMetaDescriptionMemory::ReadNode(value, GetValueAsMetaDesc());
	return true;
}

bool ibPropertyFunctionalOptions::WriteNodeValue(ibDataValue& value) const
{
	const ibPropertyObject* owner = m_owner;   // CONST overload — the non-const one returns null (see propertyObject.h)
	return ibMetaDescriptionMemory::WriteNode(value, GetValueAsMetaDesc(), owner->GetMetaData());
}
