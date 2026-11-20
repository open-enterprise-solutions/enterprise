////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : a field of a view drawn as radio buttons
////////////////////////////////////////////////////////////////////////////

#include "widgets.h"

#include <wx/control.h>   // wxControl::EscapeMnemonics - a caption is text, not a label with mnemonics

#include "deskgui/univ/ctrl/radioButton.h"
#include "deskgui/univ/ctrl/staticText.h"

void ibValueRadioButtons::Create(wxWindow* parent, const ibProtocolNode& /*node*/)
{
	m_panel = new ibPanel(parent, wxID_ANY);
	wxBoxSizer* const sizer = new wxBoxSizer(wxHORIZONTAL);
	m_caption = new ibStaticText(m_panel, wxID_ANY, wxEmptyString);
	sizer->Add(m_caption, 0, wxALIGN_TOP | wxRIGHT, 8);
	m_buttonSizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(m_buttonSizer, 1, wxEXPAND);
	m_panel->SetSizer(sizer);
}

void ibValueRadioButtons::Update(const ibProtocolNode& node)
{
	const ibProtocolNode state = node.FindChild(ibProtocol::ibForm::ibState::State);

	const wxString caption = state.GetString(ibProtocol::ibForm::ibState::Caption);
	m_caption->SetLabel(wxControl::EscapeMnemonics(caption));
	m_caption->Show(!caption.IsEmpty());

	// The buttons — built anew only when the choices are others.
	std::vector<wxString> captions;
	for (const ibProtocolNode& choice : state.FindChild(ibProtocol::ibForm::ibField::Choices).Children())
		captions.push_back(choice.GetString(ibProtocol::ibForm::ibField::Caption));
	if (captions != m_captions) {
		for (ibRadioButton* button : m_buttons)
			button->Destroy();
		m_buttons.clear();
		m_buttonSizer->Clear();
		for (size_t idx = 0; idx < captions.size(); ++idx) {
			// Each one alone (wxRB_SINGLE): which one is checked is the server's, not a group's.
			ibRadioButton* const button = new ibRadioButton(m_panel, wxID_ANY, wxControl::EscapeMnemonics(captions[idx]),
				wxDefaultPosition, wxDefaultSize, wxRB_SINGLE);
			button->Bind(wxEVT_RADIOBUTTON, [this, idx](wxCommandEvent&) { OnButtonClicked(idx); });
			m_buttonSizer->Add(button, 0, wxBOTTOM, 2);
			m_buttons.push_back(button);
		}
		m_captions = std::move(captions);
	}

	m_selected = state.Has(ibProtocol::ibForm::ibField::Selected)
		? static_cast<int>(state.GetInt(ibProtocol::ibForm::ibField::Selected)) : -1;
	m_readOnly = state.GetBool(ibProtocol::ibForm::ibState::ReadOnly);
	ShowSelected();

	UpdateWindow(m_panel, node);
	m_panel->Layout();
}

wxWindow* ibValueRadioButtons::GetWindow() const
{
	return m_panel;
}

void ibValueRadioButtons::ShowSelected()
{
	for (size_t idx = 0; idx < m_buttons.size(); ++idx)
		m_buttons[idx]->SetValue(static_cast<int>(idx) == m_selected);
}

void ibValueRadioButtons::OnButtonClicked(size_t index)
{
	// Until the server answers, the checks are what it said last: a read-only field keeps them so, and the frame after
	// the choice moves them.
	ShowSelected();
	if (m_readOnly)
		return;

	ibProtocolNode args;
	args.SetValue(ibProtocol::ibEvent::Choice, static_cast<long long>(index));
	Send(ibProtocol::ibEvent::ibKind::Change, args);
}

CONTROL_KIND_REGISTER(ibValueRadioButtons, ibProtocol::ibForm::ibType::Field, ibProtocol::ibForm::ibProperty::ibFieldKind::RadioButtons)
