////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : a caption of a view
////////////////////////////////////////////////////////////////////////////

#include "widgets.h"

#include "frmclient/win/ctrls/controlStaticTextValue.h"

void ibValueStaticText::Create(wxWindow* parent, const ibProtocolNode& /*node*/)
{
	// ONE widget for both cases, as on the desktop: bound, a caption and a value; unbound, the value half is empty and
	// takes no room. Only the value sends a click, and only where it is a link.
	m_staticText = new ibControlStaticTextValue(parent, wxID_ANY);
	m_staticText->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Send(ibProtocol::ibEvent::ibKind::Open); });
}

void ibValueStaticText::Update(const ibProtocolNode& node)
{
	const ibProtocolNode state = node.FindChild(ibProtocol::ibForm::ibState::State);

	m_staticText->SetLabel(state.GetString(ibProtocol::ibForm::ibState::Caption));
	m_staticText->SetValueText(state.GetString(ibProtocol::ibForm::ibState::Text));
	// No value text — bound to nothing: the caption is the whole control.
	m_staticText->ShowValue(state.Has(ibProtocol::ibForm::ibState::Text));
	m_staticText->SetHyperlink(state.GetBool(ibProtocol::ibForm::ibState::Link));
	m_staticText->SetWindowStyle(node.GetInt(ibProtocol::ibForm::ibProperty::TitleLocation)
		== static_cast<int>(ibProtocol::ibForm::ibProperty::ibTitleLocation::Left) ? wxALIGN_LEFT : wxALIGN_RIGHT);

	UpdateWindow(m_staticText, node);
}

wxWindow* ibValueStaticText::GetWindow() const
{
	return m_staticText;
}

