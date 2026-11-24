#include "layout.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode (control -> node)

//****************************************************************************
//*                       the view's child, as the layout                    *
//****************************************************************************

ibString ibLayoutViewProvider::GetClassName() const
{
	return m_layout->GetClassName();
}

ibString ibLayoutViewProvider::GetObjectTypeName() const
{
	return m_layout->GetObjectTypeName();
}

bool ibLayoutViewProvider::IsEditable() const
{
	return m_layout->IsEditable();
}

const ibMetaData* ibLayoutViewProvider::GetMetaData() const
{
	return m_layout->GetMetaData();
}

ibString ibLayoutViewProvider::GetControlTitle() const
{
	return m_layout->ibValueFrame::GetControlTitle();
}

std::unique_ptr<ibLayoutViewProvider> ibLayoutViewProvider::Create(ibLayoutView view, ibValueLayout* layout)
{
	switch (view) {
	case ibLayoutView::ibLayoutView_Box:   return std::make_unique<ibLayoutViewBox>(layout);
	case ibLayoutView::ibLayoutView_Wrap:  return std::make_unique<ibLayoutViewWrap>(layout);
	case ibLayoutView::ibLayoutView_Grid:  return std::make_unique<ibLayoutViewGrid>(layout);
	case ibLayoutView::ibLayoutView_Group: return std::make_unique<ibLayoutViewGroup>(layout);
	}
	return nullptr;
}

//****************************************************************************
//*                                Layout                                    *
//****************************************************************************

ibValueLayout::ibValueLayout() : ibValueControl()
{
	// The frame owns the layout's place in the sizer above, attached: its properties are the layout's…
	m_sizerItem = std::make_unique<ibSizerItem>(this);
	AttachPropertyObject(m_sizerItem.get());

	//set default params — …and a layout in a layout fills it across, with no margin of its own
	m_sizerItem->SetBorder(0);
	m_sizerItem->SetFlagState(ibStretch_Expand);

	// born a box
	SetView(ibLayoutView::ibLayoutView_Box);
}

void ibValueLayout::OnUpdate(ibDataNode& state, ibVisualHost* host)
{
	m_provider->OnUpdate(state, host);
}

// View changed — in the inspector, over MCP or from a script (the live road; the designer's other road reads a layout
// anew from its node, ReadData). The child of the new view takes the old one's place, keeping what the two share by name.
void ibValueLayout::OnPropertyChanged(ibProperty* property, const ibVariant& oldValue, const ibVariant& newValue)
{
	if (property == m_propertyView)
		SetView(GetView());

	ibValueControl::OnPropertyChanged(property, oldValue, newValue);
}

//*******************************************************************
//*							 The view    	                        *
//*******************************************************************

void ibValueLayout::SetView(ibLayoutView view)
{
	std::unique_ptr<ibLayoutViewProvider> made = ibLayoutViewProvider::Create(view, this);
	if (made == nullptr) {
		// A view the enumeration has and Create does not — a new view half added: the layout is kept, a box.
		m_propertyView->SetValue(ibLayoutView::ibLayoutView_Box);
		made = ibLayoutViewProvider::Create(ibLayoutView::ibLayoutView_Box, this);
	}

	if (m_provider != nullptr) {
		// WHAT THE TWO VIEWS HAVE BY THE SAME NAME CROSSES OVER — a box turned into a group keeps its Orient: the old
		// view's node read by the new one.
		ibDataNode carried;
		m_provider->WriteData(carried);
		made->ReadData(carried);
		RemoveAttachedObject(m_provider.get());
	}

	m_provider = std::move(made);
	AttachPropertyObject(m_provider.get());

	// The members a script names are the new view's.
	InvalidateNames();
}

//**********************************************************************************
//*                                    Data										   *
//**********************************************************************************

bool ibValueLayout::ReadData(const ibDataNode& node)
{
	// A view this version does not know (a later one, or a damaged form) is refused by the property, which keeps the
	// one it holds: the layout is kept, a box, and the number is said.
	const auto saved = node.FindProperty(m_propertyView->GetName());
	if (saved && !m_propertyView->SetNodeValue(*saved)) {
		ibJournalWarning(ibT("ui.form"), _("Layout '%s' has a view this version does not know (%d); it is laid out as a box."),
			GetControlName(), (int)saved->AsInt());
		m_propertyView->SetValue(ibLayoutView::ibLayoutView_Box);
	}
	SetView(GetView());

	if (!m_provider->ReadData(node))
		return false;

	m_propertyMinSize->SetNodeValue(node.GetProperty(m_propertyMinSize->GetName()));

	if (!m_sizerItem->ReadData(node))
		return false;

	return ibValueControl::ReadData(node);
}

bool ibValueLayout::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyView->GetName(), m_propertyView->GetNodeValue());

	if (!m_provider->WriteData(node))
		return false;

	node.SetProperty(m_propertyMinSize->GetName(), m_propertyMinSize->GetNodeValue());

	if (!m_sizerItem->WriteData(node))
		return false;

	return ibValueControl::WriteData(node);
}

// The copy walk (ibValueFrame::CopyNode) carries every property, the view's among them; the paste makes the layout of
// the view pasted first, and its child takes what the copy carried.
bool ibValueLayout::PasteData(const ibDataNode& node)
{
	SetView(GetView());
	return m_provider->PasteData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueLayout, "Layout", "Sizer", g_controlLayoutCLSID);
