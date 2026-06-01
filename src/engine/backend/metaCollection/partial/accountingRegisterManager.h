#ifndef _ACCOUNTING_REGISTER_MANAGER_H__
#define _ACCOUNTING_REGISTER_MANAGER_H__

#include "accountingRegister.h"

class ibValueManagerDataObjectAccountingRegister :
	public ibValueManagerDataObject {
	public:

	ibValue Balance(const ibValue& cPeriod, const ibValue& cAccount = ibValue(), const ibValue& cFilter = ibValue());
	ibValue Turnovers(const ibValue& cBeginOfPeriod, const ibValue& cEndOfPeriod, const ibValue& cAccount = ibValue(), const ibValue& cFilter = ibValue());
	ibValue DrCrTurnovers(const ibValue& cBeginOfPeriod, const ibValue& cEndOfPeriod, const ibValue& cAccount = ibValue(), const ibValue& cFilter = ibValue());
	ibValue BalanceAndTurnovers(const ibValue& cBeginOfPeriod, const ibValue& cEndOfPeriod, const ibValue& cAccount = ibValue(), const ibValue& cFilter = ibValue());

	ibValueManagerDataObjectAccountingRegister(const ibValueMetaObjectAccountingRegister* metaObject = nullptr) : m_metaObject(metaObject) {}
	virtual ~ibValueManagerDataObjectAccountingRegister() {}

	virtual const ibValueMetaObjectCommonModule* GetManagerModule() const;
	virtual const ibValueMetaObjectAccountingRegister* GetMetaObject() const { return m_metaObject; }

	virtual void PrepareNames() const;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

protected:
	const ibValueMetaObjectAccountingRegister* m_metaObject;
private:
};

#endif
