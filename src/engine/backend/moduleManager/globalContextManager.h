#ifndef __MANAGER_BASE_H__
#define __MANAGER_BASE_H__

#include "backend/metaData.h"

class ibValueGlobalContextManager : public ibValue {
	public:

	ibValueGlobalContextManager(ibMetaData* metaData = nullptr) : ibValue(ibValueTypes::TYPE_VALUE, true),
		m_methodHelper(new ibValueMethodHelper()), m_metaData(metaData)
	{
	}

	virtual ~ibValueGlobalContextManager() { wxDELETE(m_methodHelper); }

	virtual ibValueMethodHelper* GetPMethods() const {
		//PrepareNames();  
		return m_methodHelper;
	}

	virtual void PrepareNames() const;

	//attributes
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);

protected:

	//metaData 
	ibMetaData* m_metaData;

	//methods 
	ibValueMethodHelper* m_methodHelper;
};

#endif 