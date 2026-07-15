#ifndef __PROPERTY_DYNAMIC_LIST_H__
#define __PROPERTY_DYNAMIC_LIST_H__

#include "backend/propertyManager/propertyObject.h"

// ---------------------------------------------------------------------------
// ibPropertyDynamicList — backend half of the "dynamic list settings" designer
// property. Pure action property (no stored scalar of its own): it surfaces a
// single "Settings…" action on whatever owner created it (a form attribute
// whose Type is a dynamic list). It mirrors ibPropertyForm: GetPGProperty hands
// the owner (ibPropertyObject*) to the frontend slot, and the frontend property
// (ibPGDynamicListProperty) dynamic_casts that owner to reach the dynamic list
// and open the settings form. No backend function-pointer hook lives on the
// dynamic list itself — the action is a FRONTEND property action.
// ---------------------------------------------------------------------------
class BACKEND_API ibPropertyDynamicList : public ibProperty {
public:

	// The action carries no editable scalar — seed an empty variant so the base
	// stays well-formed; the frontend property drives the click flag.
	ibPropertyDynamicList(ibPropertyCategory* cat, const wxString& name)
		: ibProperty(cat, name, wxVariant())
	{
	}

	ibPropertyDynamicList(ibPropertyCategory* cat, const wxString& name, const wxString& label)
		: ibProperty(cat, name, label, wxVariant())
	{
	}

	ibPropertyDynamicList(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString)
		: ibProperty(cat, name, label, helpString, wxVariant())
	{
	}

	// get property for grid — pass the OWNER to the frontend slot (like ibPropertyForm).
	virtual wxObject* GetPGProperty() const override {
		if (ms_propertyDynamicList != nullptr)
			return ms_propertyDynamicList(m_owner, m_propLabel, m_propName, m_propValue);
		return nullptr;
	}

	// Action property — no runtime data exchange and nothing to (de)serialize on
	// the property cell itself. The dynamic-list settings are persisted by the
	// OWNER (the form attribute), not by this property.
	virtual bool SetDataValue(const ibValue& /*varPropVal*/) override { return false; }
	virtual bool GetDataValue(ibValue& /*pvarPropVal*/) const override { return false; }

	virtual bool ReadNodeValue(const ibDataValue& /*value*/) override { return true; }
	virtual bool WriteNodeValue(ibDataValue& /*value*/) const override { return true; }

public:

	// Wired at frontend load by ibPG_IMPLEMENT_PROPERTY_CALLBACK in advpropDynamicList.cpp.
	static wxObject* (*ms_propertyDynamicList)(ibPropertyObject*, const wxString&, const wxString&, const wxVariant&);
};

#endif // __PROPERTY_DYNAMIC_LIST_H__
