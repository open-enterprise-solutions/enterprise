#ifndef __CHOICE_LINK_DESCRIPTION_H__
#define __CHOICE_LINK_DESCRIPTION_H__

#include "backend/backend_core.h"      // ibMetaID
#include "backend/clsid.h"             // ibClassID — the type a link governs is a TYPE, never an ordinal
#include "backend/sourceDescription.h" // ibSourceDescription — THE path in this tree, hop by hop

#include <vector>

// ⭐ A LINK NAMES A FIELD BY THE PATH THIS TREE ALREADY HAS. The first cut of this file carried a path of
// its own — a root plus a list of ids — and that was a second road beside `ibSourceDescription`, which is
// what binds a control to its data, walks a dot-path hop by hop and serialises itself. The give-away was
// the function that had to convert one into the other (Max, 2026-09-23: "why drag the source object in,
// it is a hop you have there").
//
// ⭐ AND THE ROOT GOES WITH IT. It existed to say WHERE to resolve — the object, the form, the row — and
// that is not a property of the path: it is the SOURCE a caller resolves against. A column resolves its
// row's source, a form field the form's, an object its own; the path is the same hops in every case, and
// whoever holds the source knows which one it is.
//
// ⭐ AND THE FIELD A LINK NAMES IS `source.GetLeaf()` — the path type's own word for its last hop, which
// it has always had. A helper of mine stood here saying the same thing with a different empty answer (0
// against the path's wxNOT_FOUND), so the two roads did not even agree about "nothing" (audit, 2026-09-23).
// Whether a link is set at all is `source.IsOk()`, asked of the path rather than deduced from the id.

// ⭐ THE LINK BY TYPE, AS A DESCRIPTION — the same shape as ibTypeDescription and ibCalcScheduleDescription:
// a structure, a variant that holds it, a property that hands it out by reference, and a Memory class that
// saves it.
//
// It answers ONE question - WHICH LIST OPENS - and that is what keeps it apart from the choice parameters
// beside it: those answer what is shown IN the list. A caller handed a condition where a type was meant
// cannot recover the difference, so the two are not folded into one property (docs/private/choice-links.md).
//
// 🛑 `m_governedType` NAMES A TYPE, NOT A POSITION. The older tools spell this as an ordinal - "the third
// element of the type description" - and it breaks in silence: the types are reordered, nothing complains,
// and the link now governs a different one. This tree already refuses that identity everywhere else (a
// reference is its type's id, never its name or its place), and the same answer holds here. Zero means the
// field has exactly one reference type and the engine takes it: the author is not asked a question that has
// only one answer.
struct ibChoiceTypeLinkDescription {

	ibSourceDescription m_source;
	ibClassID           m_governedType = 0;

	bool IsOk() const { return m_source.IsOk(); }
	void Clear() { m_source.ClearSource(); m_governedType = 0; }

	bool operator==(const ibChoiceTypeLinkDescription& other) const {
		return m_source.m_listSource == other.m_source.m_listSource && m_governedType == other.m_governedType;
	}
	bool operator!=(const ibChoiceTypeLinkDescription& other) const { return !(*this == other); }
};

// ⭐ WHAT HAPPENS TO THE VALUE ALREADY CHOSEN when the field that governs it changes. It is a question
// about the VALUE, so it is named after what becomes of the value rather than after what the link does.
//
// 🛑 It has to be answered at declaration time. When the counterparty changes, the contract already in
// the field belongs to the previous one: leaving it there is a silently wrong value, and deciding it in
// whichever handler somebody writes that day is deciding it once per form.
enum class ibChoiceParameterOnChange : unsigned char {
	Clear = 0,    // the safe default — the old value belonged to the old source
	Keep,         // for the cases where it is still legitimate
};

// ONE ROW of the choice parameters: which parameter of the target is filled, where its value comes
// from, and what becomes of the value already chosen when that source changes.
//
// `m_parameter` is a FIELD OF THE TARGET, held by its metaID — the namespace (`Filter.`) is the shape
// of the row, not part of its storage: a row of this table IS a condition over a field of the target,
// and a parameter of another nature would be another table rather than another prefix here.
struct ibChoiceParameterRowDescription {

	ibMetaID                  m_parameter = 0;
	ibSourceDescription       m_source;
	ibChoiceParameterOnChange m_onChange = ibChoiceParameterOnChange::Clear;

	bool IsOk() const { return m_parameter != 0 && m_source.IsOk(); }

	bool operator==(const ibChoiceParameterRowDescription& other) const {
		return m_parameter == other.m_parameter && m_source.m_listSource == other.m_source.m_listSource
			&& m_onChange == other.m_onChange;
	}
	bool operator!=(const ibChoiceParameterRowDescription& other) const { return !(*this == other); }
};

// ⭐ WHAT IS SHOWN IN THE LIST — the companion of the link by type, which answers which list opens. A
// TABLE, because there is no reason for there to be one: a choice may be narrowed by the owner and by
// a warehouse and by whatever else the form knows.
//
// ⭐ THE OWNER IS ONE OF THESE ROWS, and that is the whole of "link by owner": a catalog that declares
// `ListOwner` has its row written by the designer (parameter = the owner field, source = the field of
// the owner's type, on change = Clear), so the author sees it rather than having to know it was needed.
// A third mechanism for the owner would say the same thing in a second place.
struct ibChoiceParametersDescription {

	std::vector<ibChoiceParameterRowDescription> m_rows;

	bool IsOk() const { return !m_rows.empty(); }
	void Clear() { m_rows.clear(); }

	// One row per parameter: setting replaces, an empty source removes.
	void SetRow(const ibChoiceParameterRowDescription& row) {
		for (auto it = m_rows.begin(); it != m_rows.end(); ++it) {
			if (it->m_parameter != row.m_parameter)
				continue;
			if (!row.m_source.IsOk()) m_rows.erase(it);
			else *it = row;
			return;
		}
		if (row.IsOk())
			m_rows.push_back(row);
	}

	bool operator==(const ibChoiceParametersDescription& other) const { return m_rows == other.m_rows; }
	bool operator!=(const ibChoiceParametersDescription& other) const { return !(*this == other); }
};

// node form: a link is a Child { root, path: [ids], governedType }; the parameters are a Child
// { rows: [{ parameter, root, path: [ids], onChange }] } — metaIDs and a class id, for the reason above.
class BACKEND_API ibChoiceLinkDescriptionMemory {
public:
	static bool ReadNode(const class ibDataValue& value, ibChoiceTypeLinkDescription& linkDesc);
	static bool WriteNode(class ibDataValue& value, const ibChoiceTypeLinkDescription& linkDesc);

	static bool ReadNode(const class ibDataValue& value, ibChoiceParametersDescription& paramsDesc);
	static bool WriteNode(class ibDataValue& value, const ibChoiceParametersDescription& paramsDesc);
};

#endif
