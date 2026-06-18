#ifndef __PROPERTY_NUMBER_H__
#define __PROPERTY_NUMBER_H__

#include "backend/propertyManager/propertyObject.h"
#include "backend/serialize/dataBuilder.h"   // ibDataValue — node value (inline Int for integers)

//base property for "number"
class BACKEND_API ibPropertyNumber : public ibProperty {
	wxVariantData* CreateVariantData(const ibNumber& val);
public:

	ibNumber& GetValueAsNumber() const;
	void SetValue(const ibNumber& val);

	ibPropertyNumber(ibPropertyCategory* cat, const wxString& name,
		const ibNumber& value = 0) : ibProperty(cat, name, CreateVariantData(value))
	{
	}

	ibPropertyNumber(ibPropertyCategory* cat, const wxString& name, const wxString& label,
		const ibNumber& value = 0) : ibProperty(cat, name, label, CreateVariantData(value))
	{
	}

	ibPropertyNumber(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString,
		const ibNumber& value = 0) : ibProperty(cat, name, label, helpString, CreateVariantData(value))
	{
	}

	virtual bool IsEmptyProperty() const { return GetValueAsNumber().IsZero(); }

	//get property for grid 
	virtual wxObject* GetPGProperty() const {
		if (ms_propertyNumber != nullptr)
			return ms_propertyNumber(m_propLabel, m_propName, GetValueAsNumber());
		return nullptr;
	};

	// set/get property data
	virtual bool SetDataValue(const ibValue& varPropVal);
	virtual bool GetDataValue(ibValue& pvarPropVal) const;

	//per-type node value
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;

public:

	static wxObject* (*ms_propertyNumber)(const wxString&, const wxString&, const ibNumber&);
};

//base property for "integer"
class BACKEND_API ibPropertyInteger : public ibProperty {
	wxVariant CreateVariantData(const int& val) const { return WXVARIANT(val); }
public:

	void SetValue(const int& val) { m_propValue = CreateVariantData(val); }
	int GetValueAsInteger() const { return m_propValue.GetLong(); }

	ibPropertyInteger(ibPropertyCategory* cat, const wxString& name,
		const int& value = 0) : ibProperty(cat, name, CreateVariantData(value))
	{
	}

	ibPropertyInteger(ibPropertyCategory* cat, const wxString& name, const wxString& label,
		const int& value = 0) : ibProperty(cat, name, label, CreateVariantData(value))
	{
	}

	ibPropertyInteger(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString,
		const int& value = 0) : ibProperty(cat, name, label, helpString, CreateVariantData(value))
	{
	}

	virtual bool IsEmptyProperty() const { return GetValueAsInteger() == 0; }

	//get property for grid 
	virtual wxObject* GetPGProperty() const {
		if (ms_propertyInteger != nullptr)
			return ms_propertyInteger(m_propLabel, m_propName, GetValueAsInteger());
		return nullptr;
	};

	// set/get property data
	virtual bool SetDataValue(const ibValue& varPropVal) {
		SetValue(varPropVal.GetInteger());
		return true;
	}
	virtual bool GetDataValue(ibValue& pvarPropVal) const {
		pvarPropVal = GetValueAsInteger();
		return true;
	}

	//per-type node value
	virtual bool ReadNodeValue(const ibDataValue& value) override {
		SetValue((int)value.AsInt());
		return true;
	}
	virtual bool WriteNodeValue(ibDataValue& value) const override {
		value = ibDataValue::Int(GetValueAsInteger());
		return true;
	}

public:

	static wxObject* (*ms_propertyInteger)(const wxString&, const wxString&, const int&);
};

//base property for "unsigned integer"
class BACKEND_API ibPropertyUInteger : public ibProperty {
	wxVariant CreateVariantData(const unsigned int& val) const { return WXVARIANT((long)val); }
public:

	void SetValue(const unsigned int& val) { m_propValue = CreateVariantData(val); }
	unsigned int GetValueAsUInteger() const { return m_propValue.GetLong(); }

	ibPropertyUInteger(ibPropertyCategory* cat, const wxString& name,
		const unsigned int& value = 0) : ibProperty(cat, name, CreateVariantData(value))
	{
	}

	ibPropertyUInteger(ibPropertyCategory* cat, const wxString& name, const wxString& label,
		const unsigned int& value = 0) : ibProperty(cat, name, label, CreateVariantData(value))
	{
	}

	ibPropertyUInteger(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString,
		const unsigned int& value = 0) : ibProperty(cat, name, label, helpString, CreateVariantData(value))
	{
	}

	virtual bool IsEmptyProperty() const { return GetValueAsUInteger() == 0; }

	//get property for grid 
	virtual wxObject* GetPGProperty() const {
		if (ms_propertyUInteger != nullptr)
			return ms_propertyUInteger(m_propLabel, m_propName, GetValueAsUInteger());
		return nullptr;
	};

	// set/get property data
	virtual bool SetDataValue(const ibValue& varPropVal) {
		SetValue(varPropVal.GetUInteger());
		return true;
	}

	virtual bool GetDataValue(ibValue& pvarPropVal) const {
		pvarPropVal = GetValueAsUInteger();
		return true;
	}

	//per-type node value
	virtual bool ReadNodeValue(const ibDataValue& value) override {
		SetValue((unsigned int)value.AsInt());
		return true;
	}
	virtual bool WriteNodeValue(ibDataValue& value) const override {
		value = ibDataValue::Int(GetValueAsUInteger());
		return true;
	}

public:

	static wxObject* (*ms_propertyUInteger)(const wxString&, const wxString&, const unsigned int&);
};

#endif