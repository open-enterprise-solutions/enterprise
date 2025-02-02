#ifndef _MANAGER_BASE_H__
#define _MANAGER_BASE_H__

#include "backend/metaData.h"

class CContextSystemManager : public CValue {
	wxDECLARE_DYNAMIC_CLASS(CContextSystemManager);
public:

	CContextSystemManager(IMetaData *metaData = nullptr);
	virtual ~CContextSystemManager();

	virtual CMethodHelper* GetPMethods() const { 
		//PrepareNames();  
		return m_methodHelper;
	} 

	virtual void PrepareNames() const;                       
	
	//attributes
	virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);                  

protected:
	//methods 
	CMethodHelper *m_methodHelper;
	//metaData 
	IMetaData *m_metaData;
};

#endif 