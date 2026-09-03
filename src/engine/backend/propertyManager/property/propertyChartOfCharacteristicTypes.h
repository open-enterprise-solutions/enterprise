#ifndef __PROPERTY_CHART_OF_CHARACTERISTIC_TYPES_H__
#define __PROPERTY_CHART_OF_CHARACTERISTIC_TYPES_H__

#include "backend/propertyManager/propertyObject.h"
#include "backend/backend_type.h"

//base property for "chart of characteristic types" selection
class BACKEND_API ibPropertyChartOfCharacteristicTypes : public ibProperty {
	wxVariantData* CreateVariantData(ibPropertyObject* property, const ibMetaDescription& typeDesc = ibMetaDescription()) const;
public:

	ibMetaDescription& GetValueAsMetaDesc() const;
	void SetValue(const ibMetaDescription& val);

	// WHICH CHART OF CHARACTERISTIC TYPES. The comment above already says "exactly one, by what it
	// is, not by a setting" — this is that same sentence in a form the gate can read.
	virtual ibPropertyChoiceMode GetValueList(ibPropertyChoiceList& list) override;

	ibPropertyChartOfCharacteristicTypes(ibPropertyCategory* cat, const wxString& name) : ibProperty(cat, name, CreateVariantData(cat->GetPropertyObject())) {}
	ibPropertyChartOfCharacteristicTypes(ibPropertyCategory* cat, const wxString& name, const wxString& label) : ibProperty(cat, name, label, CreateVariantData(cat->GetPropertyObject())) {}
	ibPropertyChartOfCharacteristicTypes(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString) : ibProperty(cat, name, label, helpString, CreateVariantData(cat->GetPropertyObject())) {}

	// THIS BINDING NAMES EXACTLY ONE CHART — always, by what it is, not by a setting.
	//
	// A type may legitimately be composite; a binding may not. An owner keeps its analytics in ONE
	// chart of characteristic types, and naming two would leave "whose composition types the value
	// slots" unanswerable. There is no case where several are wanted, so there is no switch: the
	// editor opens as a single choice because that is what the property IS.

	// NOTHING CHOSEN — asked of the property, not counted at the callsite. A binding that names no
	// chart is empty in the ordinary sense every other property means by it, so the rules that refuse
	// an unbound chart of accounts read like every other emptiness check in the codebase instead of
	// reaching through to a type count.
	virtual bool IsEmptyProperty() const override { return GetValueAsMetaDesc().GetTypeCount() == 0; }

	// set/get property data
	virtual bool SetDataValue(const ibValue& varPropVal);
	virtual bool GetDataValue(ibValue& pvarPropVal) const;

	//load & save object in control
protected:

	// The family rule, kept here too: a relationship arrives in whichever wrapper the caller was
	// handed - CreateValueList builds every candidate as ibVariantDataOwner - and a property that
	// stores a neighbour's wrapper raises on every later read. See propertyRecord.h.
	virtual void DoSetValue(const wxVariant& val) override;

public:

	// readable node value
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;

public:

};

#endif
