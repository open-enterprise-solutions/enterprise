#ifndef __PROPERTY_SIZE_H__
#define __PROPERTY_SIZE_H__

#include "frmclient/backend/propertyManager/propertyObject.h"

//base property for "size"
class FRMCLIENT_API ibPropertySize : public ibProperty {
public:
	// …held as its node, the size's text (ibBackendProperty::ReadNodeValue).
	wxSize GetValueAsSize() const { return typeConv::StringToSize(GetValueAsString()); }
	wxString GetValueAsString() const;

	void SetValue(const wxSize& val) { SetValue(typeConv::SizeToString(val)); }
	void SetValue(const wxString& val);

	ibPropertySize(ibPropertyCategory* cat, const wxString& name, const wxSize& s = wxDefaultSize)
		: ibProperty(cat, name, wxNullVariant)
	{
		SetValue(s);
	}

	ibPropertySize(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxSize& s = wxDefaultSize)
		: ibProperty(cat, name, label, wxNullVariant)
	{
		SetValue(s);
	}

	ibPropertySize(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString, const wxSize& s = wxDefaultSize)
		: ibProperty(cat, name, label, helpString, wxNullVariant)
	{
		SetValue(s);
	}

	//load & save object in control

	// readable node value
	virtual bool ReadNodeValue(const ibDataValue& value) override;

public:

};

#endif