////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : the sizers of a view
////////////////////////////////////////////////////////////////////////////

#include "sizer.h"

#include "frmclient/win/typeconv.h"

void ibViewUpdateSizer(wxSizer* sizer, const ibProtocolNode& node)
{
	if (sizer == nullptr)
		return;

	const wxSize minSize = typeConv::StringToSize(node.GetString(ibProtocol::ibForm::ibProperty::MinimumSize));
	if (minSize != wxDefaultSize) {
		sizer->SetMinSize(minSize);
		sizer->Layout();
	}
}

//*******************************************************************
//*                             BoxSizer                            *
//*******************************************************************

void ibValueBoxSizer::Create(wxWindow* /*parent*/, const ibProtocolNode& node)
{
	m_sizer = new wxBoxSizer(static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::Orient, wxVERTICAL)));
}

void ibValueBoxSizer::Update(const ibProtocolNode& node)
{
	m_sizer->SetOrientation(static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::Orient, wxVERTICAL)));
	m_sizer->SetMinSize(typeConv::StringToSize(node.GetString(ibProtocol::ibForm::ibProperty::MinimumSize)));
	ibViewUpdateSizer(m_sizer, node);
}

//*******************************************************************
//*                            WrapSizer                            *
//*******************************************************************

void ibValueWrapSizer::Create(wxWindow* /*parent*/, const ibProtocolNode& node)
{
	// Not wxWRAPSIZER_DEFAULT_FLAGS, as on the desktop: its wxEXTEND_LAST_ON_EACH_LINE stretches the last control of
	// every line to the line's end, whatever its own sizer item and MaximumSize say. Leading spaces are still dropped.
	m_sizer = new wxWrapSizer(static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::Orient, wxHORIZONTAL)), wxREMOVE_LEADING_SPACES);
}

void ibValueWrapSizer::Update(const ibProtocolNode& node)
{
	m_sizer->SetOrientation(static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::Orient, wxHORIZONTAL)));
	m_sizer->SetMinSize(typeConv::StringToSize(node.GetString(ibProtocol::ibForm::ibProperty::MinimumSize)));
	ibViewUpdateSizer(m_sizer, node);
}

//*******************************************************************
//*                            GridSizer                            *
//*******************************************************************

void ibValueGridSizer::Create(wxWindow* /*parent*/, const ibProtocolNode& node)
{
	m_sizer = new wxGridSizer(static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::Rows)), static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::Cols)), 0, 0);
}

void ibValueGridSizer::Update(const ibProtocolNode& node)
{
	m_sizer->SetRows(static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::Rows)));
	m_sizer->SetCols(static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::Cols)));
	m_sizer->SetMinSize(typeConv::StringToSize(node.GetString(ibProtocol::ibForm::ibProperty::MinimumSize)));
	ibViewUpdateSizer(m_sizer, node);
}

//*******************************************************************
//*                          StaticBoxSizer                         *
//*******************************************************************

void ibValueStaticBoxSizer::Create(wxWindow* parent, const ibProtocolNode& node)
{
	const ibProtocolNode state = node.FindChild(ibProtocol::ibForm::ibState::State);
	wxStaticBox* const staticBox = new wxStaticBox(parent, wxID_ANY, state.GetString(ibProtocol::ibForm::ibState::Title));
	m_sizer = new wxStaticBoxSizer(staticBox, static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::Orient, wxVERTICAL)));
}

void ibValueStaticBoxSizer::Update(const ibProtocolNode& node)
{
	const ibProtocolNode state = node.FindChild(ibProtocol::ibForm::ibState::State);
	const wxSize minSize = typeConv::StringToSize(node.GetString(ibProtocol::ibForm::ibProperty::MinimumSize));

	m_sizer->SetOrientation(static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::Orient, wxVERTICAL)));
	m_sizer->SetMinSize(minSize);

	// The box is the group's window: its caption, its look, whether it is enabled and shown — the State's, which the
	// server reads with the group's title in the person's language.
	wxStaticBox* const staticBox = m_sizer->GetStaticBox();
	staticBox->SetLabel(state.GetString(ibProtocol::ibForm::ibState::Title));
	staticBox->SetMinSize(minSize);
	ApplyLook(staticBox, node);
	staticBox->Enable(state.GetBool(ibProtocol::ibForm::ibState::Enabled, true));
	staticBox->Show(state.GetBool(ibProtocol::ibForm::ibState::Visible, true));
	staticBox->SetToolTip(node.GetString(ibProtocol::ibForm::ibProperty::Tooltip));

	if (minSize != wxDefaultSize)
		staticBox->Layout();

	ibViewUpdateSizer(m_sizer, node);
}

//*******************************************************************
//*                            SizerItem                            *
//*******************************************************************

void ibValueSizerItem::Update(const ibProtocolNode& node)
{
	if (GetChildCount() == 0)
		return;
	wxSizerItem* const item = GetItem(GetChild(0));
	if (item == nullptr)
		return;

	item->SetProportion(static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::Proportion)));
	item->SetFlag(GetFlag(node));
	item->SetBorder(static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::BorderSize)));
}

wxSizerItem* ibValueSizerItem::GetItem(const ibValueFrame* child)
{
	// The child is in the sizer already (the host's Generate) — the item it stands in is found, not added again: a window
	// knows its sizer, a sizer the window it is laid out in.
	if (wxWindow* const window = child->GetWindow()) {
		if (wxSizer* const holder = window->GetContainingSizer())
			return holder->GetItem(window);
	}
	else if (wxSizer* const sizer = child->GetSizer()) {
		wxWindow* const owner = sizer->GetContainingWindow();
		if (owner != nullptr && owner->GetSizer() != nullptr)
			return owner->GetSizer()->GetItem(sizer, true);
	}
	return nullptr;
}

int ibValueSizerItem::GetFlag(const ibProtocolNode& node)
{
	int flag = static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::Stretch));
	if (node.GetBool(ibProtocol::ibForm::ibProperty::BorderLeft))
		flag |= wxLEFT;
	if (node.GetBool(ibProtocol::ibForm::ibProperty::BorderRight))
		flag |= wxRIGHT;
	if (node.GetBool(ibProtocol::ibForm::ibProperty::BorderTop))
		flag |= wxUP;
	if (node.GetBool(ibProtocol::ibForm::ibProperty::BorderBottom))
		flag |= wxDOWN;
	return flag;
}

