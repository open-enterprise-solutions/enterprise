#ifndef _MANAGER_REPORT_H__
#define _MANAGER_REPORT_H__

#include "dataReport.h"

class ibValueManagerDataObjectReport :
	public ibValueManagerDataObject {
	public:

	ibValueManagerDataObjectReport(const ibValueMetaObjectReport* metaObject = nullptr) : m_metaObject(metaObject) {}
	virtual ~ibValueManagerDataObjectReport() {}

	virtual const ibValueMetaObjectCommonModule* GetManagerModule() const;
	virtual const ibValueMetaObjectReport* GetMetaObject() const { return m_metaObject; }

	virtual void PrepareNames() const;                         // this method is automatically called to initialize attribute and method names.
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);//method call

protected:
	const ibValueMetaObjectReport* m_metaObject;
private:
};

class ibValueManagerDataObjectExternalReport :
	public ibValueManagerObject {
	public:

	ibValueManagerDataObjectExternalReport() {}
	virtual ~ibValueManagerDataObjectExternalReport() {}

	virtual const ibValueMetaObjectReport* GetMetaObject() const { return nullptr; }

	virtual ibValueMethodHelper* GetPMethods() const {  // get a reference to the class helper for parsing attribute and method names
		//PrepareNames(); 
		return &m_methodHelper;
	}

	virtual void PrepareNames() const;                         // this method is automatically called to initialize attribute and method names.
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);//method call

protected:
	static ibValueMethodHelper m_methodHelper;
private:
};

#endif 