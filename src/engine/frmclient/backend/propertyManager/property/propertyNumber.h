#ifndef __PROPERTY_NUMBER_H__
#define __PROPERTY_NUMBER_H__

#include "frmclient/backend/propertyManager/propertyObject.h"
#include "core/serialize/dataBuilder.h"   // ibDataValue — node value (inline Int for integers)

// (The engine's ibPropertyNumber — a decimal — is not here: the thin client shows a decimal, it does not hold one.)

//base property for "integer"
class FRMCLIENT_API ibPropertyInteger : public ibProperty {
	static wxVariant CreateVariantData(const int& val) { return wxVariant((long)val); }   // WXVARIANT<int> IS wxVariant((long)v) — propgriddefs.h
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

};

//base property for "unsigned integer"
class FRMCLIENT_API ibPropertyUInteger : public ibProperty {
	static wxVariant CreateVariantData(const unsigned int& val) { return wxVariant((long)val); }
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

};

#endif