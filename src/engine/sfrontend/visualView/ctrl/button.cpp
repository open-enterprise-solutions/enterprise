#include "widgets.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "sfrontend/visualView/ctrl/form.h"           // ibValueForm — GetOwnerForm (the command-door gate)
#include "sfrontend/visualView/layers/commandBar.h"   // GatherFormCommands — the icon/caption source the navigator uses
#include "backend/backend_picture.h"                  // ibBackendPicture::GetServerPicture — the picture as it travels


//****************************************************************************
//*                              Button                                      *
//****************************************************************************

ibValueButton::ibValueButton() : ibValueWindow()
{
}

// The command door's gate — a button hops its command path from the form it lives on.
ibValueForm* ibValueButton::GetCommandGateForm() const
{
	return GetOwnerForm();
}

void ibValueButton::OnUpdate(ibDataNode& state, ibVisualHost* host)
{
	ibValueWindow::OnUpdate(state, host);

	// The button's OWN Title / Picture win; where it sets nothing, the bound command provides the DEFAULT look —
	// pulled straight FROM the command through the command door (ResolveValueByPath), not gathered. So a plain
	// button shows its own text; a command button left blank shows the command's caption + icon.
	wxString title = m_propertyTitle->GetValueAsTranslateString();
	ibServerPicture picture = ibBackendPicture::GetServerPicture(m_propertyPicture->GetValueAsPictureDesc(), GetMetaData());
	const ibCommandDescription& cmdDesc = GetCommandDesc();
	ibValueForm* const ownerForm = GetOwnerForm();
	bool cmdModifies = false;   // only meaningful when a command is bound (used for the view-only greying below)
	bool cmdResolved = false;   // the bound command still resolves through the door — a button with no LIVE command hides
	bool cmdPicAndText = true;  // the bound command's DEFAULT display (a standard action like Close is picture-only)
	if (cmdDesc.IsOk() && ownerForm != nullptr) {
		wxString cmdCaption; ibServerPicture cmdIcon;
		// THE one resolve (ResolveCommand on the door): walk + reliable gather fallback -> existence + caption + icon
		// + modifies + default display, decided in ONE place so the button, the command bar and the inspector cell can't drift.
		cmdResolved = ResolveCommand(cmdDesc, cmdCaption, cmdIcon, &cmdModifies, nullptr, &cmdPicAndText);
		// The button's OWN Title / Picture win only when actually SET — test the PROPERTY (IsEmptyProperty), not the
		// resolved value: an unset backend-picture still yields an IsOk() bitmap, which used to mask the command icon.
		if (m_propertyTitle->IsEmptyProperty())   title = cmdCaption;
		if (m_propertyPicture->IsEmptyProperty()) picture = cmdIcon;
	}
	// The button's OWN Representation wins; left on Auto it takes the bound COMMAND's default (picture-only for
	// Close / Update, picture+text for Add / Post), and picture+text when nothing is bound. One resolved rep.
	ibRepresentation rep = m_propertyRepresentation->GetValueAsEnum();
	if (rep == ibRepresentation::ibRepresentation_Auto)
		rep = (cmdResolved && !cmdPicAndText) ? ibRepresentation::ibRepresentation_Picture
		                                      : ibRepresentation::ibRepresentation_PictureAndText;

	// The caption is always written — it names the button even when only the picture is shown; the
	// representation says which of the two are shown. The picture travels as a base64 PNG, and only
	// when it is shown at all.
	state.SetValue(wxT("Caption"), title);
	state.SetValue(wxT("Representation"), static_cast<s32>(rep));
	if (rep != ibRepresentation::ibRepresentation_Text && picture.IsOk())
		state.SetValue(wxT("Picture"), wxString(picture.GetData()));

	// A button carries ONLY a command — with none bound, OR the bound command DELETED (its path no longer resolves
	// through the door), it has nothing to do, so it is NOT shown. The orphaned control still lives in the object
	// tree (select it there to rebind or remove); mirrors an unbound command bar item. The button ASKS the door and
	// hides itself on a dead path — same rule whether the binding is empty or points at a gone command.
	// Both answers below overwrite the window's own (SetField replaces; SetValue would write the key twice).
	if (!cmdResolved)
		state.SetField(wxT("Visible"), ibDataValue::Bool(false));
	// A command button OBEYS the command's "modifies data" flag: a view-only form greys a data-changing command
	// (controls aren't disabled in view-only, but command projections are — the same rule as the command bar).
	else if (cmdModifies && ownerForm != nullptr && ownerForm->IsViewOnly())
		state.SetField(wxT("Enabled"), ibDataValue::Bool(false));

	// A GROUP command opens its sub-commands (groups nest) — the client draws the submenu from these.
	std::vector<ibFrontendCommandReceiver::ibCommandSubItem> subs;
	if (cmdResolved && ResolveSubCommands(cmdDesc, subs)) {
		std::vector<ibCommandDescription> leaves;
		WalkGroup(subs, leaves, &state.Child(wxT("Members")));
	}
}

void ibValueButton::WalkGroup(const std::vector<ibFrontendCommandReceiver::ibCommandSubItem>& subs,
	std::vector<ibCommandDescription>& leaves, ibDataNode* node) const
{
	for (const ibFrontendCommandReceiver::ibCommandSubItem& sub : subs) {
		ibDataNode* member = node != nullptr ? &node->AddChild(0, 0) : nullptr;
		if (member != nullptr) {
			member->SetValue(wxT("Caption"), sub.caption);
			if (sub.icon.IsOk())
				member->SetValue(wxT("Picture"), wxString(sub.icon.GetData()));
		}
		std::vector<ibFrontendCommandReceiver::ibCommandSubItem> nested;
		if (ResolveSubCommands(sub.desc, nested)) {   // a GROUP -> its own submenu
			WalkGroup(nested, leaves, member);
			continue;
		}
		if (member != nullptr)                        // a LEAF -> a place the press names
			member->SetValue(wxT("Member"), static_cast<s32>(leaves.size()));
		leaves.push_back(sub.desc);
	}
}

//*******************************************************************
//*                           Data									*
//*******************************************************************

bool ibValueButton::ReadData(const ibDataNode& node)
{
	m_propertyTitle->SetNodeValue(node.GetProperty(m_propertyTitle->GetName()));
	m_propertyRepresentation->SetNodeValue(node.GetProperty(m_propertyRepresentation->GetName()));
	m_propertyPicture->SetNodeValue(node.GetProperty(m_propertyPicture->GetName()));
	m_propertyCommand->SetNodeValue(node.GetProperty(m_propertyCommand->GetName()));   // the button's ONLY binding (hop path)

	return ibValueWindow::ReadData(node);
}

bool ibValueButton::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyTitle->GetName(), m_propertyTitle->GetNodeValue());
	node.SetProperty(m_propertyRepresentation->GetName(), m_propertyRepresentation->GetNodeValue());
	node.SetProperty(m_propertyPicture->GetName(), m_propertyPicture->GetNodeValue());
	node.SetProperty(m_propertyCommand->GetName(), m_propertyCommand->GetNodeValue());   // the button's ONLY binding (hop path)

	return ibValueWindow::WriteData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueButton, "Button", "Widget", control_to_clsid("CT_BUTN"));
