#ifndef __PROPERTY_GENERATION_H__
#define __PROPERTY_GENERATION_H__

#include "backend/propertyManager/propertyObject.h"
#include "backend/backend_type.h"

//base property for "generation"
class BACKEND_API ibPropertyGeneration : public ibProperty {
	wxVariantData* CreateVariantData(ibPropertyObject* property, const ibMetaDescription& typeDesc = ibMetaDescription()) const;
public:

	ibMetaDescription& GetValueAsMetaDesc() const;
	void SetValue(const ibMetaDescription& val);


	ibPropertyGeneration(ibPropertyCategory* cat, const wxString& name)
		: ibProperty(cat, name, CreateVariantData(cat->GetPropertyObject()))
	{
	}

	ibPropertyGeneration(ibPropertyCategory* cat, const wxString& name, const wxString& label)
		: ibProperty(cat, name, label, CreateVariantData(cat->GetPropertyObject()))
	{
	}

	ibPropertyGeneration(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString)
		: ibProperty(cat, name, label, helpString, CreateVariantData(cat->GetPropertyObject()))
	{
	}

	//get property for grid 
	virtual wxObject* GetPGProperty() const {
		if (ms_propertyGeneration != nullptr)
			return ms_propertyGeneration(m_owner, m_propLabel, m_propName, m_propValue);
		return nullptr;
	}

	// set/get property data
	virtual bool SetDataValue(const ibValue& varPropVal);
	virtual bool GetDataValue(ibValue& pvarPropVal) const;

	//load & save object in control 

	// readable node value
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;

public:

	static wxObject* (*ms_propertyGeneration)(ibPropertyObject*, const wxString&, const wxString&, const wxVariant&);
};

#endif