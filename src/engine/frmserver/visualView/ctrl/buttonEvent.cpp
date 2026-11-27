#include "widgets.h"
#include "form.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode — an event's arguments

//*******************************************************************
//*                             Events                              *
//*******************************************************************
void ibValueButton::OnButtonPressed()
{
	// A button carries ONLY a command now (its user event is retired) — and the button IS-A command door.
	const ibCommandDescription desc = GetCommandDesc();

	// HUB — a GROUP command is not run by the press: its sub-commands (groups nest) are the user's to pick, and
	// the picked leaf comes back through OnButtonPressed(id). A plain LEAF command just runs: the press walks
	// the hop path from the button's own form gate.
	std::vector<ibFrontendCommandReceiver::ibCommandSubItem> subs;
	if (!ResolveSubCommands(desc, subs))
		ExecuteValueByPath(desc);

	if (m_formOwner != nullptr)
		m_formOwner->RefreshForm();
}

bool ibValueButton::OnClientEvent(ibProtocol::ibEvent::ibKind event, const ibDataNode& args)
{
	if (event == ibProtocol::ibEvent::ibKind::Press)
		OnButtonPressed();
	else if (event == ibProtocol::ibEvent::ibKind::Command && args.Has(ibProtocol::ibEvent::Id))
		OnButtonPressed(static_cast<size_t>(args.GetValue<std::int32_t>(ibProtocol::ibEvent::Id)));
	else
		return ibValueWindow::OnClientEvent(event, args);
	return true;
}

void ibValueButton::OnButtonPressed(size_t id)
{
	// Walked again rather than remembered: the group is resolved as it stands now, and an id past its leaves —
	// a command gone since the frame was written — runs nothing.
	std::vector<ibFrontendCommandReceiver::ibCommandSubItem> subs;
	if (!ResolveSubCommands(GetCommandDesc(), subs))
		return;
	std::vector<ibCommandDescription> leaves;
	WalkGroup(subs, leaves, nullptr);
	if (id < leaves.size())
		ExecuteValueByPath(leaves[id]);

	if (m_formOwner != nullptr)
		m_formOwner->RefreshForm();
}
