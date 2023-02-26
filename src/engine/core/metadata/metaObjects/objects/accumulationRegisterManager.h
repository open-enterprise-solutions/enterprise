#ifndef _ACC_REGISTER_MANAGER_H__
#define _ACC_REGISTER_MANAGER_H__

#include "accumulationRegister.h"
#include "core/common/managerInfo.h"

class CAccumulationRegisterManager : public CValue,
	public IMetaManagerInfo {
	wxDECLARE_DYNAMIC_CLASS(CInformationRegisterManager);
public:

	CValue Balance(const CValue& cPeriod, const CValue& cFilter = CValue());
	CValue Turnovers(const CValue& cBeginOfPeriod, const CValue& cEndOfPeriod, const CValue& cFilter = CValue());

	CAccumulationRegisterManager(CMetaObjectAccumulationRegister* metaObject = NULL);
	virtual ~CAccumulationRegisterManager();

	virtual CMetaCommonModuleObject* GetModuleManager() const;

	virtual CMethodHelper* GetPMethods() const { PrepareNames(); return m_methodHelper; } //получить ссылку на класс помощник разбора имен атрибутов и методов
	virtual void PrepareNames() const;                         //этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual bool CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray);//вызов метода

	//Get ref class 
	virtual CLASS_ID GetTypeClass() const;

	//types 
	virtual wxString GetTypeString() const;
	virtual wxString GetString() const;

protected:
	//methods 
	CMethodHelper* m_methodHelper;
	CMetaObjectAccumulationRegister* m_metaObject;
};

#endif 