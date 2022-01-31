#ifndef _MANAGER_CATALOG_H__
#define _MANAGER_CATALOG_H__

#include "catalog.h"
#include "common/managerInfo.h"

class CManagerCatalogValue : public CValue,
	public IMetaManagerInfo {
	wxDECLARE_DYNAMIC_CLASS(CManagerCatalogValue);
public:

	CManagerCatalogValue(CMetaObjectCatalogValue *metaObject = NULL);
	virtual ~CManagerCatalogValue();

	virtual CMetaCommonModuleObject *GetModuleManager() const;

	CValue FindByCode(const CValue &vCode);
	CValue FindByName(const CValue &vName);

	CValue EmptyRef();

	virtual CMethods* GetPMethods() const { PrepareNames();  return m_methods; }; //получить ссылку на класс помощник разбора имен атрибутов и методов
	virtual void PrepareNames() const;                         //этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual CValue Method(methodArg_t &aParams);//вызов метода

	//Get ref class 
	virtual CLASS_ID GetTypeID() const;

	//types 
	virtual wxString GetTypeString() const;
	virtual wxString GetString() const;

protected:
	//methos 
	CMethods *m_methods;
	CMetaObjectCatalogValue *m_metaObject;
};

#endif 