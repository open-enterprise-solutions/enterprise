#ifndef __VALUE_SPREADSHEET_DETAILS_H__
#define __VALUE_SPREADSHEET_DETAILS_H__

// ---------------------------------------------------------------------------
// ⭐⭐ WHAT A CELL WAS COMPOSED FROM — the value it shows, and what that value stood UNDER.
//
// A cell of a report has always carried a details parameter: the NAME of a parameter of the sheet,
// and under that name a value, which a click opens. The name stays exactly what it was; what
// changes is what lies under it. A bare value answers "what is this" and nothing else, so a reader
// asking the other question — "out of WHAT was this figure composed" — had nowhere to look.
//
// So the parameter becomes a WRAPPER: the value, plus the links to the wrappers it is subordinate
// to (Max, 2026-08-28: "the details parameter gives you information not only about the value, but
// about what it is linked WITH — with another details parameter, most likely").
//
// ⭐ IT IS STILL A VALUE WHERE IT IS READ AS ONE. `[Cell_3]` in a template prints what it always
// printed and "Open value" opens what it always opened, because everything a value is asked —
// its text, its number, its date, its comparisons, ShowValue — is answered by the value inside.
// The wrapper is only visible to the one reader that asks what only it knows.
//
// ⭐ AND IT IS A SYSTEM TYPE: the composer builds these, a script never says `New Details`.
//
// ⭐ THE LINKS POINT UP, NEVER DOWN, and that is not a preference:
//   * the reader needs the ASCENT — "in what context was this figure" is the path to the root;
//     which rows are under a group is answered by re-reading the database, not by the sheet;
//   * downward links are a refcount CYCLE — parent holds children, children hold parent, and the
//     sheet is never released;
//   * downward, a heading would have to remember every row under it, which is the whole sheet;
//     upward, a cell remembers a chain of two to four links and the chains are SHARED between the
//     cells of a group. The reports arc's bound holds: memory is the number of GROUPS, not of rows.
//
// ⭐ SEVERAL PARENTS, not a second kind of link. A cross-table cell is subordinate to its row
// heading AND to its column heading — one ascent that forks — while a grouping's chain is linear.
// "Parent" and "owner" would be two names for one question ("field = value, in the filter"), and
// which axis a link belongs to is already said by its PATH: the composition knows which of its
// levels read down the page and which across (ibCompositionOutputInfo::AxisOf).
// ---------------------------------------------------------------------------

#include "backend/compiler/value.h"
#include "backend/compiler/enumUnit.h"     // …and its script-visible face, below
#include "backend/query/queryLowering.h"   // ibColumnRole — WHAT the field a link stands for is

#include <vector>

// ⭐ WHAT KIND OF FIELD IS IN FRONT OF US, said in the runtime's own words (Max, 2026-08-28: "and if
// need be, declare runtime enums that let you tell what field this is").
//
// ⚠ IT IS THE FACE OF THE ROLE THE LOWERING ALREADY STATES, not a second vocabulary beside it. A
// dimension is filtered and grouped by, a measure is the figure itself, a detail is a projected
// field — that distinction exists once, in ibQueryLowering::ibColumnRole, and this only lets a
// script say it. Copying the three names into an enum of their own would be two lists to keep in
// step, and the day they disagree the report is right and the script is wrong.
class ibValueEnumSpreadsheetFieldKind : public ibValueEnumeration<ibQueryLowering::ibColumnRole> {
public:
	ibValueEnumSpreadsheetFieldKind() : ibValueEnumeration() {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibQueryLowering::ibColumnRole::Detail, wxT("Field"), _("Field"));
		AddEnumeration(ibQueryLowering::ibColumnRole::Dimension, wxT("Dimension"), _("Dimension"));
		AddEnumeration(ibQueryLowering::ibColumnRole::Measure, wxT("Resource"), _("Resource"));
	}
};

void ibValueSpreadsheetDetails_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

class BACKEND_API ibValueSpreadsheetDetails : public ibValueStaticMembers<&ibValueSpreadsheetDetails_BindNames> {
public:

	// One (field, value) of the context, in the composition's own vocabulary — the path is what a
	// grouping and a filter line are written with (ibGroupDescription::Append, ibFilterDescription::Append).
	struct ibSpreadsheetDetailsField {
		wxString m_path;
		ibValue  m_value;
	};

	ibValueSpreadsheetDetails() {}
	ibValueSpreadsheetDetails(const wxString& path, ibQueryLowering::ibColumnRole role, const ibValue& value)
		: m_path(path), m_role(role), m_value(value) {}

	// ⭐ THE PATH, NOT THE TITLE. A title is written for a person and may repeat; a path is what the
	// composition groups and filters by, and the detail is going to do both with it.
	const wxString& GetPath() const { return m_path; }
	// ⭐ AND THE ROLE, because the driver already knows it. A DIMENSION link is part of the context —
	// it is filtered by and can be grouped by; a MEASURE is the figure itself, and "Sum = 1234" is
	// not a filter anybody meant. Left out, this reader would re-derive it by hunting the path
	// through the composition's resources, which is the same answer computed a second time.
	ibQueryLowering::ibColumnRole GetRole() const { return m_role; }

	const ibValue& GetDetailsValue() const { return m_value; }
	const std::vector<ibValue>& GetParents() const { return m_parents; }

	// LINK THIS ONE UNDER THAT ONE. Empty parents are not links and are dropped here, so a caller
	// never has to ask whether the level above it exists.
	void LinkTo(const ibValue& parent);

	// ⭐ THE CONTEXT THIS CELL STOOD IN — every dimension on the way up, this one included when it is
	// a dimension itself. One walk answers both clicks: on a heading the heading is part of its own
	// context, on a figure it is not, and the ROLE is what decides rather than the caller.
	//
	// First seen wins: the two branches of a cross-table cell can name the same path only if the
	// composition grouped by it twice, and then the nearer link is the one that describes the cell.
	void CollectContext(std::vector<ibSpreadsheetDetailsField>& into) const;

	// ⭐ IF IT IS ONE, HERE IT IS. The probe every reader starts with — never ConvertToType, which
	// with _USE_CONTROL_VALUECAST RAISES on a value that is merely something else, and most
	// parameters of a sheet are.
	static ibValueSpreadsheetDetails* From(const ibValue& value) {
		ibValueSpreadsheetDetails* details = nullptr;
		return value.ConvertToValue(details) ? details : nullptr;
	}

	// …and the value BEHIND whatever this is — the wrapped one for a wrapper, the value itself
	// otherwise. So a comparison between a wrapped and a plain value compares the two values.
	static const ibValue& Unwrap(const ibValue& value);

	//////////////////////////////////////////////////////////////////////////
	// OUTSIDE, IT IS THE VALUE IT WRAPS — see the header note.
	//////////////////////////////////////////////////////////////////////////

	virtual wxString GetString() const override { return m_value.GetString(); }
	virtual const ibString& GetString(ibString& scratch) const override { return m_value.GetString(scratch); }
	virtual ibNumber GetNumber() const override { return m_value.GetNumber(); }
	virtual wxLongLong_t GetDate() const override { return m_value.GetDate(); }
	virtual bool GetBoolean() const override { return m_value.GetBoolean(); }
	virtual bool IsEmpty() const override { return m_value.IsEmpty(); }
	virtual size_t GetValueHash() const override { return m_value.GetValueHash(); }

	// ⭐⭐ AND OPENING IT OPENS THE VALUE — the wrapper has no card of its own, it stands for one.
	//
	// ⭐ …AND WHERE ITS OWN VALUE HAS NO CARD, IT OPENS WHAT IT STANDS UNDER (Max, 2026-08-28: "maybe
	// it should hold the reference as well"). A figure is a number: `ShowValue` on a number does
	// nothing, so a click on the cell a reader is most likely to click did nothing at all. The
	// reference IS already held — it is up the chain, on the heading this figure was measured under
	// — so this is the chain being ASKED rather than a second field beside the value.
	virtual void ShowValue() override;

	// The nearest value with something to show — this one when it is a reference, otherwise the
	// first one found going up. Null when nothing on the way up has a card either, which is an
	// honest answer: a report of pure numbers has nothing to open.
	ibValue* FindOpenable();

	virtual int  CompareValueLS(const ibValue& cParam) const override { return m_value.CompareValueLS(Unwrap(cParam)); }
	virtual int  CompareValueGT(const ibValue& cParam) const override { return m_value.CompareValueGT(Unwrap(cParam)); }
	virtual bool CompareValueGE(const ibValue& cParam) const override { return m_value.CompareValueGE(Unwrap(cParam)); }
	virtual bool CompareValueLE(const ibValue& cParam) const override { return m_value.CompareValueLE(Unwrap(cParam)); }
	virtual bool CompareValueEQ(const ibValue& cParam) const override { return m_value.CompareValueEQ(Unwrap(cParam)); }
	virtual bool CompareValueNE(const ibValue& cParam) const override { return m_value.CompareValueNE(Unwrap(cParam)); }

	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;

private:

	wxString                      m_path;
	ibQueryLowering::ibColumnRole m_role = ibQueryLowering::ibColumnRole::Detail;
	ibValue                       m_value;
	// ⚠ ibValue, NOT ibValueSpreadsheetDetails* — the links have to survive `PutArea`, which RE-KEYS every
	// parameter it copies (name + row + col). A link by name breaks on the first area written; a
	// link by value is a counted reference and does not notice the renaming.
	std::vector<ibValue>          m_parents;
};

#endif // __VALUE_SPREADSHEET_DETAILS_H__
