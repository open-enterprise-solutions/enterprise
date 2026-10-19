#include "propertyChoiceLink.h"
#include "variant/variantChoiceLink.h"

#include "propertySerializable.h"
#include "frmclient/win/picture.h"   // ibProtocolPicture — an item's picture, as the wire carries it
#include "protocol/protocol.h"       // ibProtocolName — the row's keys, as the server's inspector writes them

// WHAT A FIELD'S CHOICE WINDOWS ASK, READ OFF ITS ROW — the server writes it where the desktop's windows asked the
// configuration (frmserver ibPropertyInspector, AppendChoiceTargets): the row's Targets (the field's reference types,
// each its fields a parameter may fill) and Sources (the fields beside it, each the parameters it could fill).

namespace {

const ibDataNode* RowNode(const ibBackendProperty* property, const char* name)
{
	const ibPropertySerializable* const row = dynamic_cast<const ibPropertySerializable*>(property);
	if (row == nullptr)
		return nullptr;
	const ibDataValue* const value = row->GetRow().FindProperty(name);
	return value != nullptr && value->Kind() == ibDataKind::Child ? value->AsChild().get() : nullptr;
}

void AddItem(ibPropertyChoiceList& list, const ibDataNode& item)
{
	const wxString title = item.GetValue<wxString>(ibProtocolName::Title);
	const s32 id = item.GetValue<s32>(ibProtocolName::Id);
	list.Add(id, title, wxEmptyString, wxVariant((long)id),
		ibProtocolPicture(item.GetValue<wxString>(ibProtocolName::Picture)));
}

const ibDataNode* Target(const ibBackendProperty* property, const ibMetaID& type)
{
	if (const ibDataNode* const targets = RowNode(property, ibProtocolName::Targets)) {
		for (const ibDataNode& target : targets->Children()) {
			if ((ibMetaID)target.GetValue<s32>(ibProtocolName::Id) == type)
				return &target;
		}
	}
	return nullptr;
}

}

void ibFieldReferenceTypes(const ibBackendProperty* property, ibPropertyChoiceList& list)
{
	if (const ibDataNode* const targets = RowNode(property, ibProtocolName::Targets)) {
		for (const ibDataNode& target : targets->Children())
			AddItem(list, target);
	}
}

ibClassID ibFieldReferenceClass(const ibBackendProperty* property, const ibMetaID& type)
{
	const ibDataNode* const target = Target(property, type);
	if (target == nullptr)
		return 0;
	unsigned long long clsid = 0;
	target->GetValue<wxString>(ibProtocolName::Type).ToULongLong(&clsid);
	return static_cast<ibClassID>(clsid);
}

void ibChoiceParameterList(const ibBackendProperty* property, const ibMetaID& type, ibPropertyChoiceList& list)
{
	if (const ibDataNode* const target = Target(property, type)) {
		for (const ibDataNode& field : target->Children())
			AddItem(list, field);
	}
}

void ibChoiceSourceList(const ibBackendProperty* property, ibPropertyChoiceList& list)
{
	if (const ibDataNode* const sources = RowNode(property, ibProtocolName::Sources)) {
		for (const ibDataNode& source : sources->Children())
			AddItem(list, source);
	}
}

bool ibChoiceHasLists(const ibBackendProperty* property)
{
	return RowNode(property, ibProtocolName::Targets) != nullptr && RowNode(property, ibProtocolName::Sources) != nullptr;
}

wxString ibChoiceHolderName(const ibBackendProperty* property)
{
	const ibDataNode* const sources = RowNode(property, ibProtocolName::Sources);
	return sources != nullptr ? sources->GetValue<wxString>(ibProtocolName::Title) : wxString();
}

void ibChoiceParametersForSource(const ibBackendProperty* property, const ibMetaID& sourceId,
	std::vector<ibMetaID>& out, ibMetaID* obvious)
{
	if (obvious != nullptr)
		*obvious = 0;
	const ibDataNode* const sources = RowNode(property, ibProtocolName::Sources);
	if (sources == nullptr)
		return;
	for (const ibDataNode& source : sources->Children()) {
		if ((ibMetaID)source.GetValue<s32>(ibProtocolName::Id) != sourceId)
			continue;
		for (const ibDataNode& fit : source.Children())
			out.push_back((ibMetaID)fit.GetValue<s32>(ibProtocolName::Id));
		if (obvious != nullptr)
			*obvious = (ibMetaID)source.GetValue<s32>(ibProtocolName::Default);
		return;
	}
}
