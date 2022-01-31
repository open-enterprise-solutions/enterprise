////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : base control - methods
////////////////////////////////////////////////////////////////////////////

#include "baseControl.h"
#include "compiler/methods.h"

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

void IValueFrame::PrepareNames() const
{
	std::vector<SEng> aAttributes;

	for (unsigned int idx = 0; idx < IObjectBase::GetPropertyCount(); idx++)
	{
		Property *property = IObjectBase::GetProperty(idx);

		if (!property)
			continue;

		SEng attributes;
		attributes.sName = property->GetName();
		attributes.sSynonym = wxT("attribute");
		attributes.iName = idx; 
		aAttributes.push_back(attributes);
	}

	//if we have sizerItem then call him  
	IValueFrame *m_sizeritem = GetParent();
	if (m_sizeritem && m_sizeritem->GetClassName() == wxT("sizerItem"))
	{
		for (unsigned int idx = 0; idx < m_sizeritem->GetPropertyCount(); idx++)
		{
			Property *property = m_sizeritem->GetProperty(idx);

			SEng attributes;
			attributes.sName = property->GetName();
			attributes.sSynonym = wxT("sizerItem");
			attributes.iName = idx;
			aAttributes.push_back(attributes);
		}
	}

	SEng attributes;
	attributes.sName = wxT("events");
	attributes.sSynonym = wxT("events");
	aAttributes.push_back(attributes);

	m_methods->PrepareAttributes(aAttributes.data(), aAttributes.size());
}

CValue IValueFrame::Method(methodArg_t &aParams)
{
	return CValue();
}