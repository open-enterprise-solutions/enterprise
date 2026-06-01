#ifndef _VALUE_PTR_H__
#define _VALUE_PTR_H__

#include "backend_core.h"

// ----------------------------------------------------------------------------
// ibValuePtr<T>: an owning, refcounted reference to an ibValue-derived T that
// also gives typed access. It IS an ibValue — just like a plain ibValue it holds
// its referent in m_pRef (TYPE_REFFER) and is traversed by the runtime as a
// value — and on top it adds the typed view (operator-> / operator T*) and
// manages the refcount: IncrRef on bind, DecrRef on rebind / destruction (the
// latter via ~ibValue). Assigning a raw T* or another ibValuePtr rebinds the
// reference, releasing the previous referent.
// ----------------------------------------------------------------------------

template <class T>
class ibValuePtr : public ibValue {

	inline void OnSetValueT(ibValue* ptr) {
		// Self-assignment guard: skip when already bound to the same referent.
		// m_pRef is null-initialized (ibValue ctor) and, for an ibValuePtr, only
		// ever holds a reffer, so comparing it directly is safe. The previous
		// `this != ptr` compared the WRAPPER's address to the referent — it never
		// caught a real self-assign, so `p = (T*)p` would Reset()→DecrRef the sole
		// owner to 0 (destroying it) and then IncrRef the freed object: a
		// use-after-free. Comparing the referent fixes that and also skips
		// redundant DecrRef/IncrRef churn on a no-op rebind.
		if (m_pRef != ptr && !m_bReadOnly) {
			Reset();
			if (ptr != nullptr) {
				m_typeClass = ibValueTypes::TYPE_REFFER;
				m_pRef = ptr;
				m_pRef->IncrRef();
			}
		}
	}

	inline void OnSetValueU(ibValue* ptr) {
		if (ptr != nullptr)
			OnSetValueT(dynamic_cast<T*>(ptr));
	}

public:

	constexpr ibValuePtr() : ibValue() {}
	constexpr ibValuePtr(nullptr_t) : ibValue() {} // construct empty ibValuePtr

	explicit ibValuePtr(T* ptr) : ibValue() { OnSetValueT(ptr); }

	// copy ctor
	ibValuePtr(const ibValue& to_copy) : ibValue() { OnSetValueU(to_copy.m_pRef); }
	ibValuePtr(const ibValuePtr<T>& to_copy) : ibValue() { OnSetValueT(to_copy.m_pRef); }

	// generalized copy ctor: U must be convertible to T	
	template <typename U>
	ibValuePtr(const ibValuePtr<U>& to_copy) : ibValue() { OnSetValueU(to_copy.m_pRef); }

	inline T& operator*() const {
		T* const ptr = Ptr();
		wxASSERT(ptr != nullptr);
		return *(ptr);
	}

	inline T* operator->() const { return Ptr(); }

	inline operator T* () const { return Ptr(); }
	inline explicit operator bool() const noexcept { return m_pRef != nullptr; }

	void operator = (T* ptr) { OnSetValueT(ptr); }

	template <typename U>
	void operator = (const ibValuePtr<U>& other) { OnSetValueU(other.m_pRef); }
	void operator = (const ibValuePtr& other) { OnSetValueT(other.m_pRef); }

private:

	inline T* Ptr() const { return static_cast<T*>(m_pRef); }
};

template <class _Ty1, class _Ty2>
inline bool operator==(const ibValuePtr<_Ty1>& _Left, const ibValuePtr<_Ty2>& _Right) noexcept {
	return _Left.m_pRef == _Right.m_pRef;
}

template <class _Ty1, class _Ty2>
inline bool operator!=(const ibValuePtr<_Ty1>& _Left, const ibValuePtr<_Ty2>& _Right) noexcept {
	return _Left.m_pRef != _Right.m_pRef;
}

template <class _Ty>
inline bool operator==(nullptr_t, const ibValuePtr<_Ty>& _Right) noexcept {
	return _Right.m_pRef == nullptr;
}

template <class _Ty>
inline bool operator!=(nullptr_t, const ibValuePtr<_Ty>& _Right) noexcept {
	return _Right.m_pRef != nullptr;
}

template <class _Ty>
inline bool operator==(const ibValuePtr<_Ty>& _Left, nullptr_t) noexcept {
	return _Left.m_pRef == nullptr;
}

template <class _Ty>
inline bool operator!=(const ibValuePtr<_Ty>& _Left, nullptr_t) noexcept {
	return _Left.m_pRef != nullptr;
}

template <class _Ty1, class _Ty2>
inline bool operator==(const ibValuePtr<_Ty1>& _Left, _Ty2* _Right) noexcept {
	return _Left.m_pRef == _Right;
}

template <class _Ty1, class _Ty2>
inline bool operator==(_Ty1* _Left, const ibValuePtr<_Ty2>& _Right) noexcept {
	return _Left == _Right.m_pRef;
}

template <class _Ty1, class _Ty2>
inline bool operator!=(_Ty1* _Left, const ibValuePtr<_Ty2>& _Right) noexcept {
	return _Left != _Right.m_pRef;
}

template <class _Ty1, class _Ty2>
inline bool operator!=(const ibValuePtr<_Ty1>& _Left, _Ty2* _Right) noexcept {
	return _Left.m_pRef != _Right;
}

#endif 