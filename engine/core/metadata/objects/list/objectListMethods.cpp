////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : list methods 
////////////////////////////////////////////////////////////////////////////

#include "objectList.h"
#include "compiler/methods.h"

enum
{
};

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

CMethods* CDataObjectList::GetPMethods() const
{
	return m_methods;
};

void CDataObjectList::PrepareNames() const
{
	m_methods->ResetAttributes();
	m_methods->AppendAttribute(wxT("choiceMode"));
}

CValue CDataObjectList::Method(methodArg_t &aParams)
{
	CValue Ret;
	return Ret;
}