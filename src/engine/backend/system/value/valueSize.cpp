////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value size 
////////////////////////////////////////////////////////////////////////////

#include "valueSize.h"

//////////////////////////////////////////////////////////////////////

ibValueSize::ibValueSize() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE), m_size(ibDefaultSize)
{
}

ibValueSize::ibValueSize(const ibSize& size) : ibValueStaticMembers(ibValueTypes::TYPE_VALUE), m_size(size)
{
}

bool ibValueSize::Init(ibValue** paParams, const long lSizeArray)
{
	if (lSizeArray == 1 && paParams[0]->GetType() == ibValueTypes::TYPE_STRING) {
		m_size.FromString(paParams[0]->GetString());
		return true;
	}
	else if (lSizeArray > 1) {
		m_size = ibSize(paParams[0]->GetInteger(), paParams[1]->GetInteger());
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
	helper.AppendProp(ibT("Width"));
	helper.AppendProp(ibT("Height"));
}

bool ibValueSize::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	switch (lPropNum)
	{
	case eWidth:
		m_size.m_width = varPropVal.GetInteger();
		return true;
	case eHeight:
		m_size.m_height = varPropVal.GetInteger();
		return true;
	}
	return false;
}

bool ibValueSize::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum)
	{
	case eWidth:
		pvarPropVal = m_size.m_width;
		return true;
	case eHeight:
		pvarPropVal = m_size.m_height;
		return true;
	}
	return false;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(ibValueSize, "Size", value_to_clsid("VL_SIZE"));
