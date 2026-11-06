////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : a caption of a view
////////////////////////////////////////////////////////////////////////////

#include "widgets.h"

#include <wx/control.h>   // wxControl::EscapeMnemonics - a value is text, not a label with mnemonics

#include "gui/univ/ctrl/staticText.h"

void ibValueStaticText::Create(wxWindow* parent, const ibProtocolNode& /*node*/)
{
	// ONE widget for both cases, as on the desktop: bound, a title and a value; unbound, the caption alone is the
	// text. Only the value sends a click, and only where it is a link.
	m_staticText = new ibStaticText(parent, wxID_ANY, wxEmptyString);
	m_staticText->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Send(ibProtocol::ibEvent::ibKind::Open); });
}

void ibValueStaticText::Update(const ibProtocolNode& node)
{
	const ibProtocolNode state = node.FindChild(ibProtocol::ibForm::ibState::State);

	// Bound, the caption is the title of a value, lined up with the fields' titles; bound to nothing — a heading, a
	// note — it is the text itself, and the label column neither measures it nor widens it: a long note above a group
	// of fields used to push every field's title out to the note's width.
	const wxString caption = state.GetString(ibProtocol::ibForm::ibState::Caption);
	if (state.Has(ibProtocol::ibForm::ibState::Text)) {
		m_staticText->SetTitle(caption);
		m_staticText->SetLabel(wxControl::EscapeMnemonics(state.GetString(ibProtocol::ibForm::ibState::Text)));
	}
	else {
		m_staticText->SetTitle(wxString());
		m_staticText->SetLabel(wxControl::EscapeMnemonics(caption));
	}
	m_staticText->SetHyperlink(state.GetBool(ibProtocol::ibForm::ibState::Link));
	m_staticText->SetTitlePosition(node.GetInt(ibProtocol::ibForm::ibProperty::TitleLocation)
		== static_cast<int>(ibProtocol::ibForm::ibProperty::ibTitleLocation::Left) ? wxLEFT : wxRIGHT);

	UpdateWindow(m_staticText, node);
}

wxWindow* ibValueStaticText::GetWindow() const
{
	return m_staticText;
}

CONTROL_REGISTER(ibValueStaticText, ibProtocol::ibForm::ibType::Statictext)
