#include "sections.h"

#include "backend/backend_picture.h"                          // the pictures, as they travel
#include "backend/interfaceHelper.h"                          // ibInterfaceCommandSection, ibInterfaceCommandType
#include "backend/metaCollection/metaCommandGroupObject.h"    // the platform's groups + the declared ones, in order
#include "backend/metaCollection/metaFormObject.h"            // ibBackendCommandItem — what a link executes
#include "backend/metaCollection/metaSectionObject.h"
#include "backend/metadataConfiguration.h"
#include "core/serialize/dataBuilder.h"
#include "backend/session/session.h"                          // the configuration the session works in

#include <vector>

namespace {

// One block of a section's page — a heading (or none) and the items under it, in one of the panel's two columns.
struct ibSectionBlock {
	s32                                 column = 1;   // 1 — the navigation's, on the left; 2 — the actions', on the right
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
		block.column = 2;
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
		block.column = 2;
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

void SetPicture(ibDataNode& node, const wxString& name, const ibServerPicture& picture)
{
	if (picture.IsOk())
		node.SetValue(name, wxString(picture.GetData()));
}

} // namespace

void ibSchemaSections::Build(const ibSession& session, ibDataNode& result) const
{
	const ibMetaDataConfigurationBase* const metaData = session.GetMetaData();
	if (metaData == nullptr)
		return;

	ibDataNode& sections = result.Child(wxT("Sections"));
	for (const ibValueMetaObjectSection* section : UsedSections(metaData)) {
		ibDataNode& node = sections.AddChild(0, section->GetMetaID());
		node.SetValue(wxT("Title"), section->GetSynonym());
		// Its own picture, or the configuration's — as its button drew it.
		SetPicture(node, wxT("Picture"), section->IsEmptyPicture()
			? ibBackendPicture::GetServerPicture(g_metaCommonMetadataCLSID)
			: ibBackendPicture::GetServerPicture(section->GetPictureDesc(), metaData));

		ibDataNode& blocks = node.Child(wxT("Blocks"));
		for (const ibSectionBlock& block : PageOf(section)) {
			ibDataNode& blockNode = blocks.AddChild(0, 0);
			blockNode.SetValue(wxT("Column"), block.column);
			if (!block.title.IsEmpty())
				blockNode.SetValue(wxT("Title"), block.title);
			if (block.group != nullptr) {
				if (!block.group->IsEmptyPicture())
					SetPicture(blockNode, wxT("Picture"), ibBackendPicture::GetServerPicture(block.group->GetPictureDesc(), metaData));
				const wxString tooltip = block.group->GetToolTip();
				if (!tooltip.IsEmpty())
					blockNode.SetValue(wxT("Tooltip"), tooltip);
			}
			for (const ibValueMetaObject* object : block.items) {
				ibDataNode& item = blockNode.AddChild(0, 0);
				item.SetValue(wxT("Item"), static_cast<s32>(object->GetMetaID()));
				item.SetValue(wxT("Title"), object->GetSynonym());
				item.SetValue(wxT("Type"), static_cast<s32>(TypeOf(block.area)));
				SetPicture(item, wxT("Icon"), ibBackendPicture::GetServerPicture(object->GetClassType()));
			}
		}
	}
}

std::function<void()> ibSchemaSections::Command(const ibSession& session, s32 command,
	const ibDataNode& args, ibProtocolRefusal& refusal, wxString& error) const
{
	if (static_cast<ibSchemaSectionsCommand>(command) != ibSchemaSectionsCommand::Open) {
		refusal = ibProtocolRefusal::NotFound;
		error = wxString::Format(wxT("Sections has no command %d"), command);
		return nullptr;
	}

	const ibMetaDataConfigurationBase* const metaData = session.GetMetaData();
	if (metaData == nullptr) {
		refusal = ibProtocolRefusal::Failed;
		error = wxT("the session has no configuration");
		return nullptr;
	}

	const ibMetaID itemId = args.GetValue<s32>(wxT("Item"));
	s32 type = ibInterfaceCommandType_Default;
	args.GetValue(wxT("Type"), type);

	// Only what a page offers this person, as that page offers it: the same sections, the same right, the same blocks.
	for (const ibValueMetaObjectSection* section : UsedSections(metaData)) {
		for (const ibSectionBlock& block : PageOf(section)) {
			if (static_cast<s32>(TypeOf(block.area)) != type)
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

	refusal = ibProtocolRefusal::NotFound;
	error = wxString::Format(wxT("Sections offer no item %d"), itemId);
	return nullptr;
}
