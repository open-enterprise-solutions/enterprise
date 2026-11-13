////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : the designer's workspace while no tab is open
////////////////////////////////////////////////////////////////////////////

#include "welcomeWindowDesigner.h"

#include "frmclient/mainFrame/settings/keybinder.h"
#include "designer-thin/mainFrame/mainFrameDesigner.h"   // the designer's command ids

ibDesignerWelcomeWindow::ibDesignerWelcomeWindow(wxWindow* parent, const wxString& fullName, const ibKeyBinder& keys)
	: ibWelcomeWindow(parent, fullName), m_keyBinder(keys)
{
}

std::vector<std::pair<wxString, wxString>> ibDesignerWelcomeWindow::GetKeys() const
{
	const std::pair<int, wxString> commands[] = {
		{ wxID_DESIGNER_DEBUG_START,                   _("Start debugging") },
		{ wxID_DESIGNER_DEBUG_START_WITHOUT_DEBUGGING, _("Start without debugging") },
		{ wxID_FIND,                                   _("Find") },
		{ wxID_FRONTEND_SYNTAX_HELPER,                 _("Syntax Helper") },
	};

	std::vector<std::pair<wxString, wxString>> keys;
	for (const auto& [id, label] : commands) {
		const ibKeyBinder::Command* const command = m_keyBinder.GetCommandForId(id);
		if (command != nullptr && !command->keys.empty())
			keys.emplace_back(label, ibKeyBinder::GetKeyBindingAsText(command->keys.front()));
	}
	return keys;
}
