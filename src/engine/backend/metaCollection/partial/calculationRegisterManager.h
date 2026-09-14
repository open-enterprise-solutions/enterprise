#ifndef _CALC_REGISTER_MANAGER_H__
#define _CALC_REGISTER_MANAGER_H__

#include "calculationRegister.h"

class ibValueManagerDataObjectCalculationRegister :
	public ibValueManagerDataObject {
	public:

	ibValue Get(const ibValue& cFilter = ibValue());
	ibValue Get(const ibValue& cPeriod, const ibValue& cFilter);

	// (No GetDisplacement. The actual action periods are a SOURCE of their own —
	// `CalculationRegister.<Register>.ActualActionPeriod`, one row per piece, read off the records — and a
	// manager method recomputing them was a second road to the same answer that could disagree with it.)
	//
	// (No Base<Register> source: a base is a function of records, not a table — calculationRegister.h, "The base".)

	// The base of the records the filter chooses (the recorder at least): the register reads them and asks the base.
	ibValue GetBase(const ibValue& cFilter, const ibValue& cResources, const ibValue& cDimensions, const ibValue& cSections);

	ibValueManagerDataObjectCalculationRegister(const ibValueMetaObjectCalculationRegister* metaObject = nullptr) : m_metaObject(metaObject) { m_members.Bind(this, &ibValueManagerDataObjectCalculationRegister::FillManagerMethods); }
	virtual ~ibValueManagerDataObjectCalculationRegister() {}

	virtual const ibValueMetaObjectCommonModule* GetManagerModule() const;
	virtual const ibValueMetaObjectCalculationRegister* GetMetaObject() const { return m_metaObject; }

	void FillManagerMethods(ibMemberTable& helper) const;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray); //method call

protected:
	const ibValueMetaObjectCalculationRegister* m_metaObject;
private:
};

#endif
