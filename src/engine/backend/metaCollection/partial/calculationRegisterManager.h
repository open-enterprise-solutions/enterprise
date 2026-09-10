#ifndef _CALC_REGISTER_MANAGER_H__
#define _CALC_REGISTER_MANAGER_H__

#include "calculationRegister.h"

class ibValueManagerDataObjectCalculationRegister :
	public ibValueManagerDataObject {
	public:

	ibValue Get(const ibValue& cFilter = ibValue());
	ibValue Get(const ibValue& cPeriod, const ibValue& cFilter);

	// (No GetDisplacement. The actual action periods are a SOURCE of their own —
	// `CalculationRegister.<Register>.ActualActionPeriod`, one row per piece, kept by the write — and a
	// manager method recomputing them was a second road to the same answer that could disagree with it.)

	// GetBase(BaseRegister, Filter) — the proportional-by-period base. For each filtered record of THIS
	// (dependent) register it sums, per base-register resource, that resource weighted by how much of each
	// base record lies inside the dependent record's BASE period: value * overlap / total. Which period of
	// the base record that is, the dependent register's chart says (BaseDependence): its ACTUAL action
	// period (after displacement in the base register), or its registration period, counted whole.
	// A chart that takes no base refuses. A base record counts only when its
	// calculation type is named in the Base section of the dependent record's type, and it is matched to
	// the dependent record by shared-name dimension VALUES (the main<->base dimension mapping). The filter
	// may name any field of the records (ibRegFilterOver::Records) — the recorder above all. Returns a
	// value table = the dependent record's own attributes plus one "Base<Resource>" column per base
	// resource. Computed by the DATABASE, one statement over the dependent records, the chart's Base
	// section and the base register's actual pieces — the base register is never read whole.
	ibValue GetBase(const ibValue& cBaseRegister, const ibValue& cFilter = ibValue());

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
