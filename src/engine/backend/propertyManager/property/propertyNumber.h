#ifndef __PROPERTY_NUMBER_H__
#define __PROPERTY_NUMBER_H__

#include "backend/propertyManager/propertyObject.h"
#include "core/serialize/dataBuilder.h"   // ibDataValue — node value (inline Int for integers)

//base property for "number"
class BACKEND_API ibPropertyNumber : public ibProperty {
	static ibVariant CreateVariantData(const ibNumber& val);
public:

	ibNumber& GetValueAsNumber() const;
	void SetValue(const ibNumber& val);

	ibPropertyNumber(ibPropertyCategory* cat, const ibString& name,
		const ibNumber& value = 0) : ibProperty(cat, name, CreateVariantData(value))
	{
	}

	ibPropertyNumber(ibPropertyCategory* cat, const ibString& name, const ibString& label,
		const ibNumber& value = 0) : ibProperty(cat, name, label, CreateVariantData(value))
	{
	}

	ibPropertyNumber(ibPropertyCategory* cat, const ibString& name, const ibString& label, const ibString& helpString,
		const ibNumber& value = 0) : ibProperty(cat, name, label, helpString, CreateVariantData(value))
	{
	}

	virtual bool IsEmptyProperty() const { return GetValueAsNumber().IsZero(); }


	// set/get property data
	virtual bool SetDataValue(const ibValue& varPropVal);
	virtual bool GetDataValue(ibValue& pvarPropVal) const;

	//per-type node value
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;

public:

};

//base property for "integer"
class BACKEND_API ibPropertyInteger : public ibProperty {
	static ibVariant CreateVariantData(const int& val) { return ibVariant(val); }
public:

	void SetValue(const int& val) { m_propValue = CreateVariantData(val); }
	int GetValueAsInteger() const { return m_propValue.GetNumber().ToInt(); }

	ibPropertyInteger(ibPropertyCategory* cat, const ibString& name,
		const int& value = 0) : ibProperty(cat, name, CreateVariantData(value))
	{
	}

	ibPropertyInteger(ibPropertyCategory* cat, const ibString& name, const ibString& label,
		const int& value = 0) : ibProperty(cat, name, label, CreateVariantData(value))
	{
	}

	ibPropertyInteger(ibPropertyCategory* cat, const ibString& name, const ibString& label, const ibString& helpString,
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
class BACKEND_API ibPropertyUInteger : public ibProperty {
	static ibVariant CreateVariantData(const unsigned int& val) { return ibVariant(val); }
public:

	void SetValue(const unsigned int& val) { m_propValue = CreateVariantData(val); }
	unsigned int GetValueAsUInteger() const { return m_propValue.GetNumber().ToUInt(); }

	ibPropertyUInteger(ibPropertyCategory* cat, const ibString& name,
		const unsigned int& value = 0) : ibProperty(cat, name, CreateVariantData(value))
	{
	}

	ibPropertyUInteger(ibPropertyCategory* cat, const ibString& name, const ibString& label,
		const unsigned int& value = 0) : ibProperty(cat, name, label, CreateVariantData(value))
	{
	}

	ibPropertyUInteger(ibPropertyCategory* cat, const ibString& name, const ibString& label, const ibString& helpString,
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