#include "widgets.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "frontend/visualView/ctrl/form.h"           // ibValueForm — GetOwnerForm (the command-door gate)
#include "frontend/visualView/layers/commandBar.h"   // GatherFormCommands — the icon/caption source the navigator uses
#ifdef OES_USE_WEB
#include "frontend/web/webWindow.h"
#include "backend/backend_picture.h"
#else
#include "frontend/win/ctrls/controlButton.h"
#endif


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

wxObject* ibValueButton::Create(ibFrontendWindow* wxparent, ibVisualHost* visualHost)
{
	(void)visualHost;
	// Only the allocation differs between builds (different concrete
	// class, different ctor args). Everything after — the Bind + return
	// — is shared. `auto` deduces the concrete type so the rest of the
	// body stays platform-agnostic.
#ifdef OES_USE_WEB
	(void)wxparent;
	auto* button = new ibWebButton(GetControlID());
#else
	auto* button = new ibControlButton(wxparent, wxID_ANY,
		m_propertyTitle->GetValueAsTranslateString());
#endif
	button->Bind(wxEVT_BUTTON, &ibValueButton::OnButtonPressed, this);
	return button;
}

void ibValueButton::OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated)
{
}

void ibValueButton::Update(wxObject* wxobject, ibVisualHost* visualHost)
{
	(void)visualHost;
	// The button's OWN Title / Picture win; where it sets nothing, the bound command provides the DEFAULT look —
	// pulled straight FROM the command through the command door (ResolveValueByPath), not gathered. So a plain
	// button shows its own text; a command button left blank shows the command's caption + icon.
	wxString title = m_propertyTitle->GetValueAsTranslateString();
	wxBitmap picture = m_propertyPicture->GetValueAsBitmap();
	const ibCommandDescription& cmdDesc = GetCommandDesc();
	ibValueForm* const ownerForm = GetOwnerForm();
	bool cmdModifies = false;   // only meaningful when a command is bound (used for the view-only greying below)
	bool cmdResolved = false;   // the bound command still resolves through the door — a button with no LIVE command hides
	bool cmdPicAndText = true;  // the bound command's DEFAULT display (a standard action like Close is picture-only)
	if (cmdDesc.IsOk() && ownerForm != nullptr) {
		wxString cmdCaption; wxBitmap cmdIcon;
		// THE one resolve (ResolveCommand on the door): walk + reliable gather fallback -> existence + caption + icon
		// + modifies + default display, decided in ONE place so the button, the command bar and the inspector cell can't drift.
		cmdResolved = ResolveCommand(cmdDesc, cmdCaption, cmdIcon, &cmdModifies, nullptr, &cmdPicAndText);
		// The button's OWN Title / Picture win only when actually SET — test the PROPERTY (IsEmptyProperty), not the
		// resolved value: an unset backend-picture still yields an IsOk() bitmap, which used to mask the command icon.
		if (m_propertyTitle->IsEmptyProperty())   title = cmdCaption;
		if (m_propertyPicture->IsEmptyProperty()) picture = cmdIcon;
	}
	// The button's OWN Representation wins; left on Auto it takes the bound COMMAND's default (picture-only for
	// Close / Update, picture+text for Add / Post), and picture+text when nothing is bound. One resolved rep, both builds.
	ibRepresentation rep = m_propertyRepresentation->GetValueAsEnum();
	if (rep == ibRepresentation::ibRepresentation_Auto)
		rep = (cmdResolved && !cmdPicAndText) ? ibRepresentation::ibRepresentation_Picture
		                                      : ibRepresentation::ibRepresentation_PictureAndText;
#ifdef OES_USE_WEB
	ibWebButton* button = static_cast<ibWebButton*>(wxobject);
	// Caption + representation + picture — mirrors desktop's SetBitmap
	// switch below. Auto resolves to PictureAndText for buttons (desktop's
	// "else" branch at the bottom of its switch). Picture encodes to a
	// base64 PNG data URI so the browser renders <img src=...> directly.
	button->SetLabel(title);
	const wxBitmap bmp = (rep == ibRepresentation::ibRepresentation_Text)
		? wxNullBitmap
		: picture;
	const bool hasPic = bmp.IsOk();
	wxString pictureUri;
	if (hasPic) {
		const wxString b64 = ibBackendPicture::CreateBase64Image(bmp.ConvertToImage());
		if (!b64.IsEmpty())
			pictureUri = wxT("data:image/png;base64,") + b64;
	}
	button->SetRepresentation(static_cast<int>(rep));
	button->SetHasPicture(hasPic);
	button->SetPictureDataUri(pictureUri);
#else
	ibControlButton* button = static_cast<ibControlButton*>(wxobject);
	if (button != nullptr) {
		if (rep == ibRepresentation::ibRepresentation_Picture) {
			button->SetLabel(wxEmptyString);
			button->SetBitmap(picture);
		} else if (rep == ibRepresentation::ibRepresentation_Text) {
			button->SetLabel(title);
			button->SetBitmap(wxNullBitmap);
		} else {
			// Auto + PictureAndText: both label and bitmap.
			button->SetLabel(title);
			button->SetBitmap(picture);
		}
	}
#endif

	// Shared enable/visible/min-max/font/colour push. UpdateWindow is
	// cross-platform: on desktop it sets the full wx set; on web it
	// just syncs Enable + Show on the ibWebWindow.
	UpdateWindow(button);

	if (button != nullptr) {
		// A button carries ONLY a command — with none bound, OR the bound command DELETED (its path no longer resolves
		// through the door), it has nothing to do, so it is NOT shown. The orphaned control still lives in the object
		// tree (select it there to rebind or remove); mirrors an unbound command bar item. The button ASKS the door and
		// hides itself on a dead path — same rule whether the binding is empty or points at a gone command.
		if (!cmdResolved)
			button->Show(false);
		// A command button OBEYS the command's "modifies data" flag: a view-only form greys a data-changing command
		// (controls aren't disabled in view-only, but command projections are — the same rule as the command bar).
		else if (cmdModifies && ownerForm != nullptr && ownerForm->IsViewOnly())
			button->Enable(false);
	}
}

void ibValueButton::Cleanup(wxObject* obj, ibVisualHost* visualHost)
{
}

//*******************************************************************
//*                           Data									*
//*******************************************************************

bool ibValueButton::ReadData(const ibDataNode& node)
{
	m_propertyTitle->ReadNodeValue(node.GetProperty(m_propertyTitle->GetName()));
	m_propertyRepresentation->ReadNodeValue(node.GetProperty(m_propertyRepresentation->GetName()));
	m_propertyPicture->ReadNodeValue(node.GetProperty(m_propertyPicture->GetName()));
	m_propertyCommand->ReadNodeValue(node.GetProperty(m_propertyCommand->GetName()));   // the button's ONLY binding (hop path)

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
