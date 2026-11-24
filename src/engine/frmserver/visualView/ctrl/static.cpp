#include "static.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode (control -> node)

//****************************************************************************
//*                       the view's child, as the static                    *
//****************************************************************************

ibString ibStaticViewProvider::GetClassName() const
{
	return m_static->GetClassName();
}

ibString ibStaticViewProvider::GetObjectTypeName() const
{
	return m_static->GetObjectTypeName();
}

bool ibStaticViewProvider::IsEditable() const
{
	return m_static->IsEditable();
}

const ibMetaData* ibStaticViewProvider::GetMetaData() const
{
	return m_static->GetMetaData();
}

ibString ibStaticViewProvider::GetControlTitle() const
{
	return m_static->ibValueFrame::GetControlTitle();
}

std::unique_ptr<ibStaticViewProvider> ibStaticViewProvider::Create(ibStaticView view, ibValueStatic* control)
{
	switch (view) {
	case ibStaticView::ibStaticView_Text: return std::make_unique<ibStaticViewText>(control);
	case ibStaticView::ibStaticView_Line: return std::make_unique<ibStaticViewLine>(control);
	}
	return nullptr;
}

//****************************************************************************
//*                                Static                                    *
//****************************************************************************

ibValueStatic::ibValueStatic() : ibValueWindow()
{
	// born a text
	SetView(ibStaticView::ibStaticView_Text);
}

void ibValueStatic::OnUpdate(ibDataNode& state, ibVisualHost* host)
{
	ibValueWindow::OnUpdate(state, host);
	m_provider->OnUpdate(state, host);
}

// View changed — in the inspector, over MCP or from a script (the live road; the designer's other road reads a static
// anew from its node, ReadData). The child of the new view takes the old one's place, keeping what the two share by name.
void ibValueStatic::OnPropertyChanged(ibProperty* property, const ibVariant& oldValue, const ibVariant& newValue)
{
	// The view does not touch its place in the sizer: a line runs the full width when the person sets Stretch to Expand
	// (Max, 2026-11-24 — set for him, it stayed with a text chosen back).
	if (property == m_propertyView)
		SetView(GetView());

	ibValueWindow::OnPropertyChanged(property, oldValue, newValue);
}

//*******************************************************************
//*							 The view    	                        *
//*******************************************************************

void ibValueStatic::SetView(ibStaticView view)
{
	std::unique_ptr<ibStaticViewProvider> made = ibStaticViewProvider::Create(view, this);
	if (made == nullptr) {
		// A view the enumeration has and Create does not — a new view half added: the static is kept, a text.
		m_propertyView->SetValue(ibStaticView::ibStaticView_Text);
		made = ibStaticViewProvider::Create(ibStaticView::ibStaticView_Text, this);
	}

	if (m_provider != nullptr) {
		// WHAT THE TWO VIEWS HAVE BY THE SAME NAME CROSSES OVER: the old view's node read by the new one.
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

//*******************************************************************
//*                              Data	                            *
//*******************************************************************

bool ibValueStatic::ReadData(const ibDataNode& node)
{
	// A view this version does not know (a later one, or a damaged form) is refused by the property, which keeps the one
	// it holds: the static is kept, a text, and the number is said.
	const auto saved = node.FindProperty(m_propertyView->GetName());
	if (saved && !m_propertyView->SetNodeValue(*saved)) {
		ibJournalWarning(ibT("ui.form"), _("Static '%s' has a view this version does not know (%d); it is shown as Text."),
			GetControlName(), (int)saved->AsInt());
		m_propertyView->SetValue(ibStaticView::ibStaticView_Text);
	}
	SetView(GetView());

	if (!m_provider->ReadData(node))
		return false;

	return ibValueWindow::ReadData(node);
}

bool ibValueStatic::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyView->GetName(), m_propertyView->GetNodeValue());

	if (!m_provider->WriteData(node))
		return false;

	return ibValueWindow::WriteData(node);
}

// The copy walk (ibValueFrame::CopyNode) carries every property, the view's among them; the paste makes the static of the
// view pasted first, and its child takes what the copy carried.
bool ibValueStatic::PasteData(const ibDataNode& node)
{
	SetView(GetView());
	return m_provider->PasteData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueStatic, "Static", "Widget", g_controlStaticCLSID);
