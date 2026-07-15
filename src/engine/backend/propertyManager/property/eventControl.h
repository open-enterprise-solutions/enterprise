#ifndef __EVENT_CONTROL_H__
#define __EVENT_CONTROL_H__

#include "backend/propertyManager/propertyObject.h"

//base property for "event"
class BACKEND_API ibEventControl : public ibEvent {
public:

	ibEventControl(ibPropertyCategory* cat, const wxString& name, const wxArrayString& args)
		: ibEvent(cat, name, args, wxEmptyString)
	{
	}

	ibEventControl(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxArrayString& args)
		: ibEvent(cat, name, label, args, wxEmptyString)
	{
	}

	ibEventControl(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString, const wxArrayString& args)
		: ibEvent(cat, name, label, helpString, args, wxEmptyString)
	{
	}

	virtual bool IsEmptyProperty() const { return m_propValue.GetString().IsEmpty(); }

	// set/get property data
	virtual bool SetDataValue(const ibValue& varPropVal);
	virtual bool GetDataValue(ibValue& pvarPropVal) const;

	//load & save object in control 
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;

public:

};

#endif