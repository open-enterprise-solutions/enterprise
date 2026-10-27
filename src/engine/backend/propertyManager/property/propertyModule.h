#ifndef __PROPERTY_MODULE_H__
#define __PROPERTY_MODULE_H__

#include "backend/propertyManager/propertyObject.h"

//base property for "module"
class BACKEND_API ibPropertyModule : public ibProperty {
	static ibVariant CreateVariantData();
public:

	ibString GetValueAsString() const;
	void SetValue(const ibString& val);

	ibPropertyModule(ibPropertyCategory* cat, const ibString& name)
		: ibProperty(cat, name, CreateVariantData())
	{
	}

	ibPropertyModule(ibPropertyCategory* cat, const ibString& name, const ibString& label)
		: ibProperty(cat, name, label, CreateVariantData())
	{
	}

	ibPropertyModule(ibPropertyCategory* cat, const ibString& name, const ibString& label, const ibString& helpString)
		: ibProperty(cat, name, label, helpString, CreateVariantData())
	{
	}

	// set/get property data
	virtual bool SetDataValue(const ibValue& varPropVal);
	virtual bool GetDataValue(ibValue& pvarPropVal) const;

	//load & save object in control 

	// readable node value
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;

	// …and what its link opens: the module it is the text of (Object).
	virtual bool GetProtocolValue(ibDataNode& row, ibReader reader) override;

public:

};

#endif