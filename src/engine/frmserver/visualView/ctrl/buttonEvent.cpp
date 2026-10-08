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
	// the picked leaf comes back through OnButtonPressed(member). A plain LEAF command just runs: the press walks
	// the hop path from the button's own form gate.
	std::vector<ibFrontendCommandReceiver::ibCommandSubItem> subs;
	if (!ResolveSubCommands(desc, subs))
		ExecuteValueByPath(desc);

	if (m_formOwner != nullptr)
		m_formOwner->RefreshForm();
}

bool ibValueButton::OnClientEvent(ibProtocolEvent event, const ibDataNode& args)
{
	if (event != ibProtocolEvent::Press)
		return ibValueWindow::OnClientEvent(event, args);
	if (args.FindField(wxT("Member")) != nullptr)
		OnButtonPressed(static_cast<size_t>(args.GetValue<s32>(wxT("Member"))));
	else
		OnButtonPressed();
	return true;
}

void ibValueButton::OnButtonPressed(size_t member)
{
	// Walked again rather than remembered: the group is resolved as it stands now, and a place past its end —
	// a command gone since the frame was written — runs nothing.
	std::vector<ibFrontendCommandReceiver::ibCommandSubItem> subs;
	if (!ResolveSubCommands(GetCommandDesc(), subs))
		return;
	std::vector<ibCommandDescription> leaves;
	WalkGroup(subs, leaves, nullptr, nullptr);
	if (member < leaves.size())
		ExecuteValueByPath(leaves[member]);

	if (m_formOwner != nullptr)
		m_formOwner->RefreshForm();
}
