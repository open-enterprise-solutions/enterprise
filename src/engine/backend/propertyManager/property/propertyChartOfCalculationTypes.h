#ifndef __PROPERTY_CHART_OF_CALCULATION_TYPES_H__
#define __PROPERTY_CHART_OF_CALCULATION_TYPES_H__

#include "backend/propertyManager/propertyObject.h"
#include "backend/backend_type.h"

//base property for "chart of calculation types" selection
class BACKEND_API ibPropertyChartOfCalculationTypes : public ibProperty {
	wxVariantData* CreateVariantData(ibPropertyObject* property, const ibMetaDescription& typeDesc = ibMetaDescription()) const;
public:

	ibMetaDescription& GetValueAsMetaDesc() const;
	void SetValue(const ibMetaDescription& val);

	// WHICH CHARTS OF CALCULATION TYPES — and HOW MANY, which the property says rather than the picker.
	// One for a calculation register: its whole vocabulary of calculation types comes from one chart,
	// and its displacement relation is that chart's own. A set for the charts a chart's Base and Leading
	// sections may name (ibValueMetaObjectChartOfCalculationTypes::GetBaseCharts).
	virtual ibPropertyChoiceMode GetValueList(ibPropertyChoiceList& list) override;

	ibPropertyChartOfCalculationTypes(ibPropertyCategory* cat, const wxString& name) : ibProperty(cat, name, CreateVariantData(cat->GetPropertyObject())) {}
	ibPropertyChartOfCalculationTypes(ibPropertyCategory* cat, const wxString& name, const wxString& label) : ibProperty(cat, name, label, CreateVariantData(cat->GetPropertyObject())) {}
	ibPropertyChartOfCalculationTypes(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString) : ibProperty(cat, name, label, helpString, CreateVariantData(cat->GetPropertyObject())) {}
	ibPropertyChartOfCalculationTypes(ibPropertyCategory* cat, const wxString& name, const wxString& label, ibPropertyChoiceMode mode) : ibProperty(cat, name, label, CreateVariantData(cat->GetPropertyObject())), m_choiceMode(mode) {}

	// NOTHING CHOSEN — asked of the property itself, so a rule reads "is this binding empty" rather
	// than reaching for a type count. A calculation register with this empty cannot type its
	// CalculationType attribute, which is why the register refuses to apply until it is filled.
	virtual bool IsEmptyProperty() const override { return GetValueAsMetaDesc().GetTypeCount() == 0; }

	// set/get property data
	virtual bool SetDataValue(const ibValue& varPropVal);
	virtual bool GetDataValue(ibValue& pvarPropVal) const;

protected:

	// The family rule: a relationship arrives in whichever wrapper the caller was handed —
	// CreateValueList builds every candidate as ibVariantDataOwner — and a property that stores a
	// neighbour's wrapper raises on every later read. See propertyRecord.h.
	virtual void DoSetValue(const wxVariant& val) override;

public:

	// readable node value
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;

private:

	ibPropertyChoiceMode m_choiceMode = ibPropertyChoiceMode::Single;
};

#endif
