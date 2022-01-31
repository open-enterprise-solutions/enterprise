////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : enum unit
////////////////////////////////////////////////////////////////////////////

#include "enum.h"
#include "methods.h"

IEnumerationMethods::IEnumerationMethods(bool createInstance) : CValue(eValueTypes::TYPE_VALUE, true), m_methods(NULL)
{
	if (createInstance) {
		m_methods = new CMethods();
	}
}

IEnumerationMethods::~IEnumerationMethods()
{
	if (m_methods) 
		delete m_methods;
}
 
void IEnumerationMethods::PrepareNames() const
{
	if (!m_methods) 
		return;

	std::vector<SEng> aAttributes;
	for (auto enumVal : m_aEnumsString) aAttributes.push_back(enumVal);
	m_methods->PrepareAttributes(aAttributes.data(), aAttributes.size());
}