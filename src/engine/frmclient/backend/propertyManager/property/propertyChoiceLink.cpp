#include "propertyChoiceLink.h"

#include "core/serialize/dataBuilder.h"   // ibDataNode — the row the lists come with
#include "frmclient/win/picture.h"        // ibProtocolPicture — an item's picture, as the wire carries it
#include "protocol/protocol.h"            // ibProtocol::ibSchema::ibProperties — the row's keys, as the server's property writes them

// WHAT A FIELD'S CHOICE WINDOWS ASK, READ OFF ITS ROW — the server writes it where the desktop's windows asked the
// configuration (its ibPropertyChoiceLink / ibPropertyChoiceParameters::GetProtocolValue): the row's Targets (the
// field's reference types, each its fields a parameter may fill) and Sources (the fields beside it, each the
// parameters it could fill).

namespace {

// The row the property came of (its GetProtocolValue); none: empty, and every list with it.
ibDataNode RowOf(const ibBackendProperty* property)
{
	ibDataNode row;
	if (property != nullptr)
		property->GetProtocolValue(row);
	return row;
}

// A list the row brings (Targets, Sources); none: empty.
std::shared_ptr<ibDataNode> RowNode(const ibDataNode& row, const char* name)
{
	return row.GetProperty(name).AsChild();
}

void AddItem(ibPropertyChoiceList& list, const ibDataNode& item)
{
	const wxString title = item.GetValue<wxString>(ibProtocol::ibSchema::ibProperties::Title);
	const std::int32_t id = item.GetValue<std::int32_t>(ibProtocol::ibSchema::ibProperties::Id);
	list.Add(id, title, wxEmptyString, wxVariant((long)id),
		ibProtocolPicture(item.GetValue<wxString>(ibProtocol::ibSchema::ibProperties::Picture)));
}

const ibDataNode* Target(const ibDataNode& row, const ibMetaID& type)
{
	if (const std::shared_ptr<ibDataNode> targets = RowNode(row, ibProtocol::ibSchema::ibProperties::Targets)) {
		for (const ibDataNode& target : targets->Children()) {
			if ((ibMetaID)target.GetValue<std::int32_t>(ibProtocol::ibSchema::ibProperties::Id) == type)
				return &target;
		}
	}
	return nullptr;
}

}

void ibFieldReferenceTypes(const ibBackendProperty* property, ibPropertyChoiceList& list)
{
	const ibDataNode row = RowOf(property);
	if (const std::shared_ptr<ibDataNode> targets = RowNode(row, ibProtocol::ibSchema::ibProperties::Targets)) {
		for (const ibDataNode& target : targets->Children())
			AddItem(list, target);
	}
}

ibClassID ibFieldReferenceClass(const ibBackendProperty* property, const ibMetaID& type)
{
	const ibDataNode row = RowOf(property);
	const ibDataNode* const target = Target(row, type);
	if (target == nullptr)
		return 0;
	unsigned long long clsid = 0;
	target->GetValue<wxString>(ibProtocol::ibSchema::ibProperties::Type).ToULongLong(&clsid);
	return static_cast<ibClassID>(clsid);
}

void ibChoiceParameterList(const ibBackendProperty* property, const ibMetaID& type, ibPropertyChoiceList& list)
{
	const ibDataNode row = RowOf(property);
	if (const ibDataNode* const target = Target(row, type)) {
		for (const ibDataNode& field : target->Children())
			AddItem(list, field);
	}
}

void ibChoiceSourceList(const ibBackendProperty* property, ibPropertyChoiceList& list)
{
	const ibDataNode row = RowOf(property);
	if (const std::shared_ptr<ibDataNode> sources = RowNode(row, ibProtocol::ibSchema::ibProperties::Sources)) {
		for (const ibDataNode& source : sources->Children())
			AddItem(list, source);
	}
}

bool ibChoiceHasLists(const ibBackendProperty* property)
{
	const ibDataNode row = RowOf(property);
	return RowNode(row, ibProtocol::ibSchema::ibProperties::Targets) != nullptr && RowNode(row, ibProtocol::ibSchema::ibProperties::Sources) != nullptr;
}

wxString ibChoiceHolderName(const ibBackendProperty* property)
{
	const ibDataNode row = RowOf(property);
	const std::shared_ptr<ibDataNode> sources = RowNode(row, ibProtocol::ibSchema::ibProperties::Sources);
	return sources != nullptr ? sources->GetValue<wxString>(ibProtocol::ibSchema::ibProperties::Title) : wxString();
}

void ibChoiceParametersForSource(const ibBackendProperty* property, const ibMetaID& sourceId,
	std::vector<ibMetaID>& out, ibMetaID* obvious)
{
	if (obvious != nullptr)
		*obvious = 0;
	const ibDataNode row = RowOf(property);
	const std::shared_ptr<ibDataNode> sources = RowNode(row, ibProtocol::ibSchema::ibProperties::Sources);
	if (sources == nullptr)
		return;
	for (const ibDataNode& source : sources->Children()) {
		if ((ibMetaID)source.GetValue<std::int32_t>(ibProtocol::ibSchema::ibProperties::Id) != sourceId)
			continue;
		for (const ibDataNode& fit : source.Children())
			out.push_back((ibMetaID)fit.GetValue<std::int32_t>(ibProtocol::ibSchema::ibProperties::Id));
		if (obvious != nullptr)
			*obvious = (ibMetaID)source.GetValue<std::int32_t>(ibProtocol::ibSchema::ibProperties::Default);
		return;
	}
}
