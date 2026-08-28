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

////////////////////////////////////////////////////////////////////////////
// CompositionPredefinedValue — the DESIGNER'S way of saying "one of the declared values"
////////////////////////////////////////////////////////////////////////////
//
// ⭐⭐ A TECHNICAL TYPE, AND ITS WHOLE JOB IS TO STAND IN A TYPE LIST. A parameter that admits
// `CatalogRef.Banks, Date` asks two different questions: a DATE is typed in, and a reference — with
// no data behind it while the configuration is being written — is one of the values the
// configuration DECLARES. Listing every admitted reference type beside `Date` says the first half of
// that badly and the second half not at all.
//
// So the references collapse into ONE entry: choose it, and the declared values of every admitted
// reference type are offered together (Max, 2026-08-28: "such a technical type is introduced
// precisely so that all those references can be selected by it").
//
// ⚠ IT IS NEVER STORED. What lands in the parameter is a real reference — type + guid, which
// serialises and resolves at run time like any other. This type exists so the QUESTION can be asked,
// and it is registered for the same reason a word is: so it has a name to show.
// ⚠ SYSTEM — nobody writes `New CompositionPredefinedValue`. It is VENDED by the tier that owns it:
// the designer's window makes one when a declared value is chosen, and the composition store builds
// it back when it reads its own blob (ibStoredValue). The value factory is not that road.
constexpr ibClassID g_compositionPredefinedCLSID = system_to_clsid("VL_CPRV");

// ⭐⭐ AND IT IS WHAT GETS STORED — A DECLARATION, NOT A LIVE REFERENCE.
//
// A reference object is RUNTIME: it belongs to a session, it has a register of its own, it reads
// rows. Putting one into a description means creating runtime while a configuration is being
// LOADED — the type it names may be three branches away and not exist yet, and the load dies on a
// value that was saved perfectly well (Max, 2026-08-28: "the runtime must not leak in; the runtime
// is made at execution").
//
// So what the designer writes is what it is: the metaobject this names and which of its declared
// values — two plain fields, read and written like any other data. The RUNTIME reference is built
// from them at execution, by whoever is running the composition, against the configuration it runs
// in (ibMaterializeCompositionValue below).
class BACKEND_API ibValueCompositionPredefined : public ibValue {
public:
	ibValueCompositionPredefined() : ibValue(ibValueTypes::TYPE_VALUE, true) {}
	ibValueCompositionPredefined(const ibMetaID& metaId, const ibGuid& guid, const wxString& written)
		: ibValue(ibValueTypes::TYPE_VALUE, true), m_metaId(metaId), m_guid(guid), m_written(written) {
	}

	virtual ibClassID GetClassType() const override { return g_compositionPredefinedCLSID; }

	// WHAT A PERSON READS IN THE CELL — `CatalogRef.Goods.Chair`, as it was written. Held rather than
	// recomputed: the name is what the designer chose, and it must read the same before there is any
	// configuration to ask.
	virtual wxString GetString() const override { return m_written; }

	// ⚠ AN EMPTY REFERENCE IS NOT AN EMPTY VALUE. `CatalogRef.Goods.EmptyRef` is a value of that type
	// and a legitimate thing to store — saying otherwise made it skipped on the way to the store, and
	// the cell came back blank (journal, 2026-08-29: `store: '…EmptyRef' empty=1 packed=0`). Empty
	// here means only "this declaration names nothing at all".
	virtual bool IsEmpty() const override { return m_metaId == wxNOT_FOUND && m_written.IsEmpty(); }

	ibMetaID GetMetaId() const { return m_metaId; }
	const ibGuid& GetGuid() const { return m_guid; }

protected:
	virtual bool DoSerialize(class ibDataNode& node) const override;
	virtual bool DoDeserialize(const class ibDataNode& node) override;

private:
	ibMetaID m_metaId = wxNOT_FOUND;   // the metaobject the value belongs to
	ibGuid   m_guid;                   // which of its declared values — empty = the empty reference
	wxString m_written;                // how it reads: `CatalogRef.Goods.Chair`
};

// ⭐ THE RUNTIME IS MADE HERE AND NOWHERE EARLIER. A declaration becomes a live reference against the
// configuration handed in; anything else is already a value and comes back untouched. Called where a
// composition is EXECUTED — the parameters are worked out, the query is filled in.
BACKEND_API ibValue ibMaterializeCompositionValue(const ibValue& stored, const class ibMetaData* metaData);

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
