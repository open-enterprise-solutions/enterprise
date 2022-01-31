////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value size 
////////////////////////////////////////////////////////////////////////////

#include "valueSize.h"
#include "compiler/methods.h"

#include "frontend/mainFrame.h"
#include "databaseLayer/databaseLayer.h"

#include "utils/stringUtils.h"
#include "utils/typeconv.h"

//////////////////////////////////////////////////////////////////////
wxIMPLEMENT_DYNAMIC_CLASS(CValueSize, CValue);

CMethods CValueSize::m_methods;

CValueSize::CValueSize() : CValue(eValueTypes::TYPE_VALUE), m_size(wxDefaultSize)
{
}

CValueSize::CValueSize(const wxSize &size) : CValue(eValueTypes::TYPE_VALUE), m_size(size)
{
}

bool CValueSize::Init(CValue **aParams)
{
	if (aParams[0]->GetType() == eValueTypes::TYPE_STRING) { m_size = TypeConv::StringToSize(aParams[0]->ToString()); return true; }
	else { m_size = wxSize(aParams[0]->ToInt(), aParams[1]->ToInt()); return true; }
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
	std::vector <SEng>aAttributes;

	SEng attribute;

	attribute.sName = "x";
	aAttributes.push_back(attribute);
	attribute.sName = "y";
	aAttributes.push_back(attribute);

	m_methods.PrepareAttributes(aAttributes.data(), aAttributes.size());
}

void CValueSize::SetAttribute(attributeArg_t &aParams, CValue &cVal)
{
	switch (aParams.GetIndex())
	{
	case eX: m_size.x = cVal.ToInt(); break;
	case eY: m_size.y = cVal.ToInt(); break;
	}
}

CValue CValueSize::GetAttribute(attributeArg_t &aParams)
{
	switch (aParams.GetIndex())
	{
	case eX: return m_size.x;
	case eY: return m_size.y;
	}

	return CValue();
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