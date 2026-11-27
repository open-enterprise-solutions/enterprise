#ifndef __VALUE_PART_H__
#define __VALUE_PART_H__

#include "backend/compiler/value.h"

// ----------------------------------------------------------------------------
// A VALUE THAT IS PART OF ANOTHER: holding the part holds the whole (#645).
//
// A tabular section is part of its object, and the object's RegisterRecords are part of the document.
// A part speaks through its whole on every write (it marks the object modified, it reads its guid), so a
// part that outlives its whole is a part pointing at freed memory: `Ref.GetObject().Goods.Add()` let go
// of the object as soon as `.Goods` was read, and the row was added through a dangling pointer.
//
//   whole
//     part        — holds itself and the whole
//       its row   — holds the part (ibValueModelReturnLine::HoldOwnerModel), which holds the whole
//
// IncrRef raises the part's count and then the whole's; DecrRef lowers the part's count first (which may
// end the part) and then the whole's, so the whole is always the last to go.
//
// The whole's own hold on its part is NOT counted on the whole — that would be the whole holding itself.
// It is the part's count alone, taken and let go by ibPartHold below. So whenever anybody but the whole
// holds a part, the whole's count is above zero; when the whole ends, every part it still keeps has only
// that count left and ends with it. A part the whole has put aside (a section read again, the next row of
// a selector) and somebody still holds keeps the whole alive until it is let go.
// ----------------------------------------------------------------------------

template <class Base>
class ibValuePart : public Base {
public:

	using Base::Base;

	virtual void IncrRef() noexcept override {
		Base::IncrRef();
		if (ibValue* const whole = GetWholeValue())
			whole->IncrRef();
	}

	virtual void DecrRef() noexcept override {
		ibValue* const whole = GetWholeValue();   // read first: the part may end below
		Base::DecrRef();
		if (whole != nullptr)
			whole->DecrRef();
	}

protected:

	// The value this is part of; null for a part made with no whole (a type's sample row).
	virtual ibValue* GetWholeValue() const = 0;
};

// ----------------------------------------------------------------------------
// ibPartHold<T>: the WHOLE's hold on one of its parts. It counts the part alone — through the qualified
// ibValue::IncrRef / DecrRef, which are not the part's overrides above — so the whole never counts itself.
// Move-only: a part has one whole. Empty is a state of its own (a section this kind of object does not use).
//
// It keeps the part as an ibValue, so a holder can be declared where T is only forward-declared; T must be
// complete where a part is handed in or read back.
// ----------------------------------------------------------------------------

template <class T>
class ibPartHold {
public:

	ibPartHold() = default;
	explicit ibPartHold(T* part) : m_part(part) {
		if (m_part != nullptr)
			m_part->ibValue::IncrRef();
	}

	ibPartHold(const ibPartHold&) = delete;
	ibPartHold& operator = (const ibPartHold&) = delete;

	ibPartHold(ibPartHold&& other) noexcept : m_part(other.m_part) { other.m_part = nullptr; }
	ibPartHold& operator = (ibPartHold&& other) noexcept {
		if (this != &other) {
			Reset();
			m_part = other.m_part;
			other.m_part = nullptr;
		}
		return *this;
	}

	~ibPartHold() { Reset(); }

	T* get() const { return static_cast<T*>(m_part); }
	T* operator->() const { return get(); }
	explicit operator bool() const noexcept { return m_part != nullptr; }

private:

	void Reset() noexcept {
		ibValue* const part = m_part;
		m_part = nullptr;
		if (part != nullptr)
			part->ibValue::DecrRef();
	}

	ibValue* m_part = nullptr;
};

#endif
