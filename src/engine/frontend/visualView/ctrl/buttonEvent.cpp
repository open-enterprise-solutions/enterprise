#include "widgets.h"
#include "form.h"
#ifndef OES_USE_WEB
#include <wx/menu.h>   // hub (group) command — dropdown of sub-commands

// Menu-id base for hub leaf items: a leaf's id is this + its index in `runnable`, so the picked id maps straight
// back to the command to run. Above wxID_HIGHEST to stay clear of framework ids.
static constexpr int kHubMenuIdBase = wxID_HIGHEST + 1;

// HUB — build a menu for `subs`; a sub-command that is ITSELF a group becomes a NESTED submenu (full recursion,
// exactly as a subsystem holds subsystems), a leaf a plain item. Each leaf gets a unique id whose offset into
// `runnable` is the command to run — so a click anywhere in the tree resolves back to one terminal path.
static wxMenu* BuildHubMenu(const ibFrontendCommandReceiver* door,
	const std::vector<ibFrontendCommandReceiver::ibCommandSubItem>& subs, std::vector<ibCommandDescription>& runnable)
{
	wxMenu* menu = new wxMenu();
	for (const ibFrontendCommandReceiver::ibCommandSubItem& sub : subs) {
		std::vector<ibFrontendCommandReceiver::ibCommandSubItem> nested;
		wxMenuItem* mi = nullptr;
		if (door->ResolveSubCommands(sub.desc, nested))   // sub is a GROUP -> recurse into a submenu
			mi = menu->AppendSubMenu(BuildHubMenu(door, nested, runnable), sub.caption);
		else {                                            // sub is a LEAF -> a runnable item
			mi = menu->Append(kHubMenuIdBase + static_cast<int>(runnable.size()), sub.caption);
			runnable.push_back(sub.desc);
		}
		if (sub.icon.IsOk())
			mi->SetBitmap(sub.icon);
	}
	return menu;
}
#endif

//*******************************************************************
//*                             Events                              *
//*******************************************************************
void ibValueButton::OnButtonPressed(wxCommandEvent& event)
{
	// A button carries ONLY a command now (its user event is retired) — and the button IS-A command door.
	const ibCommandDescription desc = GetCommandDesc();

	// HUB — a GROUP command pops its sub-commands as a dropdown (groups nest as submenus); the chosen leaf runs.
	// A plain LEAF command just runs: the press walks the hop path from the button's own form gate.
	std::vector<ibFrontendCommandReceiver::ibCommandSubItem> subs;
	if (ResolveSubCommands(desc, subs)) {
#ifndef OES_USE_WEB
		std::vector<ibCommandDescription> runnable;
		wxMenu* const menu = BuildHubMenu(this, subs, runnable);
		wxWindow* const anchor = wxDynamicCast(event.GetEventObject(), wxWindow);
		const int sel = anchor != nullptr ? anchor->GetPopupMenuSelectionFromUser(*menu) : wxID_NONE;
		if (sel != wxID_NONE) {
			const size_t idx = static_cast<size_t>(sel - kHubMenuIdBase);
			if (idx < runnable.size())
				ExecuteValueByPath(runnable[idx]);
		}
		delete menu;   // owns its submenus — the whole tree goes with it
#endif
	}
	else
		ExecuteValueByPath(desc);

	if (m_formOwner != nullptr)
		m_formOwner->RefreshForm();
}
