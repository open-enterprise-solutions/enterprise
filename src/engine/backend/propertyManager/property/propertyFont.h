#ifndef __PROPERTY_FONT_H__
#define __PROPERTY_FONT_H__

#include "backend/propertyManager/propertyObject.h"
#include "core/graphics/font.h"   // ibFont — the font, as data

//base property for "font"
class BACKEND_API ibPropertyFont : public ibProperty {
public:

	ibFont GetValueAsFont() const { ibFont font; font.FromString(m_propValue.GetString()); return font; }
	wxString GetValueAsString() const { return m_propValue; }
	void SetValue(const ibFont& val) { m_propValue = wxString(val.ToString()); }
	void SetValue(const wxString& val) { m_propValue = val; }

	ibPropertyFont(ibPropertyCategory* cat, const wxString& name, const ibFont& f = ibFont())
		: ibProperty(cat, name, wxString(f.ToString()))
	{
	}

	ibPropertyFont(ibPropertyCategory* cat, const wxString& name, const wxString& label, const ibFont& f = ibFont())
		: ibProperty(cat, name, label, wxString(f.ToString()))
	{
	}

	ibPropertyFont(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString, const ibFont& f = ibFont())
		: ibProperty(cat, name, label, helpString, wxString(f.ToString()))
	{
	}

	// Unset — no text, or a stored one with no size.
	virtual bool IsEmptyProperty() const override { return !GetValueAsFont().IsOk(); }

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