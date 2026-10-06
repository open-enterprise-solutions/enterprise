#include "chartBox.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)

//***********************************************************************************
//*                                 Value Chart box                                 *
//***********************************************************************************

ibValueChartBox::ibValueChartBox() : ibValueWindow()
{
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

CONTROL_TYPE_REGISTER(ibValueChartBox, "Chartbox", "Container", control_to_clsid("CT_CHRB"));
