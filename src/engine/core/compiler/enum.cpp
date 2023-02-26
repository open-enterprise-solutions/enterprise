////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : enum unit
////////////////////////////////////////////////////////////////////////////

#include "enum.h"

IEnumerationWrapper::IEnumerationWrapper(bool createInstance) :
	CValue(eValueTypes::TYPE_VALUE, true), m_methodHelper(NULL)
{
	if (createInstance) {
		m_methodHelper = new CMethodHelper();
	}
}

IEnumerationWrapper::~IEnumerationWrapper()
{
	wxDELETE(m_methodHelper);
}

void IEnumerationWrapper::PrepareNames() const
{
	if (m_methodHelper == NULL)
		return;

	m_methodHelper->ClearHelper();
	for (auto enumVal : m_aEnumsString) {
		m_methodHelper->AppendProp(enumVal);
	}
}