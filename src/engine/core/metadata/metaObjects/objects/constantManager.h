#ifndef _CONSTANTS_MANAGER_H__
#define _CONSTANTS_MANAGER_H__

#include "constant.h"

class CConstantManager : public CValue {
	wxDECLARE_DYNAMIC_CLASS(CConstantManager);
protected:
	CMetaConstantObject *m_metaConst;
public:

	CConstantManager(CMetaConstantObject *metaConst = NULL);
	virtual ~CConstantManager();

	virtual CMethodHelper* GetPMethods() const {
		PrepareNames();  
		return &m_methodHelper; 
	} 
	virtual void PrepareNames() const;                        
	virtual bool CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray);

	//Get ref class 
	virtual CLASS_ID GetTypeClass() const;

	//types 
	virtual wxString GetTypeString() const;
	virtual wxString GetString() const;

protected:

	//methods 
	static CMethodHelper m_methodHelper;
};


#endif // !_CONSTANTS_MANAGER_H__
