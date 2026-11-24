#include "chartBox.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode (control -> node)

//***********************************************************************************
//*                                 Value Chart box                                 *
//***********************************************************************************

ibValueChartBox::ibValueChartBox() : ibValueWindow()
{
	//set default params
	m_sizerItem->SetProportion(1);
	m_sizerItem->SetFlagState(ibStretch_Expand);
}

//**********************************************************************************
//*                                   Data		                                   *
//**********************************************************************************

bool ibValueChartBox::ReadData(const ibDataNode& node)
{
	return ibValueWindow::ReadData(node);
}

bool ibValueChartBox::WriteData(ibDataNode& node) const
{
	return ibValueWindow::WriteData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueChartBox, "Chart", "Container", control_to_clsid("CT_CHRB"));
