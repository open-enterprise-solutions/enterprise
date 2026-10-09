
#include "widgets.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "backend/compiler/procUnit.h"


//****************************************************************************
//*                             Choice                                       *
//****************************************************************************

ibValueChoice::ibValueChoice() : ibValueWindow()
{
}

//*******************************************************************
//*								Data	                            *
//*******************************************************************

bool ibValueChoice::ReadData(const ibDataNode& node)
{
	return ibValueWindow::ReadData(node);
}

bool ibValueChoice::WriteData(ibDataNode& node) const
{
	return ibValueWindow::WriteData(node);
}