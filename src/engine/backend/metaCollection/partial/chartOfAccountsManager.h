#ifndef _MANAGER_CHART_OF_ACCOUNTS_H__
#define _MANAGER_CHART_OF_ACCOUNTS_H__

#include "chartOfAccounts.h"

class ibValueManagerDataObjectChartOfAccounts :
	public ibValueManagerDataObjectPredefined {
public:

	ibValueReferenceDataObject* FindByCode(const ibValue& vCode) const;
	ibValueReferenceDataObject* FindByDescription(const ibValue& cParam) const;

	ibValueReferenceDataObject* EmptyRef() const;

	ibValueManagerDataObjectChartOfAccounts(ibValueMetaObjectChartOfAccounts* metaObject = nullptr) : m_metaObject(metaObject) {}
	virtual ~ibValueManagerDataObjectChartOfAccounts() {}

	virtual ibValueMetaObjectCommonModule* GetModuleManager() const;
	virtual ibValueMetaObjectChartOfAccounts* GetMetaObject() const { return m_metaObject; }

	virtual void PrepareNames() const;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);

protected:
	ibValueMetaObjectChartOfAccounts* m_metaObject;
private:
	wxDECLARE_DYNAMIC_CLASS(ibValueManagerDataObjectChartOfAccounts);
};

#endif
