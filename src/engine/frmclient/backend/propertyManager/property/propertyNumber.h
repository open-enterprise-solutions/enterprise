#ifndef __PROPERTY_NUMBER_H__
#define __PROPERTY_NUMBER_H__

#include "frmclient/backend/propertyManager/property/propertySerializable.h"
#include "core/serialize/dataBuilder.h"   // ibDataValue — node value (inline Int for integers)

// (The engine's ibPropertyNumber — a decimal — is not here: the thin client shows a decimal, it does not hold one.)

//base property for "integer"
class FRMCLIENT_API ibPropertyInteger : public ibPropertySerializable {
public:

	// …held as its node (ibPropertySerializable::ReadNodeValue).
	void SetValue(const int& val) { ibPropertySerializable::ReadNodeValue(ibDataValue::Int(val)); }
	int GetValueAsInteger() const { return static_cast<int>(GetNodeValue().AsInt()); }

	ibPropertyInteger(ibPropertyCategory* cat, const ibString& name,
		const int& value = 0) : ibPropertySerializable(cat, name, wxNullVariant)
	{
		SetValue(value);
	}

	ibPropertyInteger(ibPropertyCategory* cat, const ibString& name, const ibString& label,
		const int& value = 0) : ibPropertySerializable(cat, name, label, wxNullVariant)
	{
		SetValue(value);
	}

	ibPropertyInteger(ibPropertyCategory* cat, const ibString& name, const ibString& label, const ibString& helpString,
		const int& value = 0) : ibPropertySerializable(cat, name, label, helpString, wxNullVariant)
	{
		SetValue(value);
	}

	virtual bool IsEmptyProperty() const { return GetValueAsInteger() == 0; }

	//per-type node value
	virtual bool ReadNodeValue(const ibDataValue& value) override {
		SetValue((int)value.AsInt());
		return true;
	}

public:

};

//base property for "unsigned integer"
class FRMCLIENT_API ibPropertyUInteger : public ibPropertySerializable {
public:

	// …held as its node (ibPropertySerializable::ReadNodeValue).
	void SetValue(const unsigned int& val) { ibPropertySerializable::ReadNodeValue(ibDataValue::UInt(val)); }
	unsigned int GetValueAsUInteger() const { return static_cast<unsigned int>(GetNodeValue().AsUInt()); }

	ibPropertyUInteger(ibPropertyCategory* cat, const ibString& name,
		const unsigned int& value = 0) : ibPropertySerializable(cat, name, wxNullVariant)
	{
		SetValue(value);
	}

	ibPropertyUInteger(ibPropertyCategory* cat, const ibString& name, const ibString& label,
		const unsigned int& value = 0) : ibPropertySerializable(cat, name, label, wxNullVariant)
	{
		SetValue(value);
	}

	ibPropertyUInteger(ibPropertyCategory* cat, const ibString& name, const ibString& label, const ibString& helpString,
		const unsigned int& value = 0) : ibPropertySerializable(cat, name, label, helpString, wxNullVariant)
	{
		SetValue(value);
	}

	virtual bool IsEmptyProperty() const { return GetValueAsUInteger() == 0; }

	//per-type node value
	virtual bool ReadNodeValue(const ibDataValue& value) override {
		SetValue((unsigned int)value.AsInt());
		return true;
	}

public:

};

#endif