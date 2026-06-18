#ifndef __PROPERTY_FORM_H__
#define __PROPERTY_FORM_H__

#include "backend/propertyManager/propertyObject.h"

//base property for "form"
class BACKEND_API ibPropertyForm : public ibProperty {
	wxVariantData* CreateVariantData();
public:

	wxString GetValueAsString() const;
	wxMemoryBuffer& GetValueAsMemoryBuffer() const;
	void SetValue(const wxString& val);
	void SetValue(const wxMemoryBuffer& val);

	ibPropertyForm(ibPropertyCategory* cat, const wxString& name)
		: ibProperty(cat, name, CreateVariantData())
	{
	}

	ibPropertyForm(ibPropertyCategory* cat, const wxString& name, const wxString& label)
		: ibProperty(cat, name, label, CreateVariantData())
	{
	}

	ibPropertyForm(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString)
		: ibProperty(cat, name, label, helpString, CreateVariantData())
	{
	}

	//get property for grid 
	virtual wxObject* GetPGProperty() const {
		if (ms_propertyForm != nullptr)
			return ms_propertyForm(m_owner, m_propLabel, m_propName, m_propValue);
		return nullptr;
	}

	// set/get property data
	virtual bool SetDataValue(const ibValue& varPropVal);
	virtual bool GetDataValue(ibValue& pvarPropVal) const;

	//load & save object in control 
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;

	//copy & paste — pulls the LIVE form data (not the stored buffer)
	virtual bool CopyNodeValue(ibDataValue& value) const override;
	virtual bool PasteNodeValue(const ibDataValue& value) override;

public:

	static wxObject* (*ms_propertyForm)(ibPropertyObject*, const wxString&, const wxString&, const wxVariant&);
};

#endif