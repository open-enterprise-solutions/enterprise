#ifndef _CORE_REF_COUNTER_H__
#define _CORE_REF_COUNTER_H__

#include "core/coreApi.h"

#include <atomic>

////////////////////////////////////////////////////////////////////////////
// A COUNT OF ITS HOLDERS, the object deleted by the last one: what wx calls wxRefCounter (wx/object.h), forked so
// the tabular model's items and models carry no wx. The verbs are wx's: a client's wxObjectDataPtr holds a model
// by IncRef / DecRef.
//
// Two departures from wx. The count is atomic (wx's is a plain int): an item is copied on the fetch thread as well,
// and a count shared between threads is what failed with "m_count > 0 failed in DecRef()" on 2026-10-06 - its
// neighbours ibVariant, ibString and ibMemoryBuffer count atomically too. And DecRef asserts nothing yet: wx's
// check of a count above zero comes back with the core's own assert.
////////////////////////////////////////////////////////////////////////////

class CORE_API_VTABLE ibRefCounter {
public:

	ibRefCounter() = default;
	ibRefCounter(const ibRefCounter&) = delete;
	ibRefCounter& operator=(const ibRefCounter&) = delete;

	int GetRefCount() const { return m_count.load(); }

	void IncRef() { ++m_count; }
	void DecRef() { if (--m_count == 0) delete this; }

protected:

	// Never deleted directly: only by the last DecRef.
	virtual ~ibRefCounter() = default;

private:

	std::atomic<int> m_count{ 1 };   // the one who made it holds it
};

#endif
