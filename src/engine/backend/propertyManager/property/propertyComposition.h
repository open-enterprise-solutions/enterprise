#ifndef __PROPERTY_COMPOSITION_H__
#define __PROPERTY_COMPOSITION_H__

#include "backend/propertyManager/propertyObject.h"
#include "backend/system/value/valueDataComposition.h"

// ---------------------------------------------------------------------------
// ibPropertyComposition — the data property that HOLDS a composition, modelled
// on ibPropertySpreadsheet (Max, 2026-08-20: "do it the way the spreadsheet
// document does").
//
// ⭐ WHY A PROPERTY AT ALL. A metaobject's node has two areas and they mean
// different things (ibDataNode): PROPERTIES are what the object IS, CHILDREN are
// the objects under it. A metatype that keeps its content in a property therefore
// serialises in two symmetric lines — the template metatype's whole ReadData is
// `SetNodeValue(node.GetProperty(name))` — and its node can never grow a child
// that is not a metaobject. The composer used to hand its own node to the
// composition, which writes a sub-node per variant and per parameter; those
// landed among the metaobject children and the file stopped opening.
//
// The composition is a VALUE (ibValueDataComposition) and stays one: this
// property owns it, hands it out for editing, and packs it whole on save.
// ---------------------------------------------------------------------------
class BACKEND_API ibPropertyComposition : public ibProperty {
public:

	ibPropertyComposition(ibPropertyCategory* cat, const wxString& name)
		: ibProperty(cat, name, wxVariant()), m_composition(new ibValueDataComposition())
	{
	}

	ibPropertyComposition(ibPropertyCategory* cat, const wxString& name, const wxString& label)
		: ibProperty(cat, name, label, wxVariant()), m_composition(new ibValueDataComposition())
	{
	}

	// THE COMPOSITION ITSELF — handed out, never copied: the settings window edits this very object,
	// and a copy given to it would be the one the user is looking at while the saved one stayed behind.
	ibValueDataComposition* GetComposition() const { return m_composition; }

	// --- the script/data face ------------------------------------------------
	// The value IS the composition; assigning one over is refused (see ibValueDataComposition:
	// a composition is configured, not replaced).
	virtual bool SetDataValue(const ibValue& /*varPropVal*/) override { return false; }
	virtual bool GetDataValue(ibValue& pvarPropVal) const override {
		pvarPropVal = m_composition;
		return true;
	}

	// --- serialisation -------------------------------------------------------
	// ONE VALUE, holding the composition's own node. `ibDataValue::Child` is the node-in-a-value the
	// property area is made of, so the composition writes whatever it likes INSIDE its own node and
	// nothing of it reaches the metaobject's children.
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;

private:
	ibValuePtr<ibValueDataComposition> m_composition;
};

#endif // __PROPERTY_COMPOSITION_H__
