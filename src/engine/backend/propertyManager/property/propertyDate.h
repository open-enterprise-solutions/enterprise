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

	//get property for grid 
	virtual wxObject* GetPGProperty() const {
		if (ms_propertyDate != nullptr)
			return ms_propertyDate(m_propLabel, m_propName, wxDateTime(static_cast<time_t>(GetValueAsDateTime())));
		return nullptr;
	}

	// set/get property data
	virtual bool SetDataValue(const ibValue& varPropVal);
	virtual bool GetDataValue(ibValue& pvarPropVal) const;

	//load & save object in control 
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory& writer);

public:

	static wxObject* (*ms_propertyDate)(const wxString&, const wxString&, const wxDateTime&);
};

#endif