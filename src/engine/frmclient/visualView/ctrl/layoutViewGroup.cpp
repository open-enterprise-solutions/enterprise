////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : the layout of a view - a row or a column in a frame with a caption (Group)
////////////////////////////////////////////////////////////////////////////

#include "layout.h"

#include "deskgui/typeconv.h"

void ibLayoutViewGroup::Create(wxWindow* parent, const ibProtocolNode& node)
{
	const ibProtocolNode state = node.FindChild(ibProtocol::ibForm::ibState::State);
	ibStaticBox* const staticBox = new ibStaticBox(parent, wxID_ANY, state.GetString(ibProtocol::ibForm::ibState::Title));
	m_group = new ibStaticBoxSizer(staticBox, static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::Orient, wxVERTICAL)));
}

void ibLayoutViewGroup::Update(const ibProtocolNode& node)
{
	m_group->SetOrientation(static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::Orient, m_group->GetOrientation())));

	// The box is the group's window: its caption, its look, whether it is enabled and shown — the State's, which the
	// server reads with the group's title in the person's language.
	const ibProtocolNode state = node.FindChild(ibProtocol::ibForm::ibState::State);
	const wxSize minSize = typeConv::StringToSize(node.GetString(ibProtocol::ibForm::ibProperty::MinimumSize));
	ibStaticBox* const staticBox = m_group->GetStaticBox();
	staticBox->SetLabel(state.GetString(ibProtocol::ibForm::ibState::Title));
	staticBox->SetMinSize(minSize);
	ApplyLook(staticBox, node);
	staticBox->Enable(state.GetBool(ibProtocol::ibForm::ibState::Enabled, true));
	staticBox->Show(state.GetBool(ibProtocol::ibForm::ibState::Visible, true));
	staticBox->SetToolTip(node.GetString(ibProtocol::ibForm::ibProperty::Tooltip));

	if (minSize != wxDefaultSize)
		staticBox->Layout();
}

wxSizer* ibLayoutViewGroup::GetSizer() const
{
	return m_group;
}

wxWindow* ibLayoutViewGroup::GetChildParent() const
{
	return m_group->GetStaticBox();
}
