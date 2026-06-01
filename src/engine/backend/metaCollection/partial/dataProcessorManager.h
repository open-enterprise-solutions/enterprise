#ifndef _MANAGER_DATAPROCESSOR_H__
#define _MANAGER_DATAPROCESSOR_H__

#include "dataProcessor.h"

class ibValueManagerDataObjectDataProcessor :
	public ibValueManagerDataObject {
	public:

	ibValueManagerDataObjectDataProcessor(const ibValueMetaObjectDataProcessor* metaObject = nullptr) : m_metaObject(metaObject) {}
	virtual ~ibValueManagerDataObjectDataProcessor() {}

	virtual const ibValueMetaObjectCommonModule* GetManagerModule() const;
	virtual const ibValueMetaObjectDataProcessor* GetMetaObject() const { return m_metaObject; }

	virtual void PrepareNames() const;                         // this method is automatically called to initialize attribute and method names.
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);//method call

protected:
	const ibValueMetaObjectDataProcessor* m_metaObject;
private:
};

class ibValueManagerDataObjectExternalDataProcessor :
	public ibValueManagerObject {
	public:

	ibValueManagerDataObjectExternalDataProcessor() {}
	virtual ~ibValueManagerDataObjectExternalDataProcessor() {}

	virtual const ibValueMetaObjectDataProcessor* GetMetaObject() const { return nullptr; }

	virtual ibValueMethodHelper* GetPMethods() const { // get a reference to the class helper for parsing attribute and method names
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