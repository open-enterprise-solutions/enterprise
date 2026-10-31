////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : a button of a view
////////////////////////////////////////////////////////////////////////////

#include "widgets.h"

#include <functional>

#include "frmclient/win/picture.h"
#include "frmclient/win/ctrls/controlButton.h"

namespace {

// The ids a group's popup gives its commands — each the command's place among the group's, as the press names it.
constexpr int kFirstMemberId = wxID_HIGHEST + 1;

} // namespace

void ibValueButton::Create(wxWindow* parent, const ibProtocolNode& /*node*/)
{
	m_button = new ibControlButton(parent, wxID_ANY);
	m_button->Bind(wxEVT_BUTTON, &ibValueButton::OnButtonPressed, this);
}

void ibValueButton::Update(const ibProtocolNode& node)
{
	const ibProtocolNode state = node.FindChild(ibProtocol::ibForm::ibState::State);

	// The server has resolved what is shown — the button's own caption and picture, or its command's — and which of
	// the two; the caption names the button even when only the picture is shown.
	const wxString caption = state.GetString(ibProtocol::ibForm::ibState::Caption);
	const wxBitmap picture = ibProtocolPicture(state, ibProtocol::ibForm::ibState::Picture);
	const ibProtocol::ibForm::ibState::ibRepresentation representation = static_cast<ibProtocol::ibForm::ibState::ibRepresentation>(
		state.GetInt(ibProtocol::ibForm::ibState::Representation, static_cast<long long>(ibProtocol::ibForm::ibState::ibRepresentation::PictureAndText)));

	if (representation == ibProtocol::ibForm::ibState::ibRepresentation::Picture) {
		m_button->SetLabel(wxEmptyString);
		m_button->SetBitmap(picture);
	}
	else if (representation == ibProtocol::ibForm::ibState::ibRepresentation::Text) {
		m_button->SetLabel(caption);
		m_button->SetBitmap(wxNullBitmap);
	}
	else {
		m_button->SetLabel(caption);
		m_button->SetBitmap(picture);
	}

	const ibProtocolNode members = state.FindChild(ibProtocol::ibForm::ibButton::Members);
	m_members = members.IsNode() ? members.Clone() : ibProtocolNode();

	// Shown and enabled as State says — hidden with no live command, greyed on a view-only form for one that modifies.
	UpdateWindow(m_button, node);
}

wxWindow* ibValueButton::GetWindow() const
{
	return m_button;
}

//*******************************************************************
//*                             Events                              *
//*******************************************************************

void ibValueButton::OnButtonPressed(wxCommandEvent& WXUNUSED(event))
{
	// A plain command is run by the press.
	if (m_members.Children().empty()) {
		Send(ibProtocol::ibEvent::ibKind::Press);
		return;
	}

	// A GROUP is picked from — its commands, groups nesting as submenus; a command is named by its place among all the
	// group's commands, in the order they are written.
	int next = kFirstMemberId;
	std::function<void(wxMenu*, const ibProtocolNode&)> fill = [&fill, &next](wxMenu* into, const ibProtocolNode& members) {
		for (const ibProtocolNode& member : members.Children()) {
			const wxString caption = member.GetString(ibProtocol::ibForm::ibButton::Caption);
			if (!member.Children().empty()) {
				wxMenu* const submenu = new wxMenu();
				fill(submenu, member);
				into->AppendSubMenu(submenu, caption);
				continue;
			}
			wxMenuItem* const item = new wxMenuItem(into, next++, caption);
			const wxBitmap picture = ibProtocolPicture(member, ibProtocol::ibForm::ibButton::Picture);
			if (picture.IsOk())
				item->SetBitmap(picture);
			into->Append(item);
		}
	};

	wxMenu menu;
	fill(&menu, m_members);
	const int chosen = m_button->GetPopupMenuSelectionFromUser(menu, wxPoint(0, m_button->GetSize().GetHeight()));
	if (chosen == wxID_NONE)
		return;

	ibProtocolNode args;
	args.SetValue(ibProtocol::ibEvent::Member, chosen - kFirstMemberId);
	Send(ibProtocol::ibEvent::ibKind::Press, args);
}

