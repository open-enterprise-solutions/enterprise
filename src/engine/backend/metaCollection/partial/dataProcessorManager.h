#ifndef _MANAGER_DATAPROCESSOR_H__
#define _MANAGER_DATAPROCESSOR_H__

#include "dataProcessor.h"

class ibValueManagerDataObjectDataProcessor :
	public ibValueManagerDataObject {
	public:

	ibValueManagerDataObjectDataProcessor(const ibValueMetaObjectDataProcessor* metaObject = nullptr) : m_metaObject(metaObject) {
		m_members.Bind(this, &ibValueManagerDataObjectDataProcessor::FillManagerMethods);
	}
	virtual ~ibValueManagerDataObjectDataProcessor() {}

	virtual const ibValueMetaObjectCommonModule* GetManagerModule() const;
	virtual const ibValueMetaObjectDataProcessor* GetMetaObject() const { return m_metaObject; }

	void FillManagerMethods(ibMemberTable& helper) const;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);//method call

protected:
	const ibValueMetaObjectDataProcessor* m_metaObject;
private:
};

class ibValueManagerDataObjectExternalDataProcessor :
	public ibValueManagerObject {
	public:

	ibValueManagerDataObjectExternalDataProcessor() {
		m_members.Bind(this, &ibValueManagerDataObjectExternalDataProcessor::FillManagerMethods);
	}
	virtual ~ibValueManagerDataObjectExternalDataProcessor() {}

	virtual const ibValueMetaObjectDataProcessor* GetMetaObject() const { return nullptr; }

	void FillManagerMethods(ibMemberTable& helper) const;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);//method call
private:
};

#endif 