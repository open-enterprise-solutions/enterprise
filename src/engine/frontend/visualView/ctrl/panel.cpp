#include "panel.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "frontend/visualView/pageWindow.h"   // ibPanelPage — plain wxPanel + wxBoxSizer

//***********************************************************************************
//*                                 Value Panel                                     *
//***********************************************************************************

ibValuePanel::ibValuePanel() : ibValueWindow()
{
}

wxObject* ibValuePanel::Create(wxWindow* wxparent, ibVisualHost* visualHost)
{
	ibPanelPage* panel = new ibPanelPage(wxparent, wxID_ANY);
	panel->SetOrientation(m_propertyOrient->GetValueAsInteger());
	return panel;
}

void ibValuePanel::OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstCreated)
{
}

void ibValuePanel::Update(wxObject* wxobject, ibVisualHost* visualHost)
{
	ibPanelPage* panel = dynamic_cast<ibPanelPage*>(wxobject);
	if (panel != nullptr) {
		panel->SetOrientation(m_propertyOrient->GetValueAsInteger());
	}

	UpdateWindow(panel);
}

void ibValuePanel::OnUpdated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost)
{
}

void ibValuePanel::Cleanup(wxObject* wxobject, ibVisualHost* visualHost)
{
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
