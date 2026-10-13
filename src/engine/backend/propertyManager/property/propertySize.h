#ifndef __PROPERTY_SIZE_H__
#define __PROPERTY_SIZE_H__

#include "backend/propertyManager/propertyObject.h"
#include "core/graphics/geometry.h"   // ibSize — the size, as data

//base property for "size"
class BACKEND_API ibPropertySize : public ibProperty {
	static wxVariantData* CreateVariantData(const ibSize& val);
public:
	ibSize GetValueAsSize() const;
	wxString GetValueAsString() const { return GetValueAsSize().ToString(); }

	void SetValue(const ibSize& val) { m_propValue = CreateVariantData(val); }
	void SetValue(const wxString& val) { ibSize size = ibDefaultSize; size.FromString(val); SetValue(size); }

	ibPropertySize(ibPropertyCategory* cat, const wxString& name, const ibSize& s = ibDefaultSize)
		: ibProperty(cat, name, CreateVariantData(s))
	{
	}

	ibPropertySize(ibPropertyCategory* cat, const wxString& name, const wxString& label, const ibSize& s = ibDefaultSize)
		: ibProperty(cat, name, label, CreateVariantData(s))
	{
	}

	ibPropertySize(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString, const ibSize& s = ibDefaultSize)
		: ibProperty(cat, name, label, helpString, CreateVariantData(s))
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