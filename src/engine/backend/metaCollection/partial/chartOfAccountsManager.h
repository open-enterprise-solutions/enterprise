#ifndef _MANAGER_CHART_OF_ACCOUNTS_H__
#define _MANAGER_CHART_OF_ACCOUNTS_H__

#include "chartOfAccounts.h"

class ibValueManagerDataObjectChartOfAccounts :
	public ibValueManagerDataObjectPredefined {
	public:

	ibValueReferenceDataObject* FindByCode(const ibValue& vCode) const;
	ibValueReferenceDataObject* FindByDescription(const ibValue& cParam) const;

	ibValueReferenceDataObject* EmptyRef() const;

	ibValueManagerDataObjectChartOfAccounts(const ibValueMetaObjectChartOfAccounts* metaObject = nullptr) : m_metaObject(metaObject) { m_members.Bind(this, &ibValueManagerDataObjectChartOfAccounts::FillManagerMethods); }
	virtual ~ibValueManagerDataObjectChartOfAccounts() {}

	virtual const ibValueMetaObjectCommonModule* GetManagerModule() const;
	virtual const ibValueMetaObjectChartOfAccounts* GetMetaObject() const { return m_metaObject; }

	void FillManagerMethods(ibMemberTable& helper) const;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

protected:
	const ibValueMetaObjectChartOfAccounts* m_metaObject;
private:
};

#endif
