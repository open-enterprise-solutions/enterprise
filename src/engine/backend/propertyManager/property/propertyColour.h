#ifndef __PROPERTY_COLOUR_H__
#define __PROPERTY_COLOUR_H__

#include "backend/propertyManager/propertyObject.h"
#include "core/graphics/colour.h"   // ibColour — the colour, as data

// Base property for "colour" — kept as the colour's text (`r,g,b`, ibColour's), as a font is kept as its own.
class BACKEND_API ibPropertyColour : public ibProperty {
	static ibVariant CreateVariantData(const ibColour& val) { return ibVariant(ibString(val.ToString())); }
public:

	ibColour GetValueAsColour() const {
		ibColour colour;
		colour.FromString(m_propValue.GetString());
		return colour;
	}

	ibString GetValueAsString() const { return m_propValue.GetString(); }

	void SetValue(const ibColour& val) { ibProperty::SetValue(CreateVariantData(val)); }
	void SetValue(const ibString& val) { ibColour colour; colour.FromString(val); SetValue(colour); }

	ibPropertyColour(ibPropertyCategory* cat, const ibString& name, const ibColour& c = ibColour())
		: ibProperty(cat, name, CreateVariantData(c))
	{
	}

	ibPropertyColour(ibPropertyCategory* cat, const ibString& name, const ibString& label, const ibColour& c = ibColour())
		: ibProperty(cat, name, label, CreateVariantData(c))
	{
	}

	ibPropertyColour(ibPropertyCategory* cat, const ibString& name, const ibString& label, const ibString& helpString, const ibColour& c = ibColour())
		: ibProperty(cat, name, label, helpString, CreateVariantData(c))
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