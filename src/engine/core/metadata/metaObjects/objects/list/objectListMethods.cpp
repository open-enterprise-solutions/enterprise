////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : list methods 
////////////////////////////////////////////////////////////////////////////

#include "objectList.h"
#include "compiler/methods.h"

enum
{
	enRefresh
};

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

CMethods* CListDataObjectRef::GetPMethods() const {
	PrepareNames(); 
	return m_methods;
};

void CListDataObjectRef::PrepareNames() const
{
	m_methods->ResetAttributes();
	m_methods->AppendAttribute(wxT("choiceMode"));
	m_methods->ResetMethods();
	m_methods->AppendMethod(wxT("refresh"), "refresh()");
}

CValue CListDataObjectRef::Method(methodArg_t& aParams)
{
	switch (aParams.GetIndex())
	{
	case enRefresh: 
		RefreshModel();
		break;
	}

	return new CValueNoRet(aParams.GetName());
}

////////////////////////////////////////////////////////////////////////////////

CMethods* CListDataObjectGroupRef::GetPMethods() const {
	PrepareNames();
	return m_methods;
};

void CListDataObjectGroupRef::PrepareNames() const
{
	m_methods->ResetAttributes();
	m_methods->AppendAttribute(wxT("choiceMode"));
	m_methods->ResetMethods();
	m_methods->AppendMethod(wxT("refresh"), "refresh()");
}

CValue CListDataObjectGroupRef::Method(methodArg_t& aParams)
{
	switch (aParams.GetIndex())
	{
	case enRefresh:
		RefreshModel();
		break;
	}

	return new CValueNoRet(aParams.GetName());
}

////////////////////////////////////////////////////////////////////////////////

CMethods* CListRegisterObject::GetPMethods() const {
	PrepareNames();
	return m_methods;
};

void CListRegisterObject::PrepareNames() const
{
	m_methods->ResetMethods();
	m_methods->AppendMethod(wxT("refresh"), "refresh()");
}

CValue CListRegisterObject::Method(methodArg_t& aParams)
{	
	switch (aParams.GetIndex())
	{
	case enRefresh:
		RefreshModel();
		break;
	}
	
	return new CValueNoRet(aParams.GetName());
}