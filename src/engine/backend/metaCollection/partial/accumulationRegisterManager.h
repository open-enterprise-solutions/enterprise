#ifndef _ACC_REGISTER_MANAGER_H__
#define _ACC_REGISTER_MANAGER_H__

#include "accumulationRegister.h"

class ibValueManagerDataObjectAccumulationRegister :
	public ibValueManagerDataObject {
	public:

	ibValue Balance(const ibValue& cPeriod, const ibValue& cFilter = ibValue());
	ibValue Turnovers(const ibValue& cBeginOfPeriod, const ibValue& cEndOfPeriod, const ibValue& cFilter = ibValue());

	ibValueManagerDataObjectAccumulationRegister(const ibValueMetaObjectAccumulationRegister* metaObject = nullptr) : m_metaObject(metaObject) { m_members.Bind(this, &ibValueManagerDataObjectAccumulationRegister::FillManagerMethods); }
	virtual ~ibValueManagerDataObjectAccumulationRegister() {}

	virtual const ibValueMetaObjectCommonModule* GetManagerModule() const;
	virtual const ibValueMetaObjectAccumulationRegister* GetMetaObject() const { return m_metaObject; }

	void FillManagerMethods(ibMemberTable& helper) const;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);//method call

protected:
	const ibValueMetaObjectAccumulationRegister* m_metaObject;
private:
};

#endif 