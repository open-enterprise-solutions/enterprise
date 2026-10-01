#ifndef _MANAGER_REPORT_H__
#define _MANAGER_REPORT_H__

#include "dataReport.h"

class ibValueManagerDataObjectReport :
	public ibValueManagerDataObject {
	public:

	ibValueManagerDataObjectReport(const ibValueMetaObjectReport* metaObject = nullptr) : m_metaObject(metaObject) {
		m_members.Bind(this, &ibValueManagerDataObjectReport::FillManagerMethods);
	}
	virtual ~ibValueManagerDataObjectReport() {}

	virtual const ibValueMetaObjectCommonModule* GetManagerModule() const;
	virtual const ibValueMetaObjectReport* GetMetaObject() const { return m_metaObject; }

	void FillManagerMethods(ibMemberTable& helper) const;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);//method call

protected:
	const ibValueMetaObjectReport* m_metaObject;
private:
};

class ibValueManagerDataObjectExternalReport :
	public ibValueManagerObject {
	public:

	ibValueManagerDataObjectExternalReport() {
		m_members.Bind(this, &ibValueManagerDataObjectExternalReport::FillManagerMethods);
	}
	virtual ~ibValueManagerDataObjectExternalReport() {}

	virtual const ibValueMetaObjectReport* GetMetaObject() const { return nullptr; }

	void FillManagerMethods(ibMemberTable& helper) const;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);//method call
private:
};

#endif 