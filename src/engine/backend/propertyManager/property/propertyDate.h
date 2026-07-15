#ifndef __PROPERTY_DATE_H__
#define __PROPERTY_DATE_H__

#include "backend/propertyManager/propertyObject.h"

//base property for "date"
class BACKEND_API ibPropertyDate : public ibProperty {
public:

	wxLongLong_t GetValueAsDateTime() const { return stringUtils::StrToInt(m_propValue); }
	void SetValue(const wxLongLong_t& val = emptyDate) { m_propValue = stringUtils::IntToStr(val); }

	ibPropertyDate(ibPropertyCategory* cat, const wxString& name,
		const wxLongLong_t& value = emptyDate) : ibProperty(cat, name, stringUtils::IntToStr(value))
	{
	}

	ibPropertyDate(ibPropertyCategory* cat, const wxString& name, const wxString& label,
		const wxLongLong_t& value = emptyDate) : ibProperty(cat, name, label, stringUtils::IntToStr(value))
	{
	}

	ibPropertyDate(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString,
		const wxLongLong_t& value = emptyDate) : ibProperty(cat, name, label, helpString, stringUtils::IntToStr(value))
	{
	}

	virtual bool IsEmptyProperty() const { return GetValueAsDateTime() == emptyDate; }

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