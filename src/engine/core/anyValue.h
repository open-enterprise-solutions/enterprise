#ifndef _CORE_ANY_VALUE_H__
#define _CORE_ANY_VALUE_H__

////////////////////////////////////////////////////////////////////////////
// ibAnyValue — WHAT EVERY VALUE ANSWERS, on either side: its kind, and what it reads as — a flag, a
// number, a date, a text — and whether it is empty.
////////////////////////////////////////////////////////////////////////////
//
// The engine's ibValue (backend/compiler/value.h) and the client's (frmclient/backend/compiler/value.h)
// derive from it. Only the QUESTIONS are here: how a value is held is each side's own — the engine's
// one word beside its runtime, the client's what the server sent — and a reference passes them to the
// object it holds on its side. What asks only these (a Format, ibFormatString::Apply) takes either.
//
////////////////////////////////////////////////////////////////////////////

#include "core/core.h"
#include "core/fdatetime.h"
#include "core/fnumber.h"
#include "core/fstring.h"
#include "core/types.h"

class CORE_API ibAnyValue {
public:

	// Out of line (anyValue.cpp): the interface's table is the library's, one in the process — and a side's class,
	// exported from its own library, derives from an exported one.
	virtual ~ibAnyValue();

	// What it is — a primitive, Undefined, Null, or an object of a kind of its own.
	virtual ibValueTypes GetType() const = 0;
	virtual bool IsEmpty() const = 0;

	// What it reads as.
	virtual bool GetBoolean() const = 0;
	virtual ibNumber GetNumber() const = 0;
	virtual ibDateTime GetDate() const = 0;
	virtual ibString GetString() const = 0;
};

#endif
