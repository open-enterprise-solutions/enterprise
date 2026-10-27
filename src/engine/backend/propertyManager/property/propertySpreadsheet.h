#ifndef __PROPERTY_TEMPLATE_H__
#define __PROPERTY_TEMPLATE_H__

#include "backend/propertyManager/propertyObject.h"
#include "core/description/spreadsheetDescription.h"

//base property for "spreadsheet"
class BACKEND_API ibPropertySpreadsheet : public ibProperty {
	static ibVariant CreateVariantData(const ibSpreadsheetDescription& val = ibSpreadsheetDescription());
public:

#pragma region _value_
	ibSpreadsheetDescription& GetValueAsSpreadsheetDesc() const;
	void SetValue(const ibSpreadsheetDescription& val);
#pragma endregion 

	ibPropertySpreadsheet(ibPropertyCategory* cat, const ibString& name)
		: ibProperty(cat, name, CreateVariantData())
	{
	}

	ibPropertySpreadsheet(ibPropertyCategory* cat, const ibString& name, const ibString& label)
		: ibProperty(cat, name, label, CreateVariantData())
	{
	}

	ibPropertySpreadsheet(ibPropertyCategory* cat, const ibString& name, const ibString& label, const ibString& helpString)
		: ibProperty(cat, name, label, helpString, CreateVariantData())
	{
	}

	virtual bool IsEmptyProperty() const;

	// set/get property data
	virtual bool SetDataValue(const ibValue& varPropVal);
	virtual bool GetDataValue(ibValue& pvarPropVal) const;

	//load & save object in control 
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;

	// …and what its link opens: the template it is the sheet of (Object).
	virtual bool GetProtocolValue(ibDataNode& row, ibReader reader) override;

public:

};

#endif