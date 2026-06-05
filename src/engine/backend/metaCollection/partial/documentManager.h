#ifndef _MANAGER_DOCUMENT_H__
#define _MANAGER_DOCUMENT_H__

#include "document.h"

class ibValueManagerDataObjectDocument :
	public ibValueManagerDataObject {
	public:

	ibValueReferenceDataObject* FindByNumber(const ibValue& vCode, const ibValue& vPeriod);
	ibValueReferenceDataObject* EmptyRef();

	ibValueManagerDataObjectDocument(const ibValueMetaObjectDocument* metaObject = nullptr) : m_metaObject(metaObject) { m_members.Bind(this, &ibValueManagerDataObjectDocument::FillManagerMethods); }
	virtual ~ibValueManagerDataObjectDocument() {}

	virtual const ibValueMetaObjectCommonModule* GetManagerModule() const;
	virtual const ibValueMetaObjectDocument* GetMetaObject() const { return m_metaObject; }

	void FillManagerMethods(ibMemberTable& helper) const;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);//method call

protected:
	const ibValueMetaObjectDocument* m_metaObject;
private:
};

#endif 