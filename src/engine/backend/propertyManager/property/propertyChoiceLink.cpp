#include "propertyChoiceLink.h"
#include "backend/propertyManager/property/variant/variantChoiceLink.h"   // the value — and everything known about it

// ⭐ THE PROPERTY HOLDS THE VALUE, THE VALUE KNOWS ITSELF — as ibPropertyType holds an ibVariantDataAttribute.
// What a link or a row may name, how it is spelled and what is gone from it are the variant's
// (variantChoiceLink.cpp); the property makes it, hands it over and asks it for its lists.

wxVariantData* ibPropertyChoiceLink::CreateVariantData(ibPropertyObject* property, const ibChoiceTypeLinkDescription& linkDesc) const
{
	return new ibVariantDataChoiceLink(property, linkDesc);
}

ibChoiceTypeLinkDescription& ibPropertyChoiceLink::GetValueAsLinkDesc() const
{
	return get_cell_variant<ibVariantDataChoiceLink>()->GetLinkDesc();
}

void ibPropertyChoiceLink::SetValue(const ibChoiceTypeLinkDescription& val)
{
	m_propValue = CreateVariantData(m_owner, val);
}

// The family rule — see propertyRecord.cpp.
void ibPropertyChoiceLink::DoSetValue(const wxVariant& val)
{
	if (const ibVariantDataChoiceLink* carried = find_cell_variant<ibVariantDataChoiceLink>(val)) {
		SetValue(carried->GetLinkDesc());
		return;
	}

	ibProperty::DoSetValue(val);
}

ibPropertyChoiceMode ibPropertyChoiceLink::GetValueList(ibPropertyChoiceList& list)
{
	return get_cell_variant<ibVariantDataChoiceLink>()->GetValueList(list);
}

bool ibPropertyChoiceLink::SetDataValue(const ibValue& varPropVal) { return false; }

bool ibPropertyChoiceLink::GetDataValue(ibValue& pvarPropVal) const
{
	wxString name;
	get_cell_variant<ibVariantDataChoiceLink>()->Write(name);
	pvarPropVal = ibValue(name);
	return true;
}

bool ibPropertyChoiceLink::ReadNodeValue(const ibDataValue& value)
{
	return ibChoiceLinkDescriptionMemory::ReadNode(value, GetValueAsLinkDesc());
}

bool ibPropertyChoiceLink::WriteNodeValue(ibDataValue& value) const
{
	return ibChoiceLinkDescriptionMemory::WriteNode(value, GetValueAsLinkDesc());
}

//*************************************************************************************************
//*                             The choice parameters — the table                                 *
//*************************************************************************************************

wxVariantData* ibPropertyChoiceParameters::CreateVariantData(ibPropertyObject* property, const ibChoiceParametersDescription& paramsDesc) const
{
	return new ibVariantDataChoiceParameters(property, paramsDesc);
}

ibChoiceParametersDescription& ibPropertyChoiceParameters::GetValueAsParametersDesc() const
{
	return get_cell_variant<ibVariantDataChoiceParameters>()->GetParametersDesc();
}

void ibPropertyChoiceParameters::SetValue(const ibChoiceParametersDescription& val)
{
	m_propValue = CreateVariantData(m_owner, val);
}

void ibPropertyChoiceParameters::DoSetValue(const wxVariant& val)
{
	if (const ibVariantDataChoiceParameters* carried = find_cell_variant<ibVariantDataChoiceParameters>(val)) {
		SetValue(carried->GetParametersDesc());
		return;
	}

	ibProperty::DoSetValue(val);
}

void ibPropertyChoiceParameters::GetParameterList(const ibClassID& ofType, ibPropertyChoiceList& list) const
{
	get_cell_variant<ibVariantDataChoiceParameters>()->GetParameterList(ofType, list);
}

void ibPropertyChoiceParameters::GetSourceList(ibPropertyChoiceList& list) const
{
	get_cell_variant<ibVariantDataChoiceParameters>()->GetSourceList(list);
}

bool ibPropertyChoiceParameters::SetDataValue(const ibValue& varPropVal) { return false; }

bool ibPropertyChoiceParameters::GetDataValue(ibValue& pvarPropVal) const
{
	wxString names;
	get_cell_variant<ibVariantDataChoiceParameters>()->Write(names);
	pvarPropVal = ibValue(names);
	return true;
}

bool ibPropertyChoiceParameters::ReadNodeValue(const ibDataValue& value)
{
	return ibChoiceLinkDescriptionMemory::ReadNode(value, GetValueAsParametersDesc());
}

bool ibPropertyChoiceParameters::WriteNodeValue(ibDataValue& value) const
{
	return ibChoiceLinkDescriptionMemory::WriteNode(value, GetValueAsParametersDesc());
}
