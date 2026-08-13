#ifndef __VALUE_POINT_IN_TIME_H__
#define __VALUE_POINT_IN_TIME_H__

#include "backend/compiler/value.h"

void ibValuePointInTime_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

// ⭐⭐ A POINT IN TIME IS A DATE AND, OPTIONALLY, THE RECORD THAT SITS AT IT.
//
// A date alone cannot separate two documents written in the same second, and the whole of
// accounting is built on "everything up to THIS document". So the moment carries both, and
// ordering is the pair: the date first, the reference as the tiebreak within it.
//
// It is a VALUE, not a mechanism. Everything that used to be "a date plus a recorder, glued by
// hand at each callsite" -- the period boundary of a virtual table, the sort key of a movements
// read, the answer to "when is this document" -- becomes one value with one order, and the
// question is asked OF IT rather than re-answered by every caller.
//
// The engine already knows how to sort it: ibValue::CompareValueLS is the ordering primitive the
// RAM sort floor and the comparison operators both go through, so overriding that one method
// gives the whole order -- in scripts, in a sorted collection, and in the query engine's own
// comparisons.
// The clsid the layout tier gates on — declared next to the value, the same arrangement as
// g_valueScheduleCLSID and g_valueTypeDescriptionCLSID. A second place that knows the spelling is a
// second place to get it wrong.
constexpr ibClassID g_valuePointInTimeCLSID = value_to_clsid("PointInTime");

class BACKEND_API ibValuePointInTime : public ibValueStaticMembers<&ibValuePointInTime_BindNames>
{
public:

	wxDateTime m_date;        // always present -- a moment with no date is not a moment
	ibValue    m_reference;   // optional -- empty means "the instant itself", before any record in it

public:

	ibValuePointInTime();
	ibValuePointInTime(const wxDateTime& date, const ibValue& reference = ibValue());
	virtual ~ibValuePointInTime() {}

	// PointInTime(date [, reference]) -- the reference is the SECOND argument and may be left out
	// or undefined. Everything that already sits at a moment vends its own PointInTime() instead.
	virtual bool Init(ibValue** paParams, const long lSizeArray);

	virtual wxString GetString() const;
	virtual bool IsEmpty() const { return !m_date.IsValid(); }

	// The date, then the reference. See the .cpp for what an absent reference means.
	virtual int CompareValueLS(const ibValue& cParam) const;
	virtual bool CompareValueEQ(const ibValue& cParam) const;
	virtual bool CompareValueNE(const ibValue& cParam) const;

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);
};

#endif // __VALUE_POINT_IN_TIME_H__
