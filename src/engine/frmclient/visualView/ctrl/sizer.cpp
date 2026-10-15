////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : the sizers of a view
////////////////////////////////////////////////////////////////////////////

#include "sizer.h"

#include "frmclient/win/typeconv.h"

void ibViewUpdateSizer(wxSizer* sizer, const ibProtocolNode& node)
{
	if (sizer == nullptr)
		return;

	const wxSize minSize = typeConv::StringToSize(node.GetString(ibProtocolName::MinimumSize));
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
	m_sizer = new wxBoxSizer(static_cast<int>(node.GetInt(ibProtocolName::Orient, wxVERTICAL)));
}

void ibValueBoxSizer::Update(const ibProtocolNode& node)
{
	m_sizer->SetOrientation(static_cast<int>(node.GetInt(ibProtocolName::Orient, wxVERTICAL)));
	m_sizer->SetMinSize(typeConv::StringToSize(node.GetString(ibProtocolName::MinimumSize)));
	ibViewUpdateSizer(m_sizer, node);
}

//*******************************************************************
//*                            WrapSizer                            *
//*******************************************************************

void ibValueWrapSizer::Create(wxWindow* /*parent*/, const ibProtocolNode& node)
{
	m_sizer = new wxWrapSizer(static_cast<int>(node.GetInt(ibProtocolName::Orient, wxHORIZONTAL)), wxWRAPSIZER_DEFAULT_FLAGS);
}

void ibValueWrapSizer::Update(const ibProtocolNode& node)
{
	m_sizer->SetOrientation(static_cast<int>(node.GetInt(ibProtocolName::Orient, wxHORIZONTAL)));
	m_sizer->SetMinSize(typeConv::StringToSize(node.GetString(ibProtocolName::MinimumSize)));
	ibViewUpdateSizer(m_sizer, node);
}

//*******************************************************************
//*                            GridSizer                            *
//*******************************************************************

void ibValueGridSizer::Create(wxWindow* /*parent*/, const ibProtocolNode& node)
{
	m_sizer = new wxGridSizer(static_cast<int>(node.GetInt(ibProtocolName::Rows)), static_cast<int>(node.GetInt(ibProtocolName::Cols)), 0, 0);
}

void ibValueGridSizer::Update(const ibProtocolNode& node)
{
	m_sizer->SetRows(static_cast<int>(node.GetInt(ibProtocolName::Rows)));
	m_sizer->SetCols(static_cast<int>(node.GetInt(ibProtocolName::Cols)));
	m_sizer->SetMinSize(typeConv::StringToSize(node.GetString(ibProtocolName::MinimumSize)));
	ibViewUpdateSizer(m_sizer, node);
}

//*******************************************************************
//*                          StaticBoxSizer                         *
//*******************************************************************

void ibValueStaticBoxSizer::Create(wxWindow* parent, const ibProtocolNode& node)
{
	const ibProtocolNode state = node.FindChild(ibProtocolName::State);
	wxStaticBox* const staticBox = new wxStaticBox(parent, wxID_ANY, state.GetString(ibProtocolName::Title));
	m_sizer = new wxStaticBoxSizer(staticBox, static_cast<int>(node.GetInt(ibProtocolName::Orient, wxVERTICAL)));
}

void ibValueStaticBoxSizer::Update(const ibProtocolNode& node)
{
	const ibProtocolNode state = node.FindChild(ibProtocolName::State);
	const wxSize minSize = typeConv::StringToSize(node.GetString(ibProtocolName::MinimumSize));

	m_sizer->SetOrientation(static_cast<int>(node.GetInt(ibProtocolName::Orient, wxVERTICAL)));
	m_sizer->SetMinSize(minSize);

	// The box is the group's window: its caption, its look, whether it is enabled and shown — the State's, which the
	// server reads with the group's title in the person's language.
	wxStaticBox* const staticBox = m_sizer->GetStaticBox();
	staticBox->SetLabel(state.GetString(ibProtocolName::Title));
	staticBox->SetMinSize(minSize);
	ApplyLook(staticBox, node);
	staticBox->Enable(state.GetBool(ibProtocolName::Enabled, true));
	staticBox->Show(state.GetBool(ibProtocolName::Visible, true));
	staticBox->SetToolTip(node.GetString(ibProtocolName::Tooltip));

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

	item->SetProportion(static_cast<int>(node.GetInt(ibProtocolName::Proportion)));
	item->SetFlag(GetFlag(node));
	item->SetBorder(static_cast<int>(node.GetInt(ibProtocolName::BorderSize)));
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
	int flag = static_cast<int>(node.GetInt(ibProtocolName::Stretch));
	if (node.GetBool(ibProtocolName::BorderLeft))
		flag |= wxLEFT;
	if (node.GetBool(ibProtocolName::BorderRight))
		flag |= wxRIGHT;
	if (node.GetBool(ibProtocolName::BorderTop))
		flag |= wxUP;
	if (node.GetBool(ibProtocolName::BorderBottom))
		flag |= wxDOWN;
	return flag;
}

