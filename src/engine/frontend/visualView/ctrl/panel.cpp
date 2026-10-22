#include "panel.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#ifdef OES_USE_WEB
#include "frontend/web/webWindow.h"           // ibWebPanel — the same window + box sizer, drawn by the browser
#else
#include "frontend/visualView/pageWindow.h"   // ibPanelPage — plain wxPanel + wxBoxSizer
#endif

//***********************************************************************************
//*                                 Value Panel                                     *
//***********************************************************************************

ibValuePanel::ibValuePanel() : ibValueWindow()
{
}

wxObject* ibValuePanel::Create(ibFrontendWindow* wxparent, ibVisualHost* visualHost)
{
#ifdef OES_USE_WEB
	(void)wxparent;
	ibWebPanel* panel = new ibWebPanel(GetControlID());
#else
	ibPanelPage* panel = new ibPanelPage(wxparent, wxID_ANY);
#endif
	panel->SetOrientation(m_propertyOrient->GetValueAsInteger());
	return panel;
}

void ibValuePanel::OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated)
{
}

void ibValuePanel::Update(wxObject* wxobject, ibVisualHost* visualHost)
{
#ifdef OES_USE_WEB
	ibWebPanel* panel = dynamic_cast<ibWebPanel*>(wxobject);
#else
	ibPanelPage* panel = dynamic_cast<ibPanelPage*>(wxobject);
#endif
	if (panel != nullptr) {
		panel->SetOrientation(m_propertyOrient->GetValueAsInteger());
	}

	UpdateWindow(panel);
}

void ibValuePanel::OnUpdated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost)
{
}

void ibValuePanel::Cleanup(wxObject* wxobject, ibVisualHost* visualHost)
{
}

ibFrontendSizer* ibValuePanel::GetSizer(wxObject* wxobject) const
{
#ifdef OES_USE_WEB
	ibWebPanel* const panel = static_cast<ibWebPanel*>(wxobject);     // what Create built
#else
	ibPanelPage* const panel = static_cast<ibPanelPage*>(wxobject);   // what Create built
#endif
	return panel != nullptr ? panel->GetSizer() : nullptr;
}

//**********************************************************************************
//*                                    Data										   *
//**********************************************************************************

bool ibValuePanel::ReadData(const ibDataNode& node)
{
	m_propertyOrient->SetNodeValue(node.GetProperty(m_propertyOrient->GetName()));
	return ibValueWindow::ReadData(node);
}

bool ibValuePanel::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyOrient->GetName(), m_propertyOrient->GetNodeValue());
	return ibValueWindow::WriteData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValuePanel, "Panel", "Container", g_controlPanelCLSID);
