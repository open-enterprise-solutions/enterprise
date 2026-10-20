#ifndef __PROPERTY_COLOUR_H__
#define __PROPERTY_COLOUR_H__

#include "frmclient/backend/propertyManager/propertyObject.h"

//base property for "colour"
class FRMCLIENT_API ibPropertyColour : public ibProperty {
public:

	// …held as its node, the colour's text (ibBackendProperty::ReadNodeValue).
	wxColour GetValueAsColour() const { return typeConv::StringToColour(GetValueAsString()); }
	wxString GetValueAsString() const;

	void SetValue(const wxColour& val) { SetValue(typeConv::ColourToString(val)); }
	void SetValue(const wxString& val);

	ibPropertyColour(ibPropertyCategory* cat, const wxString& name, const wxColour& c = wxNullColour)
		: ibProperty(cat, name, wxNullVariant)
	{
		SetValue(c);
	}

	ibPropertyColour(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxColour& c = wxNullColour)
		: ibProperty(cat, name, label, wxNullVariant)
	{
		SetValue(c);
	}

	ibPropertyColour(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString, const wxColour& c = wxNullColour)
		: ibProperty(cat, name, label, helpString, wxNullVariant)
	{
		SetValue(c);
	}

	//load & save object in control 

	// readable node value
	virtual bool ReadNodeValue(const ibDataValue& value) override;

public:

};

#endif