#include "widgets.h"
#include "backend/compiler/procUnit.h"
#ifdef OES_USE_WEB
#include "frontend/web/webWindow.h"
#include "backend/backend_picture.h"
#else
#include "frontend/win/ctrls/controlButton.h"
#endif

wxIMPLEMENT_DYNAMIC_CLASS(ibValueButton, ibValueWindow)

//****************************************************************************
//*                              Button                                      *
//****************************************************************************

ibValueButton::ibValueButton() : ibValueWindow()
{
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
#ifdef OES_USE_WEB
	ibWebButton* button = static_cast<ibWebButton*>(wxobject);
	// Caption + representation + picture — mirrors desktop's SetBitmap
	// switch below. Auto resolves to PictureAndText for buttons (desktop's
	// "else" branch at the bottom of its switch). Picture encodes to a
	// base64 PNG data URI so the browser renders <img src=...> directly.
	button->SetLabel(m_propertyTitle->GetValueAsTranslateString());
	ibRepresentation rep = m_propertyRepresentation->GetValueAsEnum();
	if (rep == ibRepresentation::ibRepresentation_Auto)
		rep = ibRepresentation::ibRepresentation_PictureAndText;
	const wxBitmap bmp = (rep == ibRepresentation::ibRepresentation_Text)
		? wxNullBitmap
		: m_propertyPicture->GetValueAsBitmap();
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
		const auto rep = m_propertyRepresentation->GetValueAsEnum();
		if (rep == ibRepresentation::ibRepresentation_Picture) {
			button->SetLabel(wxEmptyString);
			button->SetBitmap(m_propertyPicture->GetValueAsBitmap());
		} else if (rep == ibRepresentation::ibRepresentation_Text) {
			button->SetLabel(m_propertyTitle->GetValueAsTranslateString());
			button->SetBitmap(wxNullBitmap);
		} else {
			// Auto + PictureAndText: both label and bitmap.
			button->SetLabel(m_propertyTitle->GetValueAsTranslateString());
			button->SetBitmap(m_propertyPicture->GetValueAsBitmap());
		}
	}
#endif

	// Shared enable/visible/min-max/font/colour push. UpdateWindow is
	// cross-platform: on desktop it sets the full wx set; on web it
	// just syncs Enable + Show on the ibWebWindow.
	UpdateWindow(button);
}

void ibValueButton::Cleanup(wxObject* obj, ibVisualHost* visualHost)
{
}

//*******************************************************************
//*                           Data									*
//*******************************************************************

bool ibValueButton::LoadData(ibReaderMemory& reader)
{
	m_propertyTitle->LoadData(reader);
	m_propertyRepresentation->LoadData(reader);
	m_propertyPicture->LoadData(reader);

	//events
	m_onButtonPressed->LoadData(reader);

	return ibValueWindow::LoadData(reader);
}

bool ibValueButton::SaveData(ibWriterMemory& writer)
{
	m_propertyTitle->SaveData(writer);
	m_propertyRepresentation->SaveData(writer);
	m_propertyPicture->SaveData(writer);

	//events
	m_onButtonPressed->SaveData(writer);

	return ibValueWindow::SaveData(writer);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueButton, "Button", "Widget", string_to_clsid("CT_BUTN"));