
#include "widgets.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "backend/compiler/procUnit.h"


//****************************************************************************
//*                             ComboBox                                     *
//****************************************************************************

ibValueComboBox::ibValueComboBox() : ibValueWindow()
{
}

//*******************************************************************
//*								 Data		                        *
//*******************************************************************

bool ibValueComboBox::ReadData(const ibDataNode& node)
{
	return ibValueWindow::ReadData(node);
}

bool ibValueComboBox::WriteData(ibDataNode& node) const
{
	return ibValueWindow::WriteData(node);
}