#include "panel.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode (control -> node)

//***********************************************************************************
//*                                 Value Panel                                     *
//***********************************************************************************

ibValuePanel::ibValuePanel() : ibValueWindow()
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
