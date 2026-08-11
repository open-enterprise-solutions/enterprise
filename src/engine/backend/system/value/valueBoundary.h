#ifndef __VALUE_BOUNDARY_H__
#define __VALUE_BOUNDARY_H__

#include "backend/compiler/value.h"
#include "backend/compiler/enumUnit.h"

// Whether the boundary's OWN value belongs to the interval it bounds.
//
// The distinction is not decoration: "the balance at the end of the day" and "the balance before
// today's first movement" are the same instant read two ways, and an accounting answer that gets it
// backwards is wrong by exactly one document. Naming the two ends is what lets a reader say which
// one they meant instead of nudging the date by a second and hoping.
enum ibBoundaryKind {
	ibBoundaryKind_Including,
	ibBoundaryKind_Excluding
};

#pragma region enumeration
class ibValueEnumBoundaryKind : public ibValueEnumeration<ibBoundaryKind> {
public:
	ibValueEnumBoundaryKind() : ibValueEnumeration() {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibBoundaryKind_Including, wxT("Including"), _("Including"));
		AddEnumeration(ibBoundaryKind_Excluding, wxT("Excluding"), _("Excluding"));
	}
};
#pragma endregion

void ibValueBoundary_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

// ⭐⭐ A BOUNDARY IS A POSITION PLUS WHICH SIDE OF IT YOU MEAN.
//
// The position is a date or a PointInTime -- the same two things every reading already takes, so a
// boundary adds nothing to WHERE a reading stops and only says whether the row sitting exactly
// there is in or out. That is why it wraps a value rather than replacing one: `Balance(date)`,
// `Balance(moment)` and `Balance(Boundary(moment, Excluding))` are the same question at three
// precisions, and the reading unwraps whichever it was handed.
//
// Excluding is what makes "the balance BEFORE this document" expressible. Without it the only way
// to ask is to name the document before it -- which the caller usually does not know and which
// stops being the right answer the moment somebody inserts a movement between them.
class BACKEND_API ibValueBoundary : public ibValueStaticMembers<&ibValueBoundary_BindNames>
{
public:

	ibValue        m_value;                            // a date, or a PointInTime
	ibBoundaryKind m_kind = ibBoundaryKind_Including;   // whether that position is part of the interval

public:

	ibValueBoundary();
	ibValueBoundary(const ibValue& value, ibBoundaryKind kind);
	virtual ~ibValueBoundary() {}

	// Boundary(value [, kind]) -- the kind defaults to Including, which is what a bare date has
	// always meant, so wrapping a value without saying more changes nothing.
	virtual bool Init(ibValue** paParams, const long lSizeArray);

	virtual wxString GetString() const;
	virtual bool IsEmpty() const { return m_value.IsEmpty(); }

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);
};

#endif // __VALUE_BOUNDARY_H__
