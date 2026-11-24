////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : a static of a view - its caption (Text)
////////////////////////////////////////////////////////////////////////////

#include "static.h"

#include <wx/control.h>   // wxControl::EscapeMnemonics - a caption is text, not a label with mnemonics

#include "deskgui/univ/ctrl/staticText.h"

void ibStaticViewText::Create(wxWindow* parent, const ibProtocolNode& /*node*/)
{
	m_staticText = new ibStaticText(parent, wxID_ANY, wxEmptyString);
}

void ibStaticViewText::Update(const ibProtocolNode& node)
{
	const ibProtocolNode state = node.FindChild(ibProtocol::ibForm::ibState::State);

	// The caption is the text itself — a heading, a note — so the label column neither measures it nor widens it: a
	// long note above a group of fields used to push every field's title out to the note's width.
	m_staticText->SetLabel(wxControl::EscapeMnemonics(state.GetString(ibProtocol::ibForm::ibState::Caption)));
}

wxWindow* ibStaticViewText::GetWindow() const
{
	return m_staticText;
}
