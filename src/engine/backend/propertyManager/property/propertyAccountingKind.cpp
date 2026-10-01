#include "propertyAccountingKind.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/propertyManager/property/variant/variantOwner.h"
#include "backend/metaCollection/partial/chartOfAccounts.h"   // the chart whose kinds these are

wxVariantData* ibPropertyAccountingKind::CreateVariantData(ibPropertyObject* property, const ibMetaDescription& typeDesc)
{
	return new ibVariantDataOwner(property, typeDesc);
}

ibMetaDescription& ibPropertyAccountingKind::GetValueAsMetaDesc() const
{
	return get_cell_variant<ibVariantDataMetaDesc>()->GetMetaDesc();
}

void ibPropertyAccountingKind::SetValue(const ibMetaDescription& val)
{
	m_propValue = CreateVariantData(m_owner, val);
}

// The family rule — see propertyRecord.cpp.
void ibPropertyAccountingKind::DoSetValue(const wxVariant& val)
{
	if (const ibVariantDataMetaDesc* carried = find_cell_variant<ibVariantDataMetaDesc>(val)) {
		SetValue(carried->GetMetaDesc());
		return;
	}

	ibProperty::DoSetValue(val);
}

// THE KINDS OF THIS REGISTER'S CHART — see the note in the header. The chart comes from the owner that
// declared this property; no chart means the field stands under no accounting register, which is the
// same answer as "this question is not yours" — the state of every dimension of an accumulation
// register, where the property is hidden anyway.
ibPropertyChoiceMode ibPropertyAccountingKind::GetValueList(ibPropertyChoiceList& list)
{
	const ibPropertyObject* owner = m_owner;
	const ibValueMetaObjectChartOfAccounts* chart = m_chartOfAccounts ? m_chartOfAccounts() : nullptr;
	if (chart == nullptr)
		return ibPropertyChoiceMode::None;

	// One entry per kind, built the way the shared builder builds them: the metaID says WHICH, the
	// variant is the relationship to place, so a caller that picked an item has nothing left to do.
	const auto add = [&list, owner](const ibValueMetaObject* kind) {
		if (kind == nullptr || kind->IsDeleted())
			return;
		list.Add((long)kind->GetMetaID(), kind->GetName(), kind->GetSynonym(),
			wxVariant(new ibVariantDataOwner(owner, ibMetaDescription(kind->GetMetaID()))),
			kind->GetIcon());
	};

	if (m_level == Level::Account) {
		for (const ibValueMetaObjectAccountingKind* kind : chart->GetAccountingKindArrayObject())
			add(kind);
	}
	else {
		for (const ibValueMetaObjectAccountDimensionAccountingKind* kind : chart->GetAccountDimensionAccountingKindArrayObject())
			add(kind);
	}

	return ibPropertyChoiceMode::Single;
}

bool ibPropertyAccountingKind::SetDataValue(const ibValue& varPropVal) { return false; }

bool ibPropertyAccountingKind::GetDataValue(ibValue& pvarPropVal) const
{
	const ibVariantDataOwner* owner = get_cell_variant<ibVariantDataOwner>();
	wxASSERT(owner);
	pvarPropVal = owner->GetDataValue();
	return true;
}

bool ibPropertyAccountingKind::ReadNodeValue(const ibDataValue& value)
{
	ibMetaDescriptionMemory::ReadNode(value, GetValueAsMetaDesc());
	return true;
}

bool ibPropertyAccountingKind::WriteNodeValue(ibDataValue& value) const
{
	const ibPropertyObject* owner = m_owner;   // CONST overload — see propertyObject.h
	return ibMetaDescriptionMemory::WriteNode(value, GetValueAsMetaDesc(), owner->GetMetaData());
}
