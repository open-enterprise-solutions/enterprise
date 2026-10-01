#ifndef __EVENT_CONTROL_H__
#define __EVENT_CONTROL_H__

#include "backend/propertyManager/propertyObject.h"
#include "backend/compiler/value.h"   // ibValue — ibEventControl HOLDS its dispatcher value (a named event / a lambda)

class ibEventDispatcher;   // backend/eventDispatcher.h — the facet GetDispatcher vends

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

	// THE dispatcher this control event fires through — the HELD value as its ibEventDispatcher facet: a lambda if one
	// was assigned from script, else a named-event value materialised from the stored name. Implicit upcast (both value
	// types inherit the interface) -> no cast at the fire site.
	virtual ibEventDispatcher* GetDispatcher() const override;

protected:
	// A value (name) edit drops the cached dispatcher so GetDispatcher rebuilds the named one from the new name.
	virtual void DoSetValue(const wxVariant& val) override;

	// The held dispatcher value: an ibValueEvent (named, materialised from the name) or an ibValueFunction (a lambda
	// assigned from script). Mutable — GetDispatcher materialises the named one lazily. Kept alive here for its facet.
	mutable ibValue m_dispatcherValue;
};

#endif