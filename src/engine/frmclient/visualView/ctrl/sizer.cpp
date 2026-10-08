////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : the sizers of a view
////////////////////////////////////////////////////////////////////////////

#include "sizer.h"

#include "core/serialize/dataBuilder.h"
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

//*******************************************************************************************
//*                  The properties — the desktop's ReadData / WriteData, as they are             *
//*******************************************************************************************

bool ibValueSizer::ReadData(const ibDataNode& node)
{
	m_propertyMinSize->SetNodeValue(node.GetProperty(m_propertyMinSize->GetName()));

	return ibValueControl::ReadData(node);
}

bool ibValueSizer::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyMinSize->GetName(), m_propertyMinSize->GetNodeValue());

	return ibValueControl::WriteData(node);
}

void ibValueSizerItem::Update(const ibProtocolNode& WXUNUSED(node))
{
	// Its child is in the sizer already (the host's Generate) — the item it stands in is found, not added again: a window
	// knows its sizer, a sizer the window it is laid out in.
	if (GetChildCount() == 0)
		return;

	const ibValueFrame* const child = GetChild(0);
	wxSizerItem* item = nullptr;
	if (wxWindow* const window = child->GetWindow()) {
		if (wxSizer* const holder = window->GetContainingSizer())
			item = holder->GetItem(window);
	}
	else if (wxSizer* const sizer = child->GetSizer()) {
		wxWindow* const owner = sizer->GetContainingWindow();
		if (owner != nullptr && owner->GetSizer() != nullptr)
			item = owner->GetSizer()->GetItem(sizer, true);
	}
	if (item == nullptr)
		return;

	item->SetProportion(GetProportion());
	item->SetFlag(GetFlagBorder() | GetFlagState());
	item->SetBorder(GetBorder());
}

bool ibValueSizerItem::ReadData(const ibDataNode& node)
{
	//m_propertyProportion->SetNodeValue(node.GetProperty(m_propertyProportion->GetName()));
	//m_propertyFlagBorder->SetNodeValue(node.GetProperty(m_propertyFlagBorder->GetName()));
	//m_propertyFlagState->SetNodeValue(node.GetProperty(m_propertyFlagState->GetName()));
	//m_propertyBorder->SetNodeValue(node.GetProperty(m_propertyBorder->GetName()));

	m_propertyProportion->SetNodeValue(node.GetProperty(m_propertyProportion->GetName()));
	//m_propertyFlagBorder->SetNodeValue(node.GetProperty(m_propertyFlagBorder->GetName()));

	m_propertyFlagBorderLeft->SetNodeValue(node.GetProperty(m_propertyFlagBorderLeft->GetName()));
	m_propertyFlagBorderRight->SetNodeValue(node.GetProperty(m_propertyFlagBorderRight->GetName()));
	m_propertyFlagBorderTop->SetNodeValue(node.GetProperty(m_propertyFlagBorderTop->GetName()));
	m_propertyFlagBorderBottom->SetNodeValue(node.GetProperty(m_propertyFlagBorderBottom->GetName()));

	m_propertyFlagState->SetNodeValue(node.GetProperty(m_propertyFlagState->GetName()));
	m_propertyBorder->SetNodeValue(node.GetProperty(m_propertyBorder->GetName()));

	return ibValueFrame::ReadData(node);
}

bool ibValueSizerItem::WriteData(ibDataNode& node) const
{
	//node.SetProperty(m_propertyProportion->GetName(), m_propertyProportion->GetNodeValue());
	//node.SetProperty(m_propertyFlagBorder->GetName(), m_propertyFlagBorder->GetNodeValue());
	//node.SetProperty(m_propertyFlagState->GetName(), m_propertyFlagState->GetNodeValue());
	//node.SetProperty(m_propertyBorder->GetName(), m_propertyBorder->GetNodeValue());

	node.SetProperty(m_propertyProportion->GetName(), m_propertyProportion->GetNodeValue());
	//node.SetProperty(m_propertyFlagBorder->GetName(), m_propertyFlagBorder->GetNodeValue());

	node.SetProperty(m_propertyFlagBorderLeft->GetName(), m_propertyFlagBorderLeft->GetNodeValue());
	node.SetProperty(m_propertyFlagBorderRight->GetName(), m_propertyFlagBorderRight->GetNodeValue());
	node.SetProperty(m_propertyFlagBorderTop->GetName(), m_propertyFlagBorderTop->GetNodeValue());
	node.SetProperty(m_propertyFlagBorderBottom->GetName(), m_propertyFlagBorderBottom->GetNodeValue());

	node.SetProperty(m_propertyFlagState->GetName(), m_propertyFlagState->GetNodeValue());
	node.SetProperty(m_propertyBorder->GetName(), m_propertyBorder->GetNodeValue());

	return ibValueFrame::WriteData(node);
}

bool ibValueBoxSizer::ReadData(const ibDataNode& node)
{
	m_propertyOrient->SetNodeValue(node.GetProperty(m_propertyOrient->GetName()));
	return ibValueSizer::ReadData(node);
}

bool ibValueBoxSizer::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyOrient->GetName(), m_propertyOrient->GetNodeValue());
	return ibValueSizer::WriteData(node);
}

bool ibValueWrapSizer::ReadData(const ibDataNode& node)
{
	m_propertyOrient->SetNodeValue(node.GetProperty(m_propertyOrient->GetName()));
	return ibValueSizer::ReadData(node);
}

bool ibValueWrapSizer::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyOrient->GetName(), m_propertyOrient->GetNodeValue());
	return ibValueSizer::WriteData(node);
}

bool ibValueGridSizer::ReadData(const ibDataNode& node)
{
	m_propertyRows->SetNodeValue(node.GetProperty(m_propertyRows->GetName()));
	m_propertyCols->SetNodeValue(node.GetProperty(m_propertyCols->GetName()));

	return ibValueSizer::ReadData(node);
}

bool ibValueGridSizer::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyRows->GetName(), m_propertyRows->GetNodeValue());
	node.SetProperty(m_propertyCols->GetName(), m_propertyCols->GetNodeValue());

	return ibValueSizer::WriteData(node);
}

bool ibValueStaticBoxSizer::ReadData(const ibDataNode& node)
{
	m_propertyOrient->SetNodeValue(node.GetProperty(m_propertyOrient->GetName()));	
	m_propertyTitle->SetNodeValue(node.GetProperty(m_propertyTitle->GetName()));
	m_propertyFont->SetNodeValue(node.GetProperty(m_propertyFont->GetName()));
	m_propertyFG->SetNodeValue(node.GetProperty(m_propertyFG->GetName()));
	m_propertyBG->SetNodeValue(node.GetProperty(m_propertyBG->GetName()));

	m_propertyTooltip->SetNodeValue(node.GetProperty(m_propertyTooltip->GetName()));
	m_propertyContextHelp->SetNodeValue(node.GetProperty(m_propertyContextHelp->GetName()));

	m_propertyContextMenu->SetNodeValue(node.GetProperty(m_propertyContextMenu->GetName()));
	m_propertyEnabled->SetNodeValue(node.GetProperty(m_propertyEnabled->GetName()));
	m_propertyVisible->SetNodeValue(node.GetProperty(m_propertyVisible->GetName()));

	return ibValueSizer::ReadData(node);
}

bool ibValueStaticBoxSizer::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyOrient->GetName(), m_propertyOrient->GetNodeValue());
	node.SetProperty(m_propertyTitle->GetName(), m_propertyTitle->GetNodeValue());
	node.SetProperty(m_propertyFont->GetName(), m_propertyFont->GetNodeValue());
	node.SetProperty(m_propertyFG->GetName(), m_propertyFG->GetNodeValue());
	node.SetProperty(m_propertyBG->GetName(), m_propertyBG->GetNodeValue());
	node.SetProperty(m_propertyTooltip->GetName(), m_propertyTooltip->GetNodeValue());
	node.SetProperty(m_propertyContextHelp->GetName(), m_propertyContextHelp->GetNodeValue());
	node.SetProperty(m_propertyContextMenu->GetName(), m_propertyContextMenu->GetNodeValue());
	node.SetProperty(m_propertyEnabled->GetName(), m_propertyEnabled->GetNodeValue());
	node.SetProperty(m_propertyVisible->GetName(), m_propertyVisible->GetNodeValue());

	return ibValueSizer::WriteData(node);
}

