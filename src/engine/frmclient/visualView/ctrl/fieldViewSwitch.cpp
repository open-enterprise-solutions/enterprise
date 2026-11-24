////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : a field of a view - one of its items picked (Switch)
////////////////////////////////////////////////////////////////////////////

#include "field.h"

#include <algorithm>      // std::find - which button was picked
#include <wx/control.h>   // wxControl::EscapeMnemonics - an item's text is text, not a label with mnemonics

#include "deskgui/univ/ctrl/radioButton.h"

namespace {

// THE SWITCH'S WIDGET — a control: the field's title column beside it, its buttons inside, laid out by its sizer. Its
// best size is theirs, the column added as any control's (ibWindow::GetWindowBorderSize).
class ibSwitchBox : public ibControl {
public:

	explicit ibSwitchBox(wxWindow* parent) : ibControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE) {}

	// THE FIELD'S LOOK IS ITS ITEMS' — the font and the colours the field sets on its widget (ibValueFrame::ApplyLook)
	// reach the buttons too, the ones this Update made among them: the look is set after the view's Update.
	virtual bool SetFont(const wxFont& font) override
	{
		const bool changed = ibControl::SetFont(font);
		for (wxWindow* const button : GetChildren())
			button->SetFont(font);
		return changed;
	}

	virtual bool SetForegroundColour(const wxColour& colour) override
	{
		const bool changed = ibControl::SetForegroundColour(colour);
		for (wxWindow* const button : GetChildren())
			button->SetForegroundColour(colour);
		return changed;
	}

	virtual bool SetBackgroundColour(const wxColour& colour) override
	{
		const bool changed = ibControl::SetBackgroundColour(colour);
		for (wxWindow* const button : GetChildren())
			button->SetBackgroundColour(colour);
		return changed;
	}

protected:

	virtual wxSize DoGetBestClientSize() const override
	{
		wxSizer* const sizer = GetSizer();   // its GetMinSize measures, so is not const
		return sizer != nullptr ? sizer->GetMinSize() : wxDefaultSize;
	}
};

}

void ibFieldViewSwitch::Create(wxWindow* parent, const ibProtocolNode& node)
{
	m_box = new ibSwitchBox(parent);
	m_sizer = new wxBoxSizer(static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::Orient, wxHORIZONTAL)));
	m_box->SetSizer(m_sizer);

	// A button's pick comes up to its box. One that is not editable never says it was picked — its press changes nothing.
	m_box->Bind(wxEVT_RADIOBUTTON, &ibFieldViewSwitch::OnRadioButton, this);
}

void ibFieldViewSwitch::Update(const ibProtocolNode& node)
{
	const ibProtocolNode state = node.FindChild(ibProtocol::ibForm::ibState::State);
	const std::vector<ibProtocolNode> items = state.FindChild(ibProtocol::ibForm::ibState::Choices).Children();

	// A button an item, as many as the State gives: the surplus of a shorter list gone, the missing made — the first
	// of them starts the group the others join.
	while (m_buttons.size() > items.size()) {
		m_buttons.back()->Destroy();
		m_buttons.pop_back();
	}
	while (m_buttons.size() < items.size()) {
		ibRadioButton* const button = new ibRadioButton(m_box, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
			m_buttons.empty() ? wxRB_GROUP : 0);
		m_sizer->Add(button, wxSizerFlags().Border(wxRIGHT, m_box->FromDIP(8)));
		m_buttons.push_back(button);
	}

	// Each item's text, and the one the value stands on picked — none, where the value is none of them. Read-only, the
	// server takes no pick: the buttons take no press.
	const bool editable = !state.GetBool(ibProtocol::ibForm::ibState::ReadOnly);
	for (size_t place = 0; place < items.size(); place++) {
		ibRadioButton* const button = m_buttons[place];
		const wxString caption = wxControl::EscapeMnemonics(items[place].GetString(ibProtocol::ibForm::ibState::Caption));
		if (button->GetLabel() != caption)
			button->SetLabel(caption);
		button->SetValue(items[place].GetBool(ibProtocol::ibForm::ibState::Selected));
		button->SetEditable(editable);
	}

	m_sizer->SetOrientation(static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::Orient, wxHORIZONTAL)));
	m_box->InvalidateBestSize();
	m_box->Layout();
}

ibControl* ibFieldViewSwitch::GetControl() const
{
	return m_box;
}

//*******************************************************************
//*                             Events                              *
//*******************************************************************

void ibFieldViewSwitch::OnRadioButton(wxCommandEvent& event)
{
	// The item by its place among the State's Choices — the server writes its value into the field.
	const auto found = std::find(m_buttons.begin(), m_buttons.end(), event.GetEventObject());
	if (found != m_buttons.end()) {
		ibProtocolNode args;
		args.SetValue(ibProtocol::ibEvent::Member, static_cast<long long>(found - m_buttons.begin()));
		Send(ibProtocol::ibEvent::ibKind::Change, args);
	}
}
