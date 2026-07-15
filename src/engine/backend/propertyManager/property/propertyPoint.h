#ifndef __PROPERTY_POINT_H__
#define __PROPERTY_POINT_H__

#include "backend/propertyManager/propertyObject.h"

//base property for "point"
class BACKEND_API ibPropertyPoint : public ibProperty {
	wxVariantData* CreateVariantData(const wxPoint& val);
public:

	wxPoint GetValueAsPoint() const;
	wxString GetValueAsString() const { return typeConv::PointToString(GetValueAsPoint()); }

	void SetValue(const wxPoint& val) { m_propValue = CreateVariantData(val); }
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