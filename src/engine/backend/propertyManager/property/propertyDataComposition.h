#ifndef __PROPERTY_DATA_COMPOSITION_H__
#define __PROPERTY_DATA_COMPOSITION_H__

#include "backend/propertyManager/propertyObject.h"

// ---------------------------------------------------------------------------
// ibPropertyDataComposition — backend half of the "data composer settings"
// designer property. Pure action property (no stored scalar of its own): it
// surfaces a single "Settings…" action on the composition that created it.
//
// ⭐ ITS OWN TYPE, not the dynamic list's (Max, 2026-08-20). A list and a
// composition are two different things with two different settings windows —
// the list's leads with the query, the composition's with the output structure
// — and the registry matches the FRONTEND property by the BACKEND type. Sharing
// one type meant one frontend property that forked inside its click handler on
// what the owner turned out to be: a branch standing in for a type.
//
// It deliberately does NOT derive ibPropertyDynamicList: the registry matches by
// HIERARCHY (a dynamic_cast per entry), so a subclass would answer to the list's
// maker as well and the pair would be decided by registration order.
// ---------------------------------------------------------------------------
class BACKEND_API ibPropertyDataComposition : public ibProperty {
public:

	// The action carries no editable scalar — seed an empty variant so the base
	// stays well-formed; the frontend property drives the click flag.
	ibPropertyDataComposition(ibPropertyCategory* cat, const wxString& name)
		: ibProperty(cat, name, wxVariant())
	{
	}

	ibPropertyDataComposition(ibPropertyCategory* cat, const wxString& name, const wxString& label)
		: ibProperty(cat, name, label, wxVariant())
	{
	}

	ibPropertyDataComposition(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString)
		: ibProperty(cat, name, label, helpString, wxVariant())
	{
	}

	// Action property — no runtime data exchange and nothing to (de)serialize on the
	// property cell itself. What the window edits is the COMPOSITION's own state, and
	// the composition serialises that (ReadProperty / WriteProperty).
	virtual bool SetDataValue(const ibValue& /*varPropVal*/) override { return false; }
	virtual bool GetDataValue(ibValue& /*pvarPropVal*/) const override { return false; }

	virtual bool ReadNodeValue(const ibDataValue& /*value*/) override { return true; }
	virtual bool WriteNodeValue(ibDataValue& /*value*/) const override { return true; }
};

#endif // __PROPERTY_DATA_COMPOSITION_H__
