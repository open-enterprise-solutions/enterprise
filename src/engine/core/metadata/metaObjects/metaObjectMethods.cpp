////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaobject methods
////////////////////////////////////////////////////////////////////////////

#include "metaObject.h"
#include "compiler/methods.h"

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

void IMetaObject::PrepareNames() const
{
	std::vector<SEng> aAttributes;

	for (unsigned idx = 0; idx < IObjectBase::GetPropertyCount(); idx++)
	{
		Property *property = IObjectBase::GetProperty(idx); 
		if (!property) 
			continue;

		SEng attributes;
		attributes.sName = property->GetName();
		aAttributes.push_back(attributes);
	}

	m_methods->PrepareAttributes(aAttributes.data(), aAttributes.size());
}

CValue IMetaObject::Method(methodArg_t &aParams)
{
	return CValue();
}