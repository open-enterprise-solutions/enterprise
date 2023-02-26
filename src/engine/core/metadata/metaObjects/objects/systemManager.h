#ifndef _MANAGER_BASE_H__
#define _MANAGER_BASE_H__

#include "core/compiler/value.h"

class CSystemManager : public CValue {
	wxDECLARE_DYNAMIC_CLASS(CSystemManager);
public:

	CSystemManager(IMetadata *metaData = NULL);
	virtual ~CSystemManager();

	virtual CMethodHelper* GetPMethods() const { 
		PrepareNames();  
		return m_methodHelper;
	} 

	virtual void PrepareNames() const;                       
	
	//attributes
	virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);                  

	//types 
	virtual wxString GetTypeString() const {
		return wxT("systemManager"); 
	}
	
	virtual wxString GetString() const { 
		return wxT("systemManager"); 
	}

protected:
	//methods 
	CMethodHelper *m_methodHelper;
	//metadata 
	IMetadata *m_metaData;
};

#endif 