#ifndef __PROPERTY_FONT_H__
#define __PROPERTY_FONT_H__

#include "frmclient/backend/propertyManager/property/propertySerializable.h"

//base property for "font"
class FRMCLIENT_API ibPropertyFont : public ibPropertySerializable {
public:

	// …held as its node, the font's text (ibPropertySerializable::ReadNodeValue).
	wxFont GetValueAsFont() const { return typeConv::StringToFont(GetValueAsString()); }
	ibString GetValueAsString() const;
	void SetValue(const wxFont& val) { SetValue(ibString(typeConv::FontToString(val))); }
	void SetValue(const ibString& val);

	ibPropertyFont(ibPropertyCategory* cat, const ibString& name, const wxFont& f = wxNullFont)
		: ibPropertySerializable(cat, name, wxNullVariant)
	{
		SetValue(f);
	}

	ibPropertyFont(ibPropertyCategory* cat, const ibString& name, const ibString& label, const wxFont& f = wxNullFont)
		: ibPropertySerializable(cat, name, label, wxNullVariant)
	{
		SetValue(f);
	}

	ibPropertyFont(ibPropertyCategory* cat, const ibString& name, const ibString& label, const ibString& helpString, const wxFont& f = wxNullFont)
		: ibPropertySerializable(cat, name, label, helpString, wxNullVariant)
	{
		SetValue(f);
	}

	// Unset — no text, or a stored one with no size.
	virtual bool IsEmptyProperty() const override { return !GetValueAsFont().IsOk(); }

	//load & save object in control 

	// readable node value
	virtual bool ReadNodeValue(const ibDataValue& value) override;

public:

};

#endif