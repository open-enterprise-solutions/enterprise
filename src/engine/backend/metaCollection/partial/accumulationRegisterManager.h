#ifndef _ACC_REGISTER_MANAGER_H__
#define _ACC_REGISTER_MANAGER_H__

#include "accumulationRegister.h"

class ibValueManagerDataObjectAccumulationRegister :
	public ibValueManagerDataObject {
	public:

	ibValue Balance(const ibValue& cPeriod, const ibValue& cFilter = ibValue());
	ibValue Turnovers(const ibValue& cBeginOfPeriod, const ibValue& cEndOfPeriod, const ibValue& cFilter = ibValue());

	ibValueManagerDataObjectAccumulationRegister(const ibValueMetaObjectAccumulationRegister* metaObject = nullptr) : m_metaObject(metaObject) {}
	virtual ~ibValueManagerDataObjectAccumulationRegister() {}

	virtual const ibValueMetaObjectCommonModule* GetManagerModule() const;
	virtual const ibValueMetaObjectAccumulationRegister* GetMetaObject() const { return m_metaObject; }

	virtual void PrepareNames() const;                         // this method is automatically called to initialize attribute and method names.
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);//method call

protected:
	const ibValueMetaObjectAccumulationRegister* m_metaObject;
private:
};

#endif 