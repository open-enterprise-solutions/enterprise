#ifndef _MANAGER_CHART_OF_CHARACTERISTIC_TYPES_H__
#define _MANAGER_CHART_OF_CHARACTERISTIC_TYPES_H__

#include "chartOfCharacteristicTypes.h"

class ibValueManagerDataObjectChartOfCharacteristicTypes :
	public ibValueManagerDataObjectPredefined {
	public:

	ibValueReferenceDataObject* FindByCode(const ibValue& vCode) const;
	ibValueReferenceDataObject* FindByDescription(const ibValue& cParam) const;

	ibValueReferenceDataObject* EmptyRef() const;

	ibValueManagerDataObjectChartOfCharacteristicTypes(const ibValueMetaObjectChartOfCharacteristicTypes* metaObject = nullptr) : m_metaObject(metaObject) { m_members.Bind(this, &ibValueManagerDataObjectChartOfCharacteristicTypes::FillManagerMethods); }
	virtual ~ibValueManagerDataObjectChartOfCharacteristicTypes() {}

	virtual const ibValueMetaObjectCommonModule* GetManagerModule() const;
	virtual const ibValueMetaObjectChartOfCharacteristicTypes* GetMetaObject() const { return m_metaObject; }

	void FillManagerMethods(ibMemberTable& helper) const;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);//method call

protected:
	const ibValueMetaObjectChartOfCharacteristicTypes* m_metaObject;
private:
};

#endif
