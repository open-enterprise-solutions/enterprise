#ifndef __PROPERTY_FUNCTIONAL_OPTIONS_H__
#define __PROPERTY_FUNCTIONAL_OPTIONS_H__

#include "backend/propertyManager/propertyObject.h"
#include "backend/backend_type.h"

// FUNCTIONAL OPTIONS OF AN ELEMENT — the options an element of a form is available under (its *User
// visibility* group).
//
// An element names its options itself: a group, a decoration, a button that runs a procedure, a page
// have no member of the metadata to ask, and one bound to a field answers its field AND this list.
// Empty = available whatever the options are; otherwise available while ANY of them is on — the same rule
// a member of the metadata is available by (functionalOptionGate.h). It sits beside the element's Visible
// and is ANDed with it, never written into it.
//
// Edited by the same dialog every metaobject binding uses (advpropChartBinding): the options of the
// configuration, any set of them — or ONE, for what an option itself requires.
class BACKEND_API ibPropertyFunctionalOptions : public ibProperty {
	static wxVariantData* CreateVariantData(ibPropertyObject* property, const ibMetaDescription& metaDesc = ibMetaDescription());
public:

	ibMetaDescription& GetValueAsMetaDesc() const;
	void SetValue(const ibMetaDescription& val);

	virtual ibPropertyChoiceMode GetValueList(ibPropertyChoiceList& list) override;

	// Whether the element this list is on is available: false when every option it names is off in this
	// base (its own value, or one it requires). An empty list leaves it available, and so does an option the
	// configuration no longer has (functionalOptionGate.h).
	bool IsAvailable() const;

	ibPropertyFunctionalOptions(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString)
		: ibProperty(cat, name, label, helpString, CreateVariantData(cat->GetPropertyObject())) {}
	// …or ONE option — what an option itself requires (metaFunctionalOptionObject.h).
	ibPropertyFunctionalOptions(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString, ibPropertyChoiceMode mode)
		: ibProperty(cat, name, label, helpString, CreateVariantData(cat->GetPropertyObject())), m_choiceMode(mode) {}

	virtual bool IsEmptyProperty() const override { return GetValueAsMetaDesc().GetTypeCount() == 0; }

	// set/get property data
	virtual bool SetDataValue(const ibValue& varPropVal);
	virtual bool GetDataValue(ibValue& pvarPropVal) const;

protected:

	// The family rule — see propertyRecord.h.
	virtual void DoSetValue(const wxVariant& val) override;

public:

	// readable node value
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;

private:

	ibPropertyChoiceMode m_choiceMode = ibPropertyChoiceMode::Mult;
};

#endif
