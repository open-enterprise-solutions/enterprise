#ifndef __PROPERTY_BOOLEAN_H__
#define __PROPERTY_BOOLEAN_H__

#include "frmclient/backend/propertyManager/propertyObject.h"

//base property for "bool"
class FRMCLIENT_API ibPropertyBoolean : public ibProperty {
public:

	// …held as its node (ibBackendProperty::ReadNodeValue).
	bool GetValueAsBoolean() const;
	void SetValue(const bool boolean);

	ibPropertyBoolean(ibPropertyCategory* cat, const wxString& name,
		const bool& value = false) : ibProperty(cat, name, wxNullVariant)
	{
		SetValue(value);
	}

	ibPropertyBoolean(ibPropertyCategory* cat, const wxString& name, const wxString& label,
		const bool& value = false) : ibProperty(cat, name, label, wxNullVariant)
	{
		SetValue(value);
	}

	ibPropertyBoolean(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString,
		const bool& value = false) : ibProperty(cat, name, label, helpString, wxNullVariant)
	{
		SetValue(value);
	}

	//load & save object in control

	// readable node value (typed Bool, not opaque Binary) — a flag spelled as a word too
	virtual bool ReadNodeValue(const ibDataValue& value) override;

public:

};

#endif