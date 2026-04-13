#ifndef _MANAGER_CHART_OF_CHARACTERISTIC_TYPES_H__
#define _MANAGER_CHART_OF_CHARACTERISTIC_TYPES_H__

#include "chartOfCharacteristicTypes.h"

class ibValueManagerDataObjectChartOfCharacteristicTypes :
	public ibValueManagerDataObjectPredefined {
public:

	ibValueReferenceDataObject* FindByCode(const ibValue& vCode) const;
	ibValueReferenceDataObject* FindByDescription(const ibValue& cParam) const;

	ibValueReferenceDataObject* EmptyRef() const;

	ibValueManagerDataObjectChartOfCharacteristicTypes(ibValueMetaObjectChartOfCharacteristicTypes* metaObject = nullptr) : m_metaObject(metaObject) {}
	virtual ~ibValueManagerDataObjectChartOfCharacteristicTypes() {}

	virtual ibValueMetaObjectCommonModule* GetModuleManager() const;
	virtual ibValueMetaObjectChartOfCharacteristicTypes* GetMetaObject() const { return m_metaObject; }

	virtual void PrepareNames() const; // this method is automatically called to initialize attribute and method names.
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);//method call

protected:
	ibValueMetaObjectChartOfCharacteristicTypes* m_metaObject;
private:
	wxDECLARE_DYNAMIC_CLASS(ibValueManagerDataObjectChartOfCharacteristicTypes);
};

#endif
