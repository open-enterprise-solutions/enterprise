#ifndef _MANAGER_DATAPROCESSOR_H__
#define _MANAGER_DATAPROCESSOR_H__

#include "dataProcessor.h"
#include "core/common/managerInfo.h"

class CDataProcessorManager : public CValue,
	public IMetaManagerInfo {
	wxDECLARE_DYNAMIC_CLASS(CDataProcessorManager);
public:

	virtual CMetaCommonModuleObject* GetModuleManager() const;

	CDataProcessorManager(CMetaObjectDataProcessor* metaObject = NULL);
	virtual ~CDataProcessorManager();

	virtual CMethodHelper* GetPMethods() const { PrepareNames(); return m_methodHelper; } //получить ссылку на класс помощник разбора имен атрибутов и методов
	virtual void PrepareNames() const;                         //этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual bool CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray);//вызов метода

	//Get ref class 
	virtual CLASS_ID GetTypeClass() const;

	//types 
	virtual wxString GetTypeString() const;
	virtual wxString GetString() const;

protected:

	CMethodHelper* m_methodHelper;
	CMetaObjectDataProcessor* m_metaObject;
};

class CManagerExternalDataProcessorValue : public CValue {
	wxDECLARE_DYNAMIC_CLASS(CManagerExternalDataProcessorValue);
public:
	CManagerExternalDataProcessorValue();
	virtual ~CManagerExternalDataProcessorValue();

	virtual CMethodHelper* GetPMethods() const { PrepareNames(); return m_methodHelper; } //получить ссылку на класс помощник разбора имен атрибутов и методов
	virtual void PrepareNames() const;                         //этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual bool CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray);//вызов метода

	//types 
	virtual wxString GetTypeString() const;
	virtual wxString GetString() const;

protected:
	CMethodHelper* m_methodHelper;
};

#endif 