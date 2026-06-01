#ifndef _MANAGER_CATALOG_H__
#define _MANAGER_CATALOG_H__

#include "catalog.h"

class ibValueManagerDataObjectCatalog :
	public ibValueManagerDataObjectPredefined {
	public:

	ibValueReferenceDataObject* FindByCode(const ibValue& vCode) const;
	ibValueReferenceDataObject* FindByDescription(const ibValue& cParam) const;

	ibValueReferenceDataObject* EmptyRef() const;

	ibValueManagerDataObjectCatalog(const ibValueMetaObjectCatalog* metaObject = nullptr) : m_metaObject(metaObject) {}
	virtual ~ibValueManagerDataObjectCatalog() {}

	virtual const ibValueMetaObjectCommonModule* GetManagerModule() const;
	virtual const ibValueMetaObjectCatalog* GetMetaObject() const { return m_metaObject; }

	virtual void PrepareNames() const; // this method is automatically called to initialize attribute and method names.
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);//method call

protected:
	const ibValueMetaObjectCatalog* m_metaObject;
private:
};

#endif 