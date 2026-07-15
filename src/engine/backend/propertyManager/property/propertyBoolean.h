#ifndef __PROPERTY_BOOLEAN_H__
#define __PROPERTY_BOOLEAN_H__

#include "backend/propertyManager/propertyObject.h"

//base property for "bool"
class BACKEND_API ibPropertyBoolean : public ibProperty {
public:

	bool GetValueAsBoolean() const { return m_propValue; }
	void SetValue(const bool boolean) { m_propValue = boolean; }

	ibPropertyBoolean(ibPropertyCategory* cat, const wxString& name,
		const bool& value = false) : ibProperty(cat, name, value)
	{
	}

	ibPropertyBoolean(ibPropertyCategory* cat, const wxString& name, const wxString& label,
		const bool& value = false) : ibProperty(cat, name, label, value)
	{
	}

	ibPropertyBoolean(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString,
		const bool& value = false) : ibProperty(cat, name, label, helpString, value)
	{
	}

	// set/get property data
	virtual bool SetDataValue(const ibValue& varPropVal);
	virtual bool GetDataValue(ibValue& pvarPropVal) const;

	//load & save object in control

	// readable node value (typed Bool, not opaque Binary)
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;

public:

};

#endif