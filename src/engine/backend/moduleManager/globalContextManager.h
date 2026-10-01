#ifndef __MANAGER_BASE_H__
#define __MANAGER_BASE_H__

#include "backend/metaData.h"

class ibValueGlobalContextManager : public ibValueDynamicMembers {
	public:

	ibValueGlobalContextManager(ibMetaData* metaData = nullptr) : ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, true),
		m_metaData(metaData)
	{
		m_members.Bind(this, &ibValueGlobalContextManager::FillMembers);
	}

	virtual ~ibValueGlobalContextManager() {}

	void FillMembers(ibMemberTable& helper) const;   // bound in ctor (was PrepareNames)

	//attributes
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);

protected:

	//metaData
	ibMetaData* m_metaData;
};

#endif 