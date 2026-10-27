#include "propertyChoiceLink.h"

#include "core/serialize/dataBuilder.h"   // ibDataNode — the row the lists come with
#include "frmclient/win/picture.h"        // ibProtocolPicture — an item's picture, as the wire names it
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

void AddItem(ibPropertyChoiceList& list, const ibDataNode& item)
{
	const ibString title = item.GetValue<ibString>(ibProtocol::ibSchema::ibProperties::Title);
	const std::int32_t id = item.GetValue<std::int32_t>(ibProtocol::ibSchema::ibProperties::Id);
	list.Add(id, title, ibEmptyString, wxVariant((long)id),
		ibProtocolPicture(item, ibProtocol::ibSchema::ibProperties::Picture));
}

// The target of that type among the row's Targets; none: the row brings no such one.
std::optional<ibDataNode> Target(const ibDataNode& row, const ibMetaID& type)
{
	if (const auto targets = row.FindChild(ibProtocol::ibSchema::ibProperties::Targets)) {
		for (const ibDataNode& target : targets->Children()) {
			if ((ibMetaID)target.GetValue<std::int32_t>(ibProtocol::ibSchema::ibProperties::Id) == type)
				return target;
		}
	}
	return std::nullopt;
}

}

void ibFieldReferenceTypes(const ibBackendProperty* property, ibPropertyChoiceList& list)
{
	const ibDataNode row = RowOf(property);
	if (const auto targets = row.FindChild(ibProtocol::ibSchema::ibProperties::Targets)) {
		for (const ibDataNode& target : targets->Children())
			AddItem(list, target);
	}
}

ibClassID ibFieldReferenceClass(const ibBackendProperty* property, const ibMetaID& type)
{
	const ibDataNode row = RowOf(property);
	const auto target = Target(row, type);
	if (!target)
		return 0;
	unsigned long long clsid = 0;
	target->GetValue<ibString>(ibProtocol::ibSchema::ibProperties::Type).ToULongLong(&clsid);
	return static_cast<ibClassID>(clsid);
}

void ibChoiceParameterList(const ibBackendProperty* property, const ibMetaID& type, ibPropertyChoiceList& list)
{
	const ibDataNode row = RowOf(property);
	if (const auto target = Target(row, type)) {
		for (const ibDataNode& field : target->Children())
			AddItem(list, field);
	}
}

void ibChoiceSourceList(const ibBackendProperty* property, ibPropertyChoiceList& list)
{
	const ibDataNode row = RowOf(property);
	if (const auto sources = row.FindChild(ibProtocol::ibSchema::ibProperties::Sources)) {
		for (const ibDataNode& source : sources->Children())
			AddItem(list, source);
	}
}

bool ibChoiceHasLists(const ibBackendProperty* property)
{
	const ibDataNode row = RowOf(property);
	return row.FindChild(ibProtocol::ibSchema::ibProperties::Targets)
		&& row.FindChild(ibProtocol::ibSchema::ibProperties::Sources);
}

ibString ibChoiceHolderName(const ibBackendProperty* property)
{
	const ibDataNode row = RowOf(property);
	const auto sources = row.FindChild(ibProtocol::ibSchema::ibProperties::Sources);
	return sources ? sources->GetValue<ibString>(ibProtocol::ibSchema::ibProperties::Title) : ibString();
}

void ibChoiceParametersForSource(const ibBackendProperty* property, const ibMetaID& sourceId,
	std::vector<ibMetaID>& out, ibMetaID* obvious)
{
	if (obvious != nullptr)
		*obvious = 0;
	const ibDataNode row = RowOf(property);
	const auto sources = row.FindChild(ibProtocol::ibSchema::ibProperties::Sources);
	if (!sources)
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
