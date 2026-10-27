#ifndef _CORE_VARIANT_PTR_H__
#define _CORE_VARIANT_PTR_H__

#include "core/variant.h"

#include <cstddef>   // std::nullptr_t

////////////////////////////////////////////////////////////////////////////
// ibVariantPtr<T> — the runtime's ibValuePtr (backend/value_ptr.h) as it is: an owning, counted reference to a T
// derived from ibVariant that also gives typed access. It IS a variant — it holds its referent in m_pRef
// (TYPE_REFFER) — and counts it: IncrRef on bind, DecrRef on rebind / destruction (the latter via ~ibVariant).
////////////////////////////////////////////////////////////////////////////

template <class T>
class ibVariantPtr : public ibVariant {

	inline void OnSetValueT(ibVariant* ptr) {
		// Self-assignment guard: skip when already bound to the same referent.
		if (m_pRef != ptr && !m_bReadOnly) {
			Reset();
			if (ptr != nullptr) {
				m_typeClass = ibValueTypes::TYPE_REFFER;
				m_pRef = ptr;
				m_pRef->IncrRef();
			}
		}
	}

	inline void OnSetValueU(ibVariant* ptr) {
		if (ptr != nullptr)
			OnSetValueT(dynamic_cast<T*>(ptr));
	}

public:

	ibVariantPtr() : ibVariant() {}
	ibVariantPtr(std::nullptr_t) : ibVariant() {} // construct empty ibVariantPtr

	explicit ibVariantPtr(T* ptr) : ibVariant() { OnSetValueT(ptr); }

	// copy ctor
	ibVariantPtr(const ibVariant& to_copy) : ibVariant() { OnSetValueU(to_copy.m_pRef); }
	ibVariantPtr(const ibVariantPtr<T>& to_copy) : ibVariant() { OnSetValueT(to_copy.m_pRef); }

	// generalized copy ctor: U must be convertible to T
	template <typename U>
	ibVariantPtr(const ibVariantPtr<U>& to_copy) : ibVariant() { OnSetValueU(to_copy.m_pRef); }

	inline T& operator*() const { return *Ptr(); }
	inline T* operator->() const { return Ptr(); }

	inline operator T* () const { return Ptr(); }
	inline explicit operator bool() const noexcept { return m_pRef != nullptr; }

	void operator = (T* ptr) { OnSetValueT(ptr); }

	template <typename U>
	void operator = (const ibVariantPtr<U>& other) { OnSetValueU(other.m_pRef); }
	void operator = (const ibVariantPtr& other) { OnSetValueT(other.m_pRef); }

private:

	inline T* Ptr() const { return static_cast<T*>(m_pRef); }
};

// HELD OBJECTS COMPARED AS POINTERS — the runtime's (backend/value_ptr.h): a held object is a variant too, and without
// these a comparison would reach for the variant's own operator== and ask whether the two VALUES are equal.
template <class _Ty1, class _Ty2>
inline bool operator==(const ibVariantPtr<_Ty1>& _Left, const ibVariantPtr<_Ty2>& _Right) noexcept {
	return static_cast<_Ty1*>(_Left) == static_cast<_Ty2*>(_Right);
}

template <class _Ty1, class _Ty2>
inline bool operator!=(const ibVariantPtr<_Ty1>& _Left, const ibVariantPtr<_Ty2>& _Right) noexcept {
	return static_cast<_Ty1*>(_Left) != static_cast<_Ty2*>(_Right);
}

template <class _Ty>
inline bool operator==(std::nullptr_t, const ibVariantPtr<_Ty>& _Right) noexcept {
	return static_cast<_Ty*>(_Right) == nullptr;
}

template <class _Ty>
inline bool operator!=(std::nullptr_t, const ibVariantPtr<_Ty>& _Right) noexcept {
	return static_cast<_Ty*>(_Right) != nullptr;
}

template <class _Ty>
inline bool operator==(const ibVariantPtr<_Ty>& _Left, std::nullptr_t) noexcept {
	return static_cast<_Ty*>(_Left) == nullptr;
}

template <class _Ty>
inline bool operator!=(const ibVariantPtr<_Ty>& _Left, std::nullptr_t) noexcept {
	return static_cast<_Ty*>(_Left) != nullptr;
}

template <class _Ty1, class _Ty2>
inline bool operator==(const ibVariantPtr<_Ty1>& _Left, _Ty2* _Right) noexcept {
	return static_cast<_Ty1*>(_Left) == _Right;
}

template <class _Ty1, class _Ty2>
inline bool operator==(_Ty1* _Left, const ibVariantPtr<_Ty2>& _Right) noexcept {
	return _Left == static_cast<_Ty2*>(_Right);
}

template <class _Ty1, class _Ty2>
inline bool operator!=(_Ty1* _Left, const ibVariantPtr<_Ty2>& _Right) noexcept {
	return _Left != static_cast<_Ty2*>(_Right);
}

template <class _Ty1, class _Ty2>
inline bool operator!=(const ibVariantPtr<_Ty1>& _Left, _Ty2* _Right) noexcept {
	return static_cast<_Ty1*>(_Left) != _Right;
}

#endif
