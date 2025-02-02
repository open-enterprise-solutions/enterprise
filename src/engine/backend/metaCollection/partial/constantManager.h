#ifndef _CONSTANTS_MANAGER_H__
#define _CONSTANTS_MANAGER_H__

#include "constant.h"

class CConstantManager : public CValue {
	wxDECLARE_DYNAMIC_CLASS(CConstantManager);
protected:
	CMetaObjectConstant *m_metaConst;
public:

	CConstantManager(CMetaObjectConstant *metaConst = nullptr);
	virtual ~CConstantManager();

	virtual CMethodHelper* GetPMethods() const {
		//PrepareNames();  
		return &m_methodHelper; 
	} 
	virtual void PrepareNames() const;                        
	virtual bool CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray);

	//Get ref class 
	virtual class_identifier_t GetClassType() const;

	//types 
	virtual wxString GetClassName() const;
	virtual wxString GetString() const;

protected:

	//methods 
	static CMethodHelper m_methodHelper;
};


#endif // !_CONSTANTS_MANAGER_H__
