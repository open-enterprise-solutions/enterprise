#include "propertyChoiceLink.h"
#include "backend/propertyManager/property/variant/variantChoiceLink.h"   // the value — and everything known about it

// ⭐ THE PROPERTY HOLDS THE VALUE, THE VALUE KNOWS ITSELF — as ibPropertyType holds an ibVariantDataAttribute.
// What a link or a row may name, how it is spelled and what is gone from it are the variant's
// (variantChoiceLink.cpp); the property makes it, hands it over and asks it for its lists.

wxVariantData* ibPropertyChoiceLink::CreateVariantData(ibPropertyObject* property, const ibChoiceTypeLinkDescription& linkDesc)
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

wxVariantData* ibPropertyChoiceParameters::CreateVariantData(ibPropertyObject* property, const ibChoiceParametersDescription& paramsDesc)
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

////////////////////////////////////////////////////////////////////////

#include "protocol/protocol.h"         // ibProtocol::ibSchema::ibProperties — the row's keys
#include "backend/backend_picture.h"   // ibBackendPicture::SendPicture — a type's, a field's picture, by its id
#include "backend/metaData.h"          // GetIDObjectFromMetaID — the class a target's reference is registered under

namespace {

// One item — its Id, its Title (a name, as the desktop's windows named it) and its Picture.
ibDataNode& AppendItem(ibDataNode& parent, const ibPropertyChoiceList& list, unsigned int index, const ibMetaData* metaData)
{
	ibDataNode& item = parent.AddChild(0, static_cast<ibMetaID>(parent.Children().size() + 1));
	item.SetValue(ibProtocol::ibSchema::ibProperties::Id, static_cast<std::int32_t>(list.GetId(index)));
	item.SetValue(ibProtocol::ibSchema::ibProperties::Title, list.GetName(index));
	const ibPictureDescription& picture = list.GetPicture(index);
	if (!picture.IsEmptyPicture())
		item.SetValue(ibProtocol::ibSchema::ibProperties::Picture, ibBackendPicture::SendPicture(picture, metaData));
	return item;
}

// THE FIELD'S REFERENCE TYPES — what both windows name a target by; for the parameters, each its fields.
void AppendTargets(ibDataNode& row, const ibPropertyObject* owner, const ibPropertyChoiceParameters* parameters)
{
	const ibMetaData* const metaData = owner != nullptr ? owner->GetMetaData() : nullptr;
	auto targets = std::make_shared<ibDataNode>();
	ibPropertyChoiceList types;
	ibFieldReferenceTypes(owner, types);
	for (unsigned int index = 0; index < types.GetCount(); ++index) {
		ibDataNode& target = AppendItem(*targets, types, index, metaData);
		const ibClassID clsid = metaData != nullptr ? metaData->GetIDObjectFromMetaID(
			static_cast<ibMetaID>(types.GetId(index)), ibCtorObjectMetaType::ibCtorObjectMetaType_Reference) : 0;
		target.SetValue(ibProtocol::ibSchema::ibProperties::Type, wxString::Format(wxT("%llu"), static_cast<unsigned long long>(clsid)));
		if (parameters == nullptr || clsid == 0)
			continue;
		ibPropertyChoiceList fields;
		parameters->GetParameterList(clsid, fields);
		for (unsigned int field = 0; field < fields.GetCount(); ++field)
			AppendItem(target, fields, field, metaData);
	}
	row.SetProperty(ibProtocol::ibSchema::ibProperties::Targets, ibDataValue::Child(targets));
}

} // namespace

bool ibPropertyChoiceLink::GetProtocolValue(ibDataNode& row, ibReader reader)
{
	if (!ibProperty::GetProtocolValue(row, reader))
		return false;
	AppendTargets(row, m_owner, nullptr);
	return true;
}

bool ibPropertyChoiceParameters::GetProtocolValue(ibDataNode& row, ibReader reader)
{
	if (!ibProperty::GetProtocolValue(row, reader))
		return false;
	AppendTargets(row, m_owner, this);
	const ibPropertyObject* const owner = m_owner;   // CONST: the non-const GetMetaData wxFAILs
	const ibMetaData* const metaData = owner != nullptr ? owner->GetMetaData() : nullptr;   // the fields beside it are of its configuration

	auto sources = std::make_shared<ibDataNode>();
	sources->SetValue(ibProtocol::ibSchema::ibProperties::Title, ibChoiceHolderName(m_owner));
	ibPropertyChoiceList neighbours;
	GetSourceList(neighbours);
	for (unsigned int index = 0; index < neighbours.GetCount(); ++index) {
		ibDataNode& source = AppendItem(*sources, neighbours, index, metaData);
		std::vector<ibMetaID> fits;
		ibMetaID obvious = 0;
		ibChoiceParametersForSource(m_owner, static_cast<ibMetaID>(neighbours.GetId(index)), fits, &obvious);
		if (obvious != 0)
			source.SetValue(ibProtocol::ibSchema::ibProperties::Default, static_cast<std::int32_t>(obvious));
		for (const ibMetaID& fit : fits) {
			ibDataNode& item = source.AddChild(0, static_cast<ibMetaID>(source.Children().size() + 1));
			item.SetValue(ibProtocol::ibSchema::ibProperties::Id, static_cast<std::int32_t>(fit));
		}
	}
	row.SetProperty(ibProtocol::ibSchema::ibProperties::Sources, ibDataValue::Child(sources));
	return true;
}
