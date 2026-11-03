#ifndef __VALUE_SAFE_ARRAY_H__
#define __VALUE_SAFE_ARRAY_H__

#include "backend/compiler/value.h"

#include <vector>

#ifdef __IBMSW__
#include <oaidl.h>      // SAFEARRAY, VARIANT

class ibValueArray;
#endif

void ibValueSafeArray_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

// ⭐⭐ AN ARRAY AS COM HOLDS IT — `New COMSafeArray("VT_I4", 2, 3)`. A SAFEARRAY of one element type, of as many
// dimensions as it was made with, each with its own lower bound: what a COM method declared `SAFEARRAY(long)` takes,
// and what a spreadsheet's Range.Value hands back (from 1, not from 0).
//
// ⭐ AN ARRAY A COM OBJECT RETURNS IS ONE OF THESE, always — as it came, nothing read until it is asked: `Unload()`
// gives it as arrays in arrays. Going the other way an Array still goes by itself (ibValueCOM::FromValue), as
// VARIANTs; this is for a method that wants another element type or other bounds.
//
// ⭐ DIMENSIONS COUNT FROM 0, the left-most first — `GetValue(row, column)` for a spreadsheet's table — and an
// index is the array's own, from its lower bound.
//
// The type is registered on every platform, as COMObject is: a module that makes one compiles everywhere, and
// `New` is refused elsewhere than Windows.
class BACKEND_API ibValueSafeArray : public ibValueStaticMembers<&ibValueSafeArray_BindNames> {
	enum Func {
		enGetDimensions,
		enGetLength,
		enGetLowerBound,
		enGetUpperBound,
		enGetType,
		enGetValue,
		enSetValue,
		enIsResizable,
		enResize,
		enUnload,
	};
public:

	ibValueSafeArray();
#ifdef __IBMSW__
	// A copy of the array `oleVariant` holds.
	ibValueSafeArray(const VARIANT& oleVariant);
#endif
	ibValueSafeArray(const ibValueSafeArray&) = delete;   // the array is held once: a copy is a New from it
	virtual ~ibValueSafeArray();

	virtual bool Init() override {
		return false;
	}
	virtual bool Init(ibValue** paParams, const long lSizeArray) override;

	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) override;
	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;

	// NOT transferable across sessions, as the object it came from is not: an array of objects holds interface
	// pointers bound to this thread's apartment.
	virtual bool IsTransferable() const override { return false; }

	// The elements as arrays in arrays, the left-most dimension the outermost.
	ibValue Unload() const;

#ifdef __IBMSW__
	// A copy of the array, into a VARIANT the caller clears.
	void CopyTo(VARIANT& oleVariant) const;

	// `values` as an array of `elementType`, in a VARIANT the caller clears. With no `bounds` the array's own shape:
	// rows of one length, to any depth, as one array of as many dimensions — the table a spreadsheet's Range.Value
	// takes; any other array as a row of its items.
	static VARIANT FromValueArray(const ibValueArray* values, const VARTYPE elementType, std::vector<SAFEARRAYBOUND> bounds);
#endif

private:

#ifdef __IBMSW__
	// Dimension `dimension` (0 = the left-most, the outer array) added to `arr`: the elements of the right-most one,
	// an array of its own for each of the others.
	void Unload(ibValueArray* arr, const size_t dimension, std::vector<LONG>& indices) const;

	// As COM holds one: VT_ARRAY with the element type, and the SAFEARRAY. VT_EMPTY until Init makes it.
	VARIANT m_array = {};
#endif
};

#endif
