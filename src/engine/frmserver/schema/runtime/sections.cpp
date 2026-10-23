#include "sections.h"

#include "backend/interfaceHelper.h"                          // ibInterfaceCommandSection, ibInterfaceCommandType
#include "backend/metaCollection/metaCommandGroupObject.h"    // the platform's groups + the declared ones, in order
#include "backend/metaCollection/metaFormObject.h"            // ibBackendCommandItem — what a link executes
#include "backend/metaCollection/metaSectionObject.h"
#include "backend/metadataConfiguration.h"
#include "core/serialize/dataBuilder.h"
#include "backend/session/session.h"                          // the configuration the session works in
#include "backend/backend_picture.h"                          // ibBackendPicture::SendPicture — the pictures, by their ids

#include <vector>

namespace {

// One block of a section's page — a heading (or none) and the items under it, in one of the panel's two columns.
struct ibSectionBlock {
	ibProtocol::ibSchema::ibSections::ibColumn column = ibProtocol::ibSchema::ibSections::ibColumn::Left;   // the navigation's;
	                                                                                                       // the actions' — Right
	wxString                            title;        // none — the section's own items at the top
	const ibValueMetaObjectCommandGroup* group = nullptr;   // a declared group's: its picture and tooltip
	ibInterfaceCommandSection           area = ibInterfaceCommandSection_Default;   // what its items open as
	std::vector<ibValueMetaObject*>     items;
};

// What a link of an area opens its item as — the panel's own answer (GetCommandType): a new item in Create, the
// default anywhere else.
ibInterfaceCommandType TypeOf(ibInterfaceCommandSection area)
{
	return area == ibInterfaceCommandSection_Create
		? ibInterfaceCommandType_Create : ibInterfaceCommandType_Default;
}

// A subsection's items, and its subsections' after them — once per subsection (the panel's NextChildConstruct).
void AppendNested(const ibValueMetaObjectSection* parent, std::vector<ibValueMetaObject*>& items)
{
	for (const ibValueMetaObjectSection* child : parent->GetInterfaceArrayObject()) {
		const std::vector<ibValueMetaObject*> subArray = child->GetInterfaceItemArrayObject();
		if (subArray.empty())
			continue;
		items.insert(items.end(), subArray.begin(), subArray.end());
		AppendNested(child, items);
	}
}

// THE PAGE OF A SECTION — block by block, in the panel's order (the section popup's constructor): on the left the
// section's own items (Important first, then Default), the navigation groups the configuration declares, then each
// subsection with what it and its own subsections hold; on the right the platform's action groups (Create, Reports,
// Service), then the declared ones.
std::vector<ibSectionBlock> PageOf(const ibValueMetaObjectSection* section)
{
	std::vector<ibSectionBlock> page;

	const ibMetaData* const metaData = section->GetMetaData();
	const std::vector<ibValueMetaObjectCommandGroup*> declaredGroups = metaData != nullptr
		? metaData->GetAnyArrayObject<ibValueMetaObjectCommandGroup>(g_metaCommandGroupCLSID)
		: std::vector<ibValueMetaObjectCommandGroup*>();

	// IMPORTANT FIRST, IN THE SAME BLOCK — "shown at the top with the main items" (interfaceHelper.h).
	{
		ibSectionBlock block;
		section->GetInterfaceItemArrayObject(ibInterfaceCommandSection_Important, block.items);
		if (section->GetInterfaceItemArrayObject(ibInterfaceCommandSection_Default, block.items))
			page.push_back(block);
	}

	// The navigation panel's declared groups, after the platform's own.
	for (const ibValueMetaObjectCommandGroup* group : declaredGroups) {
		if (group == nullptr || group->IsDeleted() || group->GetCategory() != ibCommandGroupCategory_Navigation)
			continue;
		ibSectionBlock block;
		if (!section->GetInterfaceItemArrayObject(group, block.items))
			continue;
		block.title = group->GetSynonym();
		block.group = group;
		page.push_back(block);
	}

	// Each subsection — each command ONCE (a catalog answers both the Default and the Create area).
	for (const ibValueMetaObjectSection* child : section->GetInterfaceArrayObject()) {
		ibSectionBlock block;
		block.items = child->GetInterfaceItemArrayObject();
		if (block.items.empty())
			continue;
		block.title = child->GetSynonym();
		AppendNested(child, block.items);
		page.push_back(block);
	}

	// THE ACTIONS PANEL — the platform's groups (Create, Reports, Service), then the declared ones.
	for (const ibInterfaceCommandSection area : g_platformCommandGroups) {
		if (ibCommandGroupCategoryOf(area) != ibCommandGroupCategory_Actions)
			continue;
		ibSectionBlock block;
		if (!section->GetInterfaceItemArrayObject(area, block.items))
			continue;
		block.column = ibProtocol::ibSchema::ibSections::ibColumn::Right;
		block.title = ibCommandGroupCaption(area);
		block.area = area;
		page.push_back(block);
	}
	for (const ibValueMetaObjectCommandGroup* group : declaredGroups) {
		if (group == nullptr || group->IsDeleted() || group->GetCategory() != ibCommandGroupCategory_Actions)
			continue;
		ibSectionBlock block;
		if (!section->GetInterfaceItemArrayObject(group, block.items))
			continue;
		block.column = ibProtocol::ibSchema::ibSections::ibColumn::Right;
		block.title = group->GetSynonym();
		block.group = group;
		page.push_back(block);
	}

	return page;
}

// The sections this person may use — the panel's buttons.
std::vector<const ibValueMetaObjectSection*> UsedSections(const ibMetaDataConfigurationBase* metaData)
{
	std::vector<const ibValueMetaObjectSection*> used;
	for (const ibValueMetaObjectSection* section : metaData->GetAnyArrayObject<ibValueMetaObjectSection>(g_metaSectionCLSID)) {
		if (section->AccessRight_Use())
			used.push_back(section);
	}
	return used;
}

} // namespace

void ibSchemaSections::Build(const ibSession& session, const ibDataNode& /*args*/, ibDataNode& result) const
{
	const ibMetaDataConfigurationBase* const metaData = session.GetMetaData();
	if (metaData == nullptr)
		return;

	ibDataNode& sections = result.Child(ibProtocol::ibSchema::ibSections::Sections);
	for (const ibValueMetaObjectSection* section : UsedSections(metaData)) {
		ibDataNode& node = sections.AddChild(0, section->GetMetaID());
		node.SetValue(ibProtocol::ibSchema::ibSections::Title, section->GetSynonym());
		// Its own picture, or the configuration's — as its button drew it.
		node.SetValue(ibProtocol::ibSchema::ibSections::Picture, ibBackendPicture::SendPicture(section->IsEmptyPicture()
			? ibPictureDescription(g_metaCommonMetadataCLSID) : section->GetPictureDesc(), metaData));

		ibDataNode& blocks = node.Child(ibProtocol::ibSchema::ibSections::Blocks);
		for (const ibSectionBlock& block : PageOf(section)) {
			ibDataNode& blockNode = blocks.AddChild(0, 0);
			blockNode.SetValue(ibProtocol::ibSchema::ibSections::Column, static_cast<std::int32_t>(block.column));
			if (!block.title.IsEmpty())
				blockNode.SetValue(ibProtocol::ibSchema::ibSections::Title, block.title);
			if (block.group != nullptr) {
				if (!block.group->IsEmptyPicture())
					blockNode.SetValue(ibProtocol::ibSchema::ibSections::Picture, ibBackendPicture::SendPicture(block.group->GetPictureDesc(), metaData));
				const wxString tooltip = block.group->GetToolTip();
				if (!tooltip.IsEmpty())
					blockNode.SetValue(ibProtocol::ibSchema::ibSections::Tooltip, tooltip);
			}
			for (const ibValueMetaObject* object : block.items) {
				ibDataNode& item = blockNode.AddChild(0, 0);
				item.SetValue(ibProtocol::ibSchema::ibSections::Item, static_cast<std::int32_t>(object->GetMetaID()));
				item.SetValue(ibProtocol::ibSchema::ibSections::Title, object->GetSynonym());
				item.SetValue(ibProtocol::ibSchema::ibSections::Type, static_cast<std::int32_t>(TypeOf(block.area)));
				item.SetValue(ibProtocol::ibSchema::ibSections::Icon, ibBackendPicture::SendPicture(object->GetClassType()));
			}
		}
	}
}

std::function<void()> ibSchemaSections::Command(const ibSession& session, std::int32_t command,
	const ibDataNode& args, ibProtocol::ibRefusal& refusal, wxString& error) const
{
	if (static_cast<ibSchemaSectionsCommand>(command) != ibSchemaSectionsCommand::Open) {
		refusal = ibProtocol::ibRefusal::NotFound;
		error = wxString::Format(wxT("Sections has no command %d"), command);
		return nullptr;
	}

	const ibMetaDataConfigurationBase* const metaData = session.GetMetaData();
	if (metaData == nullptr) {
		refusal = ibProtocol::ibRefusal::Failed;
		error = wxT("the session has no configuration");
		return nullptr;
	}

	const ibMetaID itemId = args.GetValue<std::int32_t>(ibProtocol::ibSchema::ibSections::Item);
	std::int32_t type = ibInterfaceCommandType_Default;
	args.GetValue(ibProtocol::ibSchema::ibSections::Type, type);

	// Only what a page offers this person, as that page offers it: the same sections, the same right, the same blocks.
	for (const ibValueMetaObjectSection* section : UsedSections(metaData)) {
		for (const ibSectionBlock& block : PageOf(section)) {
			if (static_cast<std::int32_t>(TypeOf(block.area)) != type)
				continue;
			for (const ibValueMetaObject* object : block.items) {
				if (object->GetMetaID() != itemId)
					continue;
				// As the panel's link: the object executes what its kind answers to.
				const ibBackendCommandItem* const commandItem = dynamic_cast<const ibBackendCommandItem*>(object);
				if (commandItem == nullptr)
					continue;
				const ibInterfaceCommandType commandType = TypeOf(block.area);
				return [commandItem, commandType]() { commandItem->Execute(commandType); };
			}
		}
	}

	refusal = ibProtocol::ibRefusal::NotFound;
	error = wxString::Format(wxT("Sections offer no item %d"), itemId);
	return nullptr;
}
