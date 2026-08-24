#ifndef __VALUE_COMPOSER_FIELD_H__
#define __VALUE_COMPOSER_FIELD_H__

// The FIELD a composer setting points at — the runtime value behind every condition, sort line
// and grouping level. Part of the composer's RUNTIME surface (see valueComposerSettings.h for
// what that surface is and why it is being rebuilt); the DATA it describes lives in
// backend/compositionDescription.h and knows nothing about this.

#include "backend/compiler/value.h"
#include "backend/typeDescription.h"


////////////////////////////////////////////////////////////////////////////
// CompositionField — a FIELD of a source, as a value
////////////////////////////////////////////////////////////////////////////
//
// `Supplier.Region.Country` is not a string. It is a field: it has a path, it
// has a TYPE (which decides how its value is edited and what may be compared
// with it), it belongs to a source, and it can be expanded into the fields
// beneath it when that type is a reference.
//
// All four facts exist today — spread across three places that keep
// re-deriving each other:
//
//   * the filter line keeps a path, a leaf id and a type as three unrelated
//     members;
//   * the settings dialog keeps `ibSourceFieldNode` — path, leaf id, type,
//     referenced targets, "expanded yet?" — as tree payload;
//   * `ibSourceDataObject::ibSourceExplorer` walks a source and produces the
//     same information a third time.
//
// Making it a TYPE collapses those into one thing that can be held, passed,
// compared, serialized and edited — and, most of all, put on BOTH SIDES of a
// condition. `field = value` and `field = otherField` stop being different
// cases: both sides are just a value that may or may not be a field.
//
// It is a value in the SCRIPT too, so a configuration can name a field without
// touching a dialog:
//
//   New CompositionField("Supplier.Region")
//
// WHAT IT IS NOT. Not a query expression: `Sum(Amount) > 100` is a different
// thing and will be its own shape when totals need it. This is the plain "one
// field of the source", which is what a filter, a sort, a grouping, a
// conditional-appearance rule and an access-policy restriction all point at.

// The field's own class id, named once: the type is asked for BY id (the cell's left side admits
// exactly this type), and a second spelling of "VL_CFLD" would be a second place to keep in step
// with the registration in the module.
constexpr ibClassID g_compositionFieldCLSID = value_to_clsid("VL_CFLD");

class BACKEND_API ibValueCompositionField : public ibValueDynamicMembers {
public:
	enum Prop { enPath = 0, enPresentation };

	ibValueCompositionField();
	explicit ibValueCompositionField(const wxString& path,
		const wxString& presentation = wxEmptyString);
	virtual ~ibValueCompositionField() {}

	void FillMembers(ibMemberTable& helper) const;
	virtual bool Init(ibValue** paParams, const long lSizeArray) override;
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal) override;

	// A field with no path is nothing — an unfilled left-hand side.
	virtual bool IsEmpty() const override { return m_path.IsEmpty(); }

	// The PRESENTATION when there is one, the path otherwise: what a user sees
	// in a filter line is "Country", not "Supplier.Region.Country" — but a field
	// built from script has no presentation, and showing an empty cell would be
	// worse than showing the path.
	virtual wxString GetString() const override {
		return m_presentation.IsEmpty() ? m_path : m_presentation;
	}

	// TWO FIELDS ARE THE SAME FIELD when they point at the same place. The path
	// is that answer: the leaf id is only meaningful against one source, and the
	// presentation is a label.
	virtual bool CompareValueEQ(const ibValue& cParam) const override;
	virtual bool CompareValueNE(const ibValue& cParam) const override;

	const wxString& GetPath() const { return m_path; }
	void SetPath(const wxString& path) { m_path = path; }

	const wxString& GetPresentation() const { return m_presentation; }
	void SetPresentation(const wxString& presentation) { m_presentation = presentation; }

	// THE FIELD'S TYPE — what makes the other side of a comparison editable at
	// all: the runtime adjusts, creates and offers a choice through it, instead
	// of a text box that guesses. Empty until the field is bound to a source.
	ibMetaID GetLeafId() const { return m_leafId; }
	const ibTypeDescription& GetTypeDescription() const { return m_typeDescription; }
	void SetTypeInfo(const ibMetaID& leafId, const ibTypeDescription& typeDesc) {
		m_leafId = leafId; m_typeDescription = typeDesc;
	}

protected:
	// Packed as its own contents (the base writes the type — see
	// compiler/valueSerialization.h). A field travels in a saved report variant,
	// in a stored list setting, and to a background job, so it packs like any
	// other value rather than as a string somebody re-parses.
	virtual bool DoSerialize(class ibDataNode& node) const override;
	virtual bool DoDeserialize(const class ibDataNode& node) override;

private:
	wxString          m_path;            // dot-path in the source's technical names
	wxString          m_presentation;    // what a user sees; empty when built from script
	ibMetaID          m_leafId = wxNOT_FOUND;   // the queryable column id, against ONE source
	ibTypeDescription m_typeDescription;        // the field's type — for AdjustValue / choice
};

#endif // __VALUE_COMPOSER_FIELD_H__
