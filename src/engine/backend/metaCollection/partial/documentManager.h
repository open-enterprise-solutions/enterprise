#ifndef _MANAGER_DOCUMENT_H__
#define _MANAGER_DOCUMENT_H__

#include "document.h"
#include "backend/wrapper/managerInfo.h"

class CDocumentManager : public CValue,
	public IMetaManagerInfo {
	wxDECLARE_DYNAMIC_CLASS(CDocumentManager);

public:

	CDocumentManager(CMetaObjectDocument* metaObject = nullptr);
	virtual ~CDocumentManager();

	virtual CMetaObjectCommonModule* GetModuleManager() const;

	CReferenceDataObject* FindByNumber(const CValue& vCode, const CValue& vPeriod);
	CReferenceDataObject* EmptyRef();

	virtual CMethodHelper* GetPMethods() const { //получить ссылку на класс помощник разбора имен атрибутов и методов
		//PrepareNames(); 
		return m_methodHelper;
	}
	virtual void PrepareNames() const; //этот метод автоматически вызывается для инициализации имен атрибутов и методов
	virtual bool CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray);//вызов метода

	//Get ref class 
	virtual class_identifier_t GetClassType() const;

	//types 
	virtual wxString GetClassName() const;
	virtual wxString GetString() const;

protected:

	//methods 
	CMethodHelper* m_methodHelper;
	CMetaObjectDocument* m_metaObject;
};

#endif 