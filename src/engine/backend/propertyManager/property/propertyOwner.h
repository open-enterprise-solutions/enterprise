#ifndef __PROPERTY_OWNER_H__
#define __PROPERTY_OWNER_H__

#include "backend/propertyManager/propertyObject.h"
#include "backend/backend_type.h"

//base property for "generation"
class BACKEND_API ibPropertyOwner : public ibProperty {
	wxVariantData* CreateVariantData(ibPropertyObject* property, const ibMetaDescription& typeDesc = ibMetaDescription()) const;
public:
	ibMetaDescription& GetValueAsMetaDesc() const;
	void SetValue(const ibMetaDescription& val);

	// WHICH METAOBJECTS MAY OWN THIS ONE — catalogs. The clsid list used to live in the FRONT, in
	// the property editor's constructor, which is why nothing headless could answer the question.
	virtual ibPropertyChoiceMode GetValueList(ibPropertyChoiceList& list) override;

	ibPropertyOwner(ibPropertyCategory* cat, const wxString& name) : ibProperty(cat, name, CreateVariantData(cat->GetPropertyObject())) {}
	ibPropertyOwner(ibPropertyCategory* cat, const wxString& name, const wxString& label) : ibProperty(cat, name, label, CreateVariantData(cat->GetPropertyObject())) {}
	ibPropertyOwner(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString) : ibProperty(cat, name, label, helpString, CreateVariantData(cat->GetPropertyObject())) {}

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