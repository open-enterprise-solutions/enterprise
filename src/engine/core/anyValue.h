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

#include <utility>   // std::swap — ibAnyValuePtr's assignment

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

	// Counted, as each side counts its own: whoever holds one keeps it, and the last to let go frees it.
	virtual void IncrRef() = 0;
	virtual void DecrRef() = 0;
};

////////////////////////////////////////////////////////////////////////////
// ibAnyValuePtr — A VALUE OF EITHER SIDE, HELD BY THE CORE: what a store of the core's keeps (a sheet's
// parameters, ibAnyValuePtr<ibAnyValue>). Counted through the value's own count — the runtime's, not a
// second owner beside it — the way a side's ibValuePtr holds one. The side puts in a value of its own
// (ibAnyValuePtr<ibValue>, which passes for the core's as a derived pointer passes for its base) and reads
// back its own.
////////////////////////////////////////////////////////////////////////////

template <class T>
class ibAnyValuePtr {
public:

	ibAnyValuePtr() = default;
	explicit ibAnyValuePtr(T* value) : m_value(value) { if (m_value != nullptr) m_value->IncrRef(); }
	ibAnyValuePtr(const ibAnyValuePtr& rhs) : m_value(rhs.m_value) { if (m_value != nullptr) m_value->IncrRef(); }
	ibAnyValuePtr(ibAnyValuePtr&& rhs) noexcept : m_value(rhs.m_value) { rhs.m_value = nullptr; }
	template <class U>
	ibAnyValuePtr(const ibAnyValuePtr<U>& rhs) : m_value(rhs.m_value) { if (m_value != nullptr) m_value->IncrRef(); }
	template <class U>
	ibAnyValuePtr(ibAnyValuePtr<U>&& rhs) noexcept : m_value(rhs.m_value) { rhs.m_value = nullptr; }
	ibAnyValuePtr& operator=(ibAnyValuePtr rhs) noexcept { std::swap(m_value, rhs.m_value); return *this; }
	~ibAnyValuePtr() { if (m_value != nullptr) m_value->DecrRef(); }

	const T& operator*() const { return *m_value; }
	const T* operator->() const { return m_value; }
	explicit operator bool() const noexcept { return m_value != nullptr; }

private:
	template <class U> friend class ibAnyValuePtr;

	T* m_value = nullptr;
};

#endif
