#ifndef __PROPERTY_BOOLEAN_H__
#define __PROPERTY_BOOLEAN_H__

#include "backend/propertyManager/propertyObject.h"

//base property for "bool"
class BACKEND_API ibPropertyBoolean : public ibProperty {
public:

	bool GetValueAsBoolean() const { return m_propValue.GetBoolean(); }
	void SetValue(const bool boolean) { m_propValue = ibVariant(boolean); }

	// THE FLAG IS AN ibBoolean, not a bool: a wxString (what _() hands back) converts to a pointer and a pointer to a
	// bool, so `(name, label, help)` matched `(name, label, flag)` as well, and GCC and Clang refused the call as
	// ambiguous. An ibBoolean is one user conversion further away - out of that race.
	ibPropertyBoolean(ibPropertyCategory* cat, const ibString& name,
		ibBoolean value = false) : ibProperty(cat, name, ibVariant(static_cast<bool>(value)))
	{
	}

	ibPropertyBoolean(ibPropertyCategory* cat, const ibString& name, const ibString& label,
		ibBoolean value = false) : ibProperty(cat, name, label, ibVariant(static_cast<bool>(value)))
	{
	}

	ibPropertyBoolean(ibPropertyCategory* cat, const ibString& name, const ibString& label, const ibString& helpString,
		ibBoolean value = false) : ibProperty(cat, name, label, helpString, ibVariant(static_cast<bool>(value)))
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