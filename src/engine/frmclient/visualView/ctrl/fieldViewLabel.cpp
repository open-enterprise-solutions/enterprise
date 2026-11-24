////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : a field of a view - shown, not edited (Label)
////////////////////////////////////////////////////////////////////////////

#include "field.h"

#include <wx/control.h>   // wxControl::EscapeMnemonics - a value is text, not a label with mnemonics

#include "deskgui/univ/ctrl/staticText.h"

void ibFieldViewLabel::Create(wxWindow* parent, const ibProtocolNode& /*node*/)
{
	// ONE widget for both cases, as the desktop's caption: bound, a title and a value; unbound, the caption alone is the
	// text. Only the value sends a click, and only where it is a link.
	m_staticText = new ibStaticText(parent, wxID_ANY, wxEmptyString);
	m_staticText->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Send(ibProtocol::ibEvent::ibKind::Open); });
}

void ibFieldViewLabel::Update(const ibProtocolNode& node)
{
	const ibProtocolNode state = node.FindChild(ibProtocol::ibForm::ibState::State);

	if (state.Has(ibProtocol::ibForm::ibState::Text))
		m_staticText->SetLabel(wxControl::EscapeMnemonics(state.GetString(ibProtocol::ibForm::ibState::Text)));
	m_staticText->SetHyperlink(state.GetBool(ibProtocol::ibForm::ibState::Link));
}

ibControl* ibFieldViewLabel::GetControl() const
{
	return m_staticText;
}

// Bound, the caption is the title of a value, lined up with the fields' titles; bound to nothing — a heading, a note —
// it is the text itself, and the title column neither measures it nor widens it (ibValueStatic's rule).
void ibFieldViewLabel::ShowCaption(const ibProtocolNode& state)
{
	const wxString caption = state.GetString(ibProtocol::ibForm::ibState::Caption);
	if (state.Has(ibProtocol::ibForm::ibState::Text)) {
		m_staticText->SetTitle(caption);
	}
	else {
		m_staticText->SetTitle(wxString());
		m_staticText->SetLabel(wxControl::EscapeMnemonics(caption));
	}
}
