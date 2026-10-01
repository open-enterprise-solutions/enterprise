////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : enum unit
////////////////////////////////////////////////////////////////////////////

#include "enumUnit.h"

ibValueEnumerationWrapper::ibValueEnumerationWrapper(bool /*createInstance*/) :
	ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, true)
{
	m_members.Bind(this, &ibValueEnumerationWrapper::FillMembers);
}

ibValueEnumerationWrapper::~ibValueEnumerationWrapper()
{
}

void ibValueEnumerationWrapper::FillMembers(ibMemberTable& helper) const
{
	for (auto &obj : m_listEnumStr) {
		helper.AppendProp(obj);
	}
}