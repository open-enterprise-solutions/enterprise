#ifndef __PROPERTY_COLOUR_H__
#define __PROPERTY_COLOUR_H__

#include "backend/propertyManager/propertyObject.h"

//base property for "colour"
class BACKEND_API ibPropertyColour : public ibProperty {
	wxVariant CreateVariantData(const wxColour& val) const {
		wxVariant newValue;
		newValue << val;
		return newValue;
	}
public:

	wxColour GetValueAsColour() const {
		wxColour colour;
		colour << m_propValue;
		return colour;
	}

	wxString GetValueAsString() const { return typeConv::ColourToString(GetValueAsColour()); }

	void SetValue(const wxColour& val) { ibProperty::SetValue(CreateVariantData(val)); }
	void SetValue(const wxString& val) { SetValue(typeConv::StringToColour(val)); }

	ibPropertyColour(ibPropertyCategory* cat, const wxString& name, const wxColour& c = wxNullColour)
		: ibProperty(cat, name, CreateVariantData(c))
	{
	}

	ibPropertyColour(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxColour& c = wxNullColour)
		: ibProperty(cat, name, label, CreateVariantData(c))
	{
	}

	ibPropertyColour(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString, const wxColour& c = wxNullColour)
		: ibProperty(cat, name, label, helpString, CreateVariantData(c))
	{
	}

	// set/get property data
	virtual bool SetDataValue(const ibValue& varPropVal);
	virtual bool GetDataValue(ibValue& pvarPropVal) const;

	//load & save object in control 

	// readable node value
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;

public:

};

#endif