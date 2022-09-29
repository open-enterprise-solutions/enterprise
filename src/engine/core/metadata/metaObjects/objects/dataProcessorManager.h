#ifndef _MANAGER_DATAPROCESSOR_H__
#define _MANAGER_DATAPROCESSOR_H__

#include "dataProcessor.h"
#include "common/managerInfo.h"

class CDataProcessorManager : public CValue,
	public IMetaManagerInfo {
	wxDECLARE_DYNAMIC_CLASS(CDataProcessorManager);
public:

	virtual CMetaCommonModuleObject* GetModuleManager() const;

	CDataProcessorManager(CMetaObjectDataProcessor* metaObject = NULL);
	virtual ~CDataProcessorManager();

	virtual CMethods* GetPMethods() const { PrepareNames(); return m_methods; } //получить ссылку на класс помощник разбора имен атрибутов и методов
	virtual void PrepareNames() const;                         //этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual CValue Method(methodArg_t& aParams);//вызов метода

	//Get ref class 
	virtual CLASS_ID GetClassType() const;

	//types 
	virtual wxString GetTypeString() const;
	virtual wxString GetString() const;

protected:

	CMethods* m_methods;
	CMetaObjectDataProcessor* m_metaObject;
};

class CManagerExternalDataProcessorValue : public CValue {
	wxDECLARE_DYNAMIC_CLASS(CManagerExternalDataProcessorValue);
public:
	CManagerExternalDataProcessorValue();
	virtual ~CManagerExternalDataProcessorValue();

	virtual CMethods* GetPMethods() const { PrepareNames(); return m_methods; } //получить ссылку на класс помощник разбора имен атрибутов и методов
	virtual void PrepareNames() const;                         //этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual CValue Method(methodArg_t& aParams);//вызов метода

	//types 
	virtual wxString GetTypeString() const;
	virtual wxString GetString() const;

protected:
	CMethods* m_methods;
};

#endif 