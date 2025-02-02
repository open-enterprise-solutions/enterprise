////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : enum unit
////////////////////////////////////////////////////////////////////////////

#include "enum.h"

IEnumerationWrapper::IEnumerationWrapper(bool createInstance) :
	CValue(eValueTypes::TYPE_VALUE, true), m_methodHelper(nullptr)
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
	if (m_methodHelper != nullptr) {
		m_methodHelper->ClearHelper();
		for (auto &obj : m_aEnumsString) {
			m_methodHelper->AppendProp(obj);
		}
	}
}