#ifndef __PROPERTY_POINT_H__
#define __PROPERTY_POINT_H__

#include "backend/propertyManager/propertyObject.h"

//base property for "size"
class BACKEND_API ibPropertyPoint : public ibProperty {
	wxVariant CreateVariantData(const wxPoint& val) const {
		wxVariant newValue;
		newValue << val;
		return newValue;
	}
public:

	wxPoint GetValueAsPoint() const {
		wxPoint point;
		point << m_propValue;
		return point;
	}
	wxString GetValueAsString() const { return typeConv::PointToString(GetValueAsPoint()); }

	void SetValue(const wxPoint& val) { ibProperty::SetValue(CreateVariantData(val)); }
	void SetValue(const wxString& val) { SetValue(typeConv::StringToPoint(val)); }

	ibPropertyPoint(ibPropertyCategory* cat, const wxString& name, const wxPoint& p = wxDefaultPosition)
		: ibProperty(cat, name, CreateVariantData(p))
	{
	}

	ibPropertyPoint(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxPoint& p = wxDefaultPosition)
		: ibProperty(cat, name, label, CreateVariantData(p))
	{
	}

	ibPropertyPoint(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString, const wxPoint& p = wxDefaultPosition)
		: ibProperty(cat, name, label, helpString, CreateVariantData(p))
	{
	}

	//get property for grid 
	virtual wxObject* GetPGProperty() const {
		if (ms_propertyPoint != nullptr)
			return ms_propertyPoint(m_propLabel, m_propName, GetValueAsPoint());
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

	static wxObject* (*ms_propertyPoint)(const wxString&, const wxString&, const wxPoint&);
};

#endif