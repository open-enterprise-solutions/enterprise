#ifndef __PROPERTY_MODULE_H__
#define __PROPERTY_MODULE_H__

#include "backend/propertyManager/propertyObject.h"

//base property for "module"
class BACKEND_API ibPropertyModule : public ibProperty {
	wxVariantData* CreateVariantData();
public:

	wxString GetValueAsString() const;
	void SetValue(const wxString& val);

	ibPropertyModule(ibPropertyCategory* cat, const wxString& name)
		: ibProperty(cat, name, CreateVariantData())
	{
	}

	ibPropertyModule(ibPropertyCategory* cat, const wxString& name, const wxString& label)
		: ibProperty(cat, name, label, CreateVariantData())
	{
	}

	ibPropertyModule(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString)
		: ibProperty(cat, name, label, helpString, CreateVariantData())
	{
	}

	//get property for grid 
	virtual wxObject* GetPGProperty() const {
		if (ms_propertyModule != nullptr)
			return ms_propertyModule(m_owner, m_propLabel, m_propName, m_propValue);
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

	static wxObject* (*ms_propertyModule)(ibPropertyObject*, const wxString&, const wxString&, const wxVariant&);
};

#endif