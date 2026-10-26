////////////////////////////////////////////////////////////////////////////
//	Description : the form editor's command tree — the desktop's gather (designer/win/editor/visualEditor/
//	              visualEditorCommandTree.cpp, RebuildTree) on the server's form: its rows drawn beside the form, and the
//	              tree's doors (FormCommand)
////////////////////////////////////////////////////////////////////////////

#include "docViewFormEdit.h"

#include "core/serialize/dataBuilder.h"
#include "backend/backend_picture.h"                    // ibBackendPicture::SendPicture — a command's picture, by its id
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
	// The gather the inspector's command-source row carries as well (WriteCommandTree, commandBar.cpp).
	WriteCommandTree(node, m_valueForm);
}

void ibFormDesignView::ReadPath(const ibDataNode& node, ibCommandDescription& desc)
{
	desc.ClearCommand();
	const auto hops = node.FindField(ibProtocol::ibView::ibFormDesigner::CommandPath);
	if (!hops || hops->Kind() != ibDataKind::Array)
		return;
	for (const ibDataValue& hop : hops->AsArray()) {
		if (hop.Kind() == ibDataKind::Child)
			desc.AppendCommand(static_cast<ibMetaID>(hop.AsChild()->GetValue<std::int32_t>(ibProtocol::ibView::ibFormDesigner::Id)));
	}
	const std::int32_t kind = node.GetValue<std::int32_t>(ibProtocol::ibView::ibFormDesigner::Kind);
	desc.SetCommandType(kind != 0 ? static_cast<ibInterfaceCommandType>(kind) : ibInterfaceCommandType_Default);
}

///////////////////////////////////////////////////////////////////////////////
// The doors
///////////////////////////////////////////////////////////////////////////////

bool ibFormDesignView::FetchFormCommand(const ibDataNode& request, ibDataNode& response)
{
	// The desktop's tree's handlers, on the form the client holds. A form command is a form-LOCAL event (NOT a
	// metaobject — nothing goes into the metadata tree).
	const ibMetaID id = request.GetValue<std::int32_t>(ibProtocol::ibView::ibFormDesigner::FormCommand);
	ibFormCommandValue* const cmd = id != 0 ? m_valueForm->FindFormCommandById(id) : nullptr;
	if (id != 0 && cmd == nullptr)
		return false;

	ibFormCommandValue* shown = cmd;
	switch (static_cast<ibProtocol::ibView::ibFormDesigner::ibCommand>(request.GetValue<std::int32_t>(ibProtocol::ibView::ibFormDesigner::Command))) {
	case ibProtocol::ibView::ibFormDesigner::ibCommand::Create:
		// Uniquely named, its Action EMPTY — the handler procedure does not exist yet; the developer creates it later
		// through the event editor (the desktop's AddCommand).
		shown = m_valueForm->AddFormCommand(m_valueForm->MakeUniqueFormCommandName());
		break;
	case ibProtocol::ibView::ibFormDesigner::ibCommand::Paste: {
		// The node the client copied of its form's FormCommands — the properties, the Action event, the form-unique id;
		// a fresh entry of it, a unique name and id.
		const auto copied = request.FindChild(ibProtocol::ibView::ibFormDesigner::Value);
		if (!copied)
			return false;
		ibDataNode node = *copied;
		shown = m_valueForm->PasteFormCommand(node);
		break;
	}
	case ibProtocol::ibView::ibFormDesigner::ibCommand::Remove:
		// A button bound to it now resolves to NOTHING — the button asks the command door on its own refresh and, getting
		// no command back, breaks its own look. No walk-and-clear from here: the projection self-validates.
		if (cmd == nullptr)
			return false;
		m_valueForm->DeleteFormCommand(cmd->GetName());
		shown = nullptr;
		break;
	case ibProtocol::ibView::ibFormDesigner::ibCommand::Set:
		// …the command asked first, as the inspector's door asks it — a name another command has is refused; a button
		// bound to it takes its new name, caption, picture as the form is drawn again.
		if (!SetProperty(cmd, request))
			return false;
		break;
	default:
		return false;
	}

	// A command is what the buttons and bars bound to it show — their captions, their pictures: the form drawn whole.
	UpdateControl(m_valueForm);
	WriteForm(response);
	if (shown != nullptr)
		response.SetValue(ibProtocol::ibView::ibFormDesigner::FormCommand, static_cast<std::int32_t>(shown->GetId()));
	return true;
}
