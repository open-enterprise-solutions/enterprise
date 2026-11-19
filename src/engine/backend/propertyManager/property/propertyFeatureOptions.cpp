#include "propertyFeatureOptions.h"
#include "core/serialize/dataBuilder.h"
#include "backend/propertyManager/property/variant/variantOwner.h"
#include "backend/metaCollection/metaObject.h"   // g_metaFeatureOptionCLSID — the candidates' kind
#include "backend/featureOption/featureOptionGate.h"   // IsAvailable — the rule a member is available by

ibVariant ibPropertyFeatureOptions::CreateVariantData(ibPropertyObject* property, const ibMetaDescription& metaDesc)
{
	return ibVariant(new ibVariantOwner(property, metaDesc));
}

ibMetaDescription& ibPropertyFeatureOptions::GetValueAsMetaDesc() const {
	return get_cell_variant<ibVariantMetaDesc>()->GetMetaDesc();
}

void ibPropertyFeatureOptions::SetValue(const ibMetaDescription& val)
{
	m_propValue = CreateVariantData(m_owner, val);
}

// The family rule — see propertyRecord.cpp.
void ibPropertyFeatureOptions::DoSetValue(const ibVariant& val)
{
	if (const ibVariantMetaDesc* carried = find_cell_variant<ibVariantMetaDesc>(val)) {
		SetValue(carried->GetMetaDesc());
		return;
	}

	ibProperty::DoSetValue(val);
}

// The feature options of this application — any set of them, or one, as this property says.
ibPropertyChoiceMode ibPropertyFeatureOptions::GetValueList(ibPropertyChoiceList& list)
{
	return CreateValueList(list, m_choiceMode, { g_metaFeatureOptionCLSID });
}

bool ibPropertyFeatureOptions::IsAvailable() const
{
	const ibMetaDescription& named = GetValueAsMetaDesc();
	if (named.GetTypeCount() == 0)
		return true;

	const ibPropertyObject* owner = m_owner;   // CONST overload — the non-const one returns null (see propertyObject.h)
	const std::set<ibMetaID> options(named.m_listMetaClass.begin(), named.m_listMetaClass.end());
	return ibFeatureOptionGate::IsAvailable(owner != nullptr ? owner->GetMetaData() : nullptr, options);
}

bool ibPropertyFeatureOptions::SetDataValue(const ibValue& varPropVal) { return false; }

bool ibPropertyFeatureOptions::GetDataValue(ibValue& pvarPropVal) const
{
	const ibVariantOwner* owner = get_cell_variant<ibVariantOwner>();
	ibASSERT(owner);
	pvarPropVal = owner->GetDataValue();
	return true;
}

bool ibPropertyFeatureOptions::ReadNodeValue(const ibDataValue& value)
{
	ibBackendMetaDescriptionMemory::ReadNode(value, GetValueAsMetaDesc());
	return true;
}

bool ibPropertyFeatureOptions::WriteNodeValue(ibDataValue& value) const
{
	const ibPropertyObject* owner = m_owner;   // CONST overload — the non-const one returns null (see propertyObject.h)
	return ibBackendMetaDescriptionMemory::WriteNode(value, GetValueAsMetaDesc(), owner->GetMetaData());
}
