#ifndef _MANAGER_DOCUMENT_H__
#define _MANAGER_DOCUMENT_H__

#include "document.h"
#include "common/managerInfo.h"

class CManagerDocumentValue : public CValue,
	public IMetaManagerInfo {
	wxDECLARE_DYNAMIC_CLASS(CManagerDocumentValue);

public:

	CManagerDocumentValue(CMetaObjectDocumentValue *metaObject = NULL);
	virtual ~CManagerDocumentValue();

	virtual CMetaCommonModuleObject *GetModuleManager() const;

	CValue FindByNumber(const CValue &vCode, const CValue &vPeriod);
	CValue EmptyRef();

	virtual CMethods* GetPMethods() const { PrepareNames(); return m_methods; }; //получить ссылку на класс помощник разбора имен атрибутов и методов
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
	CMetaObjectDocumentValue *m_metaObject;
};

#endif 