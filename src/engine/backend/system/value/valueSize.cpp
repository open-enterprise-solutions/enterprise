////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value size 
////////////////////////////////////////////////////////////////////////////

#include "valueSize.h"

//////////////////////////////////////////////////////////////////////

ibValueSize::ibValueSize() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE), m_size(wxDefaultSize)
{
}

ibValueSize::ibValueSize(const wxSize& size) : ibValueStaticMembers(ibValueTypes::TYPE_VALUE), m_size(size)
{
}

bool ibValueSize::Init(ibValue** paParams, const long lSizeArray)
{
	if (lSizeArray == 1 && paParams[0]->GetType() == ibValueTypes::TYPE_STRING) {
		m_size = typeConv::StringToSize(paParams[0]->GetString());
		return true;
	}
	else if (lSizeArray > 1) {
		m_size = wxSize(paParams[0]->GetInteger(), paParams[1]->GetInteger());
		return true;
	}
	return false;
}

enum
{
	eWidth,
	eHeight
};

void ibValueSize_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendProp(wxT("Width"));
	helper.AppendProp(wxT("Height"));
}

bool ibValueSize::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	switch (lPropNum)
	{
	case eWidth:
		m_size.x = varPropVal.GetInteger();
		return true;
	case eHeight:
		m_size.y = varPropVal.GetInteger();
		return true;
	}
	return false;
}

bool ibValueSize::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum)
	{
	case eWidth:
		pvarPropVal = m_size.x;
		return true;
	case eHeight:
		pvarPropVal = m_size.y;
		return true;
	}
	return false;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(ibValueSize, "Size", string_to_clsid("VL_SIZE"));