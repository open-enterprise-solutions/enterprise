#ifndef _MANAGER_DATATOOL_H__
#define _MANAGER_DATATOOL_H__

#include "dataTool.h"

class ibValueManagerDataObjectDataTool :
	public ibValueManagerDataObject {
	public:

	ibValueManagerDataObjectDataTool(const ibValueMetaObjectDataTool* metaObject = nullptr) : m_metaObject(metaObject) {
		m_members.Bind(this, &ibValueManagerDataObjectDataTool::FillManagerMethods);
	}
	virtual ~ibValueManagerDataObjectDataTool() {}

	virtual const ibValueMetaObjectSharedModule* GetManagerModule() const;
	virtual const ibValueMetaObjectDataTool* GetMetaObject() const { return m_metaObject; }

	void FillManagerMethods(ibMemberTable& helper) const;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);//method call

protected:
	const ibValueMetaObjectDataTool* m_metaObject;
private:
};

class ibValueManagerDataObjectExternalDataTool :
	public ibValueManagerObject {
	public:

	ibValueManagerDataObjectExternalDataTool() {
		m_members.Bind(this, &ibValueManagerDataObjectExternalDataTool::FillManagerMethods);
	}
	virtual ~ibValueManagerDataObjectExternalDataTool() {}

	virtual const ibValueMetaObjectDataTool* GetMetaObject() const { return nullptr; }

	void FillManagerMethods(ibMemberTable& helper) const;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);//method call
private:
};

#endif 