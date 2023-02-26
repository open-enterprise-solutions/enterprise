#ifndef _MANAGER_DOCUMENT_H__
#define _MANAGER_DOCUMENT_H__

#include "document.h"
#include "core/common/managerInfo.h"

class CDocumentManager : public CValue,
	public IMetaManagerInfo {
	wxDECLARE_DYNAMIC_CLASS(CDocumentManager);

public:

	CDocumentManager(CMetaObjectDocument* metaObject = NULL);
	virtual ~CDocumentManager();

	virtual CMetaCommonModuleObject* GetModuleManager() const;

	CReferenceDataObject* FindByNumber(const CValue& vCode, const CValue& vPeriod);
	CReferenceDataObject* EmptyRef();

	virtual CMethodHelper* GetPMethods() const { //получить ссылку на класс помощник разбора имен атрибутов и методов
		PrepareNames(); return m_methodHelper; 
	} 
	virtual void PrepareNames() const; //этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual bool CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray);//вызов метода

	//Get ref class 
	virtual CLASS_ID GetTypeClass() const;

	//types 
	virtual wxString GetTypeString() const;
	virtual wxString GetString() const;

protected:

	//methods 
	CMethodHelper* m_methodHelper;
	CMetaObjectDocument* m_metaObject;
};

#endif 