#ifndef __PROPERTY_BOOLEAN_H__
#define __PROPERTY_BOOLEAN_H__

#include "frmclient/backend/propertyManager/property/propertySerializable.h"

//base property for "bool"
class FRMCLIENT_API ibPropertyBoolean : public ibPropertySerializable {
public:

	// …held as its node (ibPropertySerializable::ReadNodeValue).
	bool GetValueAsBoolean() const;
	void SetValue(const bool boolean);

	ibPropertyBoolean(ibPropertyCategory* cat, const ibString& name,
		const bool& value = false) : ibPropertySerializable(cat, name, wxNullVariant)
	{
		SetValue(value);
	}

	ibPropertyBoolean(ibPropertyCategory* cat, const ibString& name, const ibString& label,
		const bool& value = false) : ibPropertySerializable(cat, name, label, wxNullVariant)
	{
		SetValue(value);
	}

	ibPropertyBoolean(ibPropertyCategory* cat, const ibString& name, const ibString& label, const ibString& helpString,
		const bool& value = false) : ibPropertySerializable(cat, name, label, helpString, wxNullVariant)
	{
		SetValue(value);
	}

	//load & save object in control

	// readable node value (typed Bool, not opaque Binary) — a flag spelled as a word too
	virtual bool ReadNodeValue(const ibDataValue& value) override;

public:

};

#endif