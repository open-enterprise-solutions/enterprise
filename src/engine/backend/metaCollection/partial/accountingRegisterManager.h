#ifndef _ACCOUNTING_REGISTER_MANAGER_H__
#define _ACCOUNTING_REGISTER_MANAGER_H__

#include "accountingRegister.h"

class ibValueManagerDataObjectAccountingRegister :
	public ibValueManagerDataObject {
	public:

	// ⭐ THE ARGUMENT ARRAY IS PASSED THROUGH, NOT UNPACKED HERE. This register's readings have a
	// signature that DEPENDS ON THE REGISTER: a correspondence one takes a debit account and a credit
	// account and a breakdown per side, a one-sided one takes one of each. Named C++ parameters cannot
	// express that, and the moment they try, the script's argument N and the query's argument N mean
	// different things. So both entrances read the positional array through ONE function
	// (ibAcctParseCall) against ONE layout (ibAcctArgs::For).
	ibValue Balance(ibValue** paParams, const long lSizeArray);
	ibValue Turnovers(ibValue** paParams, const long lSizeArray);
	ibValue DrCrTurnovers(ibValue** paParams, const long lSizeArray);
	ibValue BalanceAndTurnovers(ibValue** paParams, const long lSizeArray);
	// The movement lines themselves, with the slots widened into a column per requested kind.
	ibValue RecordsWithAccountDimensions(ibValue** paParams, const long lSizeArray);


	ibValueManagerDataObjectAccountingRegister(const ibValueMetaObjectAccountingRegister* metaObject = nullptr) : m_metaObject(metaObject) { m_members.Bind(this, &ibValueManagerDataObjectAccountingRegister::FillManagerMethods); }
	virtual ~ibValueManagerDataObjectAccountingRegister() {}

	virtual const ibValueMetaObjectCommonModule* GetManagerModule() const;
	virtual const ibValueMetaObjectAccountingRegister* GetMetaObject() const { return m_metaObject; }

	void FillManagerMethods(ibMemberTable& helper) const;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

protected:
	const ibValueMetaObjectAccountingRegister* m_metaObject;
private:
};

#endif
