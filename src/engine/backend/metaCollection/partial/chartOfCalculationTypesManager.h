#ifndef _MANAGER_CHART_OF_CALCULATION_TYPES_H__
#define _MANAGER_CHART_OF_CALCULATION_TYPES_H__

#include "chartOfCalculationTypes.h"

class ibValueManagerDataObjectChartOfCalculationTypes :
	public ibValueManagerDataObjectPredefined {
	public:

	ibValueReferenceDataObject* EmptyRef() const;

	ibValueManagerDataObjectChartOfCalculationTypes(const ibValueMetaObjectChartOfCalculationTypes* metaObject = nullptr) : m_metaObject(metaObject) { m_members.Bind(this, &ibValueManagerDataObjectChartOfCalculationTypes::FillManagerMethods); }
	virtual ~ibValueManagerDataObjectChartOfCalculationTypes() {}

	virtual const ibValueMetaObjectCommonModule* GetManagerModule() const;
	virtual const ibValueMetaObjectChartOfCalculationTypes* GetMetaObject() const { return m_metaObject; }

	void FillManagerMethods(ibMemberTable& helper) const;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);//method call

protected:
	const ibValueMetaObjectChartOfCalculationTypes* m_metaObject;
private:
};

#endif
