
#include "widgets.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "backend/compiler/procUnit.h"


//****************************************************************************
//*                             Listbox                                      *
//****************************************************************************

ibValueListBox::ibValueListBox() : ibValueWindow()
{
}

//*******************************************************************
//*								Data	                            *
//*******************************************************************

bool ibValueListBox::ReadData(const ibDataNode& node)
{
	return ibValueWindow::ReadData(node);
}

bool ibValueListBox::WriteData(ibDataNode& node) const
{
	return ibValueWindow::WriteData(node);
}
