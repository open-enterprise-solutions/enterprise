#include "widgets.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "backend/compiler/procUnit.h"
#include "form.h"
#include "frontend/visualView/formdefs.h"
#ifdef OES_USE_WEB
#include "frontend/web/webWindow.h"
#endif


//****************************************************************************
//*                             Radiobutton                                  *
//****************************************************************************

ibValueRadioButton::ibValueRadioButton() : ibValueWindow()
{
}

//****************************************************************************
//*                              The group                                   *
//****************************************************************************

// Which buttons are one choice: those sharing a container. A sizer item is not
// a container, only the carrier of the layout params, so it is stepped over.
ibValueFrame* ibValueRadioButton::GetGroupHolder() const
{
	ibValueFrame* holder = GetParent();
	while (holder != nullptr && holder->GetComponentType() == COMPONENT_TYPE_SIZERITEM)
		holder = holder->GetParent();
	return holder;
}

namespace {
void CollectRadioGroup(ibValueFrame* holder, std::vector<ibValueRadioButton*>& out)
{
	if (holder == nullptr)
		return;
	for (unsigned int i = 0; i < holder->GetChildCount(); i++) {
		ibValueFrame* child = dynamic_cast<ibValueFrame*>(holder->GetChild(i));
		if (child == nullptr)
			continue;
		// One level down through a sizer item, which stands between a sizer and
		// what it lays out. A nested SIZER is a group of its own and is not
		// walked into.
		if (child->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
			CollectRadioGroup(child, out);
			continue;
		}
		if (auto* radio = dynamic_cast<ibValueRadioButton*>(child))
			out.push_back(radio);
	}
}
} // namespace

void ibValueRadioButton::SelectInGroup()
{
	std::vector<ibValueRadioButton*> group;
	CollectRadioGroup(GetGroupHolder(), group);
	for (ibValueRadioButton* radio : group)
		radio->m_propertySelected->SetValue(radio == this);
	// A lone button with no container above it still turns itself on.
	m_propertySelected->SetValue(true);
}

wxObject* ibValueRadioButton::Create(ibFrontendWindow* wxparent, ibVisualHost *visualHost)
{
#ifdef OES_USE_WEB
	(void)wxparent;
	(void)visualHost;
	auto* radioButton = new ibWebRadioButton(GetControlID());
	radioButton->Bind(wxEVT_RADIOBUTTON, &ibValueRadioButton::OnWebRadioSelected, this);
	return radioButton;
#else
	wxRadioButton *radioButton = new wxRadioButton(wxparent, wxID_ANY,
		m_propertyTitle->GetValueAsTranslateString(),
		wxDefaultPosition,
		wxDefaultSize);

	return radioButton;
#endif
}

void ibValueRadioButton::OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost *visualHost, bool firstCreated)
{
}

void ibValueRadioButton::Update(wxObject* wxobject, ibVisualHost *visualHost)
{
#ifdef OES_USE_WEB
	(void)visualHost;
	auto* radioButton = static_cast<ibWebRadioButton*>(wxobject);

	if (radioButton != nullptr) {
		radioButton->SetLabel(m_propertyTitle->GetValueAsTranslateString());
		radioButton->SetValue(m_propertySelected->GetValueAsBoolean() != false);
		const ibValueFrame* const holder = GetGroupHolder();
		radioButton->SetGroup(holder != nullptr ? holder->GetControlID() : 0);
	}
#else
	wxRadioButton *radioButton = dynamic_cast<wxRadioButton *>(wxobject);

	if (radioButton != nullptr) {
		radioButton->SetLabel(m_propertyTitle->GetValueAsTranslateString());
		radioButton->SetValue(m_propertySelected->GetValueAsBoolean() != false);
	}
#endif

	UpdateWindow(radioButton);
}

void ibValueRadioButton::Cleanup(wxObject* obj, ibVisualHost *visualHost)
{
#ifdef OES_USE_WEB
	(void)visualHost;
	if (auto* radioButton = static_cast<ibWebRadioButton*>(obj))
		radioButton->Unbind(wxEVT_RADIOBUTTON, &ibValueRadioButton::OnWebRadioSelected, this);
#endif
}

#ifdef OES_USE_WEB
//*******************************************************************
//*                             Events                              *
//*******************************************************************

void ibValueRadioButton::OnWebRadioSelected(wxCommandEvent& event)
{
	SelectInGroup();
	// The siblings that just went off have shims of their own holding the old
	// state; the refresh pass is what carries the new one out to all of them.
	if (m_formOwner != nullptr)
		m_formOwner->UpdateForm();
	event.Skip();
}
#endif

//*******************************************************************
//*                             Property                            *
//*******************************************************************

bool ibValueRadioButton::ReadData(const ibDataNode& node)
{
	// Both were missing, so a radio button drew as "Radio button", unselected,
	// whatever the designer had set -- the properties existed and were never
	// written down.
	m_propertyTitle->SetNodeValue(node.GetProperty(m_propertyTitle->GetName()));
	m_propertySelected->SetNodeValue(node.GetProperty(m_propertySelected->GetName()));
	return ibValueWindow::ReadData(node);
}

bool ibValueRadioButton::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyTitle->GetName(), m_propertyTitle->GetNodeValue());
	node.SetProperty(m_propertySelected->GetName(), m_propertySelected->GetNodeValue());
	return ibValueWindow::WriteData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueRadioButton, "Radiobutton", "Widget", control_to_clsid("CT_RDBT"));
