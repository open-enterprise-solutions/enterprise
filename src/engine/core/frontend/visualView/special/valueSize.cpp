////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value size 
////////////////////////////////////////////////////////////////////////////

#include "valueSize.h"

#include "frontend/mainFrame.h"
#include <3rdparty/databaseLayer/databaseLayer.h>

#include "utils/stringUtils.h"
#include "utils/typeconv.h"

//////////////////////////////////////////////////////////////////////
wxIMPLEMENT_DYNAMIC_CLASS(CValueSize, CValue);

CValue::CMethodHelper CValueSize::m_methodHelper;

CValueSize::CValueSize() : CValue(eValueTypes::TYPE_VALUE), m_size(wxDefaultSize)
{
}

CValueSize::CValueSize(const wxSize& size) : CValue(eValueTypes::TYPE_VALUE), m_size(size)
{
}

bool CValueSize::Init(CValue** paParams, const long lSizeArray)
{
	if (paParams[0]->GetType() == eValueTypes::TYPE_STRING) {
		m_size = TypeConv::StringToSize(paParams[0]->GetString());
		return true;
	}
	else {
		m_size = wxSize(paParams[0]->GetInteger(), paParams[1]->GetInteger());
		return true;
	}
	return false;
}

CValueSize::~CValueSize()
{
}

enum
{
	eX,
	eY
};

void CValueSize::PrepareNames() const
{
	m_methodHelper.ClearHelper();

	m_methodHelper.AppendProp(wxT("x"));
	m_methodHelper.AppendProp(wxT("y"));
}

bool CValueSize::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
	switch (lPropNum)
	{
	case eX:
		m_size.x = varPropVal.GetInteger();
		return true;
	case eY:
		m_size.y = varPropVal.GetInteger();
		return true;
	}
	return false;
}

bool CValueSize::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	switch (lPropNum)
	{
	case eX:
		pvarPropVal = m_size.x;
		return true;
	case eY:
		pvarPropVal = m_size.y;
		return true;
	}
	return false;
}


wxString CValueSize::GetTypeString()const
{
	return "size";
}

wxString CValueSize::GetString()const
{
	return TypeConv::SizeToString(m_size);
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_REGISTER(CValueSize, "size", TEXT2CLSID("VL_SIZE"));