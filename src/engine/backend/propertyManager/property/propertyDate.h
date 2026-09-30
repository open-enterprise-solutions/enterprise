#ifndef __PROPERTY_DATE_H__
#define __PROPERTY_DATE_H__

#include "backend/propertyManager/propertyObject.h"

//base property for "date"
class BACKEND_API ibPropertyDate : public ibProperty {
public:

	// The text keeps the date's count (fdatetime.h) - all 64 bits of it: through IntToStr / StrToInt,
	// which are int, a date was cut to its low 32.
	ibDateTime GetValueAsDateTime() const { wxLongLong_t value = 0; m_propValue.GetString().ToLongLong(&value); return ibDateTime(value); }
	void SetValue(const ibDateTime& val = ibDateTime()) { m_propValue = TextOf(val); }

	ibPropertyDate(ibPropertyCategory* cat, const wxString& name,
		const ibDateTime& value = ibDateTime()) : ibProperty(cat, name, TextOf(value))
	{
	}

	ibPropertyDate(ibPropertyCategory* cat, const wxString& name, const wxString& label,
		const ibDateTime& value = ibDateTime()) : ibProperty(cat, name, label, TextOf(value))
	{
	}

	ibPropertyDate(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString,
		const ibDateTime& value = ibDateTime()) : ibProperty(cat, name, label, helpString, TextOf(value))
	{
	}

	virtual bool IsEmptyProperty() const { return GetValueAsDateTime().IsEmpty(); }

	// set/get property data
	virtual bool SetDataValue(const ibValue& varPropVal);
	virtual bool GetDataValue(ibValue& pvarPropVal) const;

	//load & save object in control

	// readable node value
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;

private:

	static wxString TextOf(const ibDateTime& date) { return wxString::Format(wxT("%lld"), date.GetValue()); }
};

#endif
