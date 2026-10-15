////////////////////////////////////////////////////////////////////////////
//	Description : the form editor's command tree — the desktop's gather (designer/win/editor/visualEditor/
//	              visualEditorCommandTree.cpp, RebuildTree) on the server's form: its rows drawn beside the form, and the
//	              tree's doors (FormCommand)
////////////////////////////////////////////////////////////////////////////

#include "docViewFormEdit.h"

#include "core/serialize/dataBuilder.h"
#include "frmserver/client/clientFrame.h"               // ibClientFrame::SendPicture — a command's picture, by its id
#include "frmserver/visualView/ctrl/form.h"
#include "frmserver/visualView/ctrl/formCommand.h"
#include "frmserver/visualView/layers/commandBar.h"     // GetCommandSections / GatherFormCommands — the shared gather
#include "protocol/protocol.h"

#include <map>

///////////////////////////////////////////////////////////////////////////////
// The rows
///////////////////////////////////////////////////////////////////////////////

void ibFormDesignView::FillCommandTree(ibDataNode& node)
{
	// ALL three sections first (form / standard / global), in order, so they show even when EMPTY — GatherFormCommands
	// labels its entries with exactly these strings. Rows are kept by their PLACE: a node's children move as it grows.
	std::map<wxString, size_t> sections;
	for (const wxString& section : GetCommandSections()) {
		sections[section] = node.Children().size();
		node.AddChild(0, 0).SetValue(ibProtocolName::Name, section);
	}
	std::map<wxString, size_t> subgroups;   // "section\x1fsubgroup" -> its place in the section (Standard nests per source)

	for (const ibCommandSourceEntry& entry : GatherFormCommands(m_valueForm)) {
		auto section = sections.find(entry.group);
		if (section == sections.end()) {   // defensive: an unexpected section -> appended at the end
			section = sections.emplace(entry.group, node.Children().size()).first;
			node.AddChild(0, 0).SetValue(ibProtocolName::Name, entry.group);
		}
		ibDataNode* parent = &node.Children()[section->second];
		if (!entry.subgroup.IsEmpty()) {   // nested under a per-source SUB-group (the form / a table)
			const wxString key = entry.group + wxT("\x1f") + entry.subgroup;
			auto sub = subgroups.find(key);
			if (sub == subgroups.end()) {
				sub = subgroups.emplace(key, parent->Children().size()).first;
				parent->AddChild(0, 0).SetValue(ibProtocolName::Name, entry.subgroup);
			}
			parent = &parent->Children()[sub->second];
		}

		// A form command's row is known by its id — the inspector's rows of it are; any other's, by its path only.
		const ibFormCommandValue* const formCommand = entry.desc.IsOk() && !entry.desc.IsHop()
			? m_valueForm->FindFormCommandById(entry.desc.GetLeaf()) : nullptr;
		ibDataNode& row = parent->AddChild(0, formCommand != nullptr ? formCommand->GetId() : 0);
		row.SetValue(ibProtocolName::Name, entry.label);
		const ibString picture = ibClientFrame::SendPicture(entry.picture);   // the command's own picture, else the tree's default
		if (!picture.IsEmpty())
			row.SetValue(ibProtocolName::Picture, picture);

		std::vector<ibDataValue> hops;
		for (const ibCommandHop& hop : entry.desc.GetPath()) {
			std::shared_ptr<ibDataNode> hopNode = std::make_shared<ibDataNode>();
			hopNode->SetValue(ibProtocolName::Id, static_cast<s32>(hop.m_id));
			hops.push_back(ibDataValue::Child(hopNode));
		}
		row.AddField(ibProtocolName::CommandPath, ibDataValue::Array(std::move(hops)));
		row.SetValue(ibProtocolName::Kind, static_cast<s32>(entry.desc.GetCommandType()));
	}
}

void ibFormDesignView::ReadPath(const ibDataNode& node, ibCommandDescription& desc)
{
	desc.ClearCommand();
	const ibDataValue* const hops = node.FindField(ibProtocolName::CommandPath);
	if (hops == nullptr || hops->Kind() != ibDataKind::Array)
		return;
	for (const ibDataValue& hop : hops->AsArray()) {
		if (hop.Kind() == ibDataKind::Child && hop.AsChild() != nullptr)
			desc.AppendCommand(static_cast<ibMetaID>(hop.AsChild()->GetValue<s32>(ibProtocolName::Id)));
	}
	const s32 kind = node.GetValue<s32>(ibProtocolName::Kind);
	desc.SetCommandType(kind != 0 ? static_cast<ibInterfaceCommandType>(kind) : ibInterfaceCommandType_Default);
}

///////////////////////////////////////////////////////////////////////////////
// The doors
///////////////////////////////////////////////////////////////////////////////

bool ibFormDesignView::FetchFormCommand(const ibDataNode& request, ibDataNode& response)
{
	// The desktop's tree's handlers, on the form the client holds. A form command is a form-LOCAL event (NOT a
	// metaobject — nothing goes into the metadata tree).
	const ibMetaID id = request.GetValue<s32>(ibProtocolName::FormCommand);
	ibFormCommandValue* const cmd = id != 0 ? m_valueForm->FindFormCommandById(id) : nullptr;
	if (id != 0 && cmd == nullptr)
		return false;

	ibFormCommandValue* shown = cmd;
	switch (static_cast<ibProtocolFormDesignerCommand>(request.GetValue<s32>(ibProtocolName::Command))) {
	case ibProtocolFormDesignerCommand::Create:
		// Uniquely named, its Action EMPTY — the handler procedure does not exist yet; the developer creates it later
		// through the event editor (the desktop's AddCommand).
		shown = m_valueForm->AddFormCommand(m_valueForm->MakeUniqueFormCommandName());
		break;
	case ibProtocolFormDesignerCommand::Paste: {
		// The node the client copied of its form's FormCommands — the properties, the Action event, the form-unique id;
		// a fresh entry of it, a unique name and id.
		const ibDataNode* const copied = request.FindChild(ibProtocolName::Value);
		if (copied == nullptr)
			return false;
		ibDataNode node = *copied;
		RestoreProperties(node);
		shown = m_valueForm->PasteFormCommand(node);
		break;
	}
	case ibProtocolFormDesignerCommand::Remove:
		// A button bound to it now resolves to NOTHING — the button asks the command door on its own refresh and, getting
		// no command back, breaks its own look. No walk-and-clear from here: the projection self-validates.
		if (cmd == nullptr)
			return false;
		m_valueForm->DeleteFormCommand(cmd->GetName());
		shown = nullptr;
		break;
	case ibProtocolFormDesignerCommand::Set:
		// …the command asked first, as the inspector's door asks it — a name another command has is refused; a button
		// bound to it takes its new name, caption, picture as the form is drawn again.
		if (!SetProperty(cmd, request))
			return false;
		break;
	default:
		return false;
	}

	WriteForm(response);
	if (shown != nullptr)
		response.SetValue(ibProtocolName::FormCommand, static_cast<s32>(shown->GetId()));
	return true;
}
