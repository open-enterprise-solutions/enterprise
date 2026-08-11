#ifndef _ACC_REGISTER_MANAGER_H__
#define _ACC_REGISTER_MANAGER_H__

#include "accumulationRegister.h"

class ibValueManagerDataObjectAccumulationRegister :
	public ibValueManagerDataObject {
	public:

	// ⭐ THE SAME ARGUMENTS THE VIRTUAL TABLE TAKES, in the same order — this is the OTHER entrance to
	// one table, not a second, smaller API. The period comes first and the filter closes the call
	// (ibRegBalanceArg / ibRegTurnoverArg carry that order, asserted there).
	//
	// `Turnovers` used to stop at three: a script could not ask for a periodicity that the query side
	// had offered for a while, so the same table answered differently depending on which door was used.
	ibValue Balance(const ibValue& cPeriod, const ibValue& cFilter = ibValue());
	ibValue Turnovers(const ibValue& cBeginOfPeriod, const ibValue& cEndOfPeriod,
	                  const ibValue& cFilter = ibValue(), const ibValue& cPeriodicity = ibValue());

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