#ifndef _ENUMERATION_MANAGER_H__
#define _ENUMERATION_MANAGER_H__

#include "enumeration.h"
#include "core/common/managerInfo.h"

class CEnumerationManager : public CValue,
	public IMetaManagerInfo {
	wxDECLARE_DYNAMIC_CLASS(CEnumerationManager);
public:

	CEnumerationManager(CMetaObjectEnumeration *metaObject = NULL);
	virtual ~CEnumerationManager();

	virtual CMetaCommonModuleObject *GetModuleManager() const;

	virtual CMethodHelper* GetPMethods() const { PrepareNames(); return m_methodHelper; } //получить ссылку на класс помощник разбора имен атрибутов и методов
	virtual void PrepareNames() const;                         //этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual bool CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray);//вызов метода

	virtual bool SetPropVal(const long lPropNum, CValue &varPropVal);        //установка атрибута
	virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);                   //значение атрибута

	//Get ref class 
	virtual CLASS_ID GetTypeClass() const;

	//types 
	virtual wxString GetTypeString() const;
	virtual wxString GetString() const;

protected:
	//methods 
	CMethodHelper *m_methodHelper;
	CMetaObjectEnumeration *m_metaObject;
};

#endif 