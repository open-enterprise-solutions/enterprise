#ifndef __CTOR_REGISTRY_H__
#define __CTOR_REGISTRY_H__

#include <unordered_map>
#include <typeindex>
#include <typeinfo>

#include "backend/clsid.h"               // ibClassID
#include "backend/stringUtils.h"         // stringUtils::CompareString (name lookup)
#include "backend/compiler/typeCtor.h"   // ibCtorAbstractType — the key accessors

// =============================================================================
// ibCtorRegistry<T> — the single owner of registered type-ctors and their
// lookup indices ("our class for registering classes"): clsid / type_info /
// name all resolve THROUGH this object, never by touching a raw container.
//
// Single source of truth is the ctor object itself — T (: ibCtorAbstractType)
// carries GetClassType() / GetTypeInfo() / GetClassName(). The maps below are
// pointer indices INTO that one object, mutated ONLY through Register /
// Unregister (keys read off the same ctor in the same call), so the two indices
// cannot drift out of sync.
//
//   - clsid     -> O(1) hash index   (HOT: CreateObject / IsRegisterCtor / VT)
//   - type_info -> O(1) hash index   (HOT: live-object self-id in GetClassType)
//   - name      -> LINEAR scan       (NOT very hot: compile-time resolution of
//                                      CreateObject("Name"); case-insensitive
//                                      per OES, so it can't ride the
//                                      case-sensitive clsid hash anyway)
//
// First user: the ibValue factory (valueFactory.cpp). The per-metadata factories
// (ibMetaDataDataProcessor / ibMetaDataReport — today raw std::find_if over their
// own m_factoryCtors with an activeMetaData fallback) are the next adopters: same
// three lookups, same shape. Templated so they can reuse it without copying.
// =============================================================================
template <class T>
class ibCtorRegistry {
	std::unordered_map<ibClassID, T*>       m_byClsid;   // hot — primary store + index
	std::unordered_map<std::type_index, T*> m_byType;    // hot — live-object self-id index

public:
	bool   IsEmpty() const { return m_byClsid.empty(); }
	size_t Size()    const { return m_byClsid.size(); }

	// The ONLY mutators. Both indices are filled / cleared here, with the keys
	// read off the same ctor, so they stay in lock-step by construction.
	void Register(T* ctor) {
		m_byClsid.emplace(ctor->GetClassType(), ctor);
		const std::type_info& typeInfo = ctor->GetTypeInfo();
		if (typeInfo != typeid(void))   // meta/control ctors carry no concrete C++ type -> no self-id index
			m_byType.emplace(std::type_index(typeInfo), ctor);
	}
	void Unregister(T* ctor) {
		m_byClsid.erase(ctor->GetClassType());
		const std::type_info& typeInfo = ctor->GetTypeInfo();
		if (typeInfo != typeid(void))
			m_byType.erase(std::type_index(typeInfo));
	}

	// --- lookup: one overload per key ---
	T* Find(const ibClassID& clsid) const {
		const auto it = m_byClsid.find(clsid);
		return it != m_byClsid.end() ? it->second : nullptr;
	}
	T* Find(const std::type_info& typeInfo) const {
		const auto it = m_byType.find(std::type_index(typeInfo));
		return it != m_byType.end() ? it->second : nullptr;
	}
	// Linear on purpose — name resolution is compile-time / low-frequency, not a
	// hot path. Case-insensitive (OES convention).
	T* Find(const wxString& className) const {
		for (const auto& entry : m_byClsid)
			if (stringUtils::CompareString(className, entry.second->GetClassName()))
				return entry.second;
		return nullptr;
	}

	// Iterate every registered ctor (order-independent — a caller that needs a
	// sorted result sorts it itself). For the rare type / category filters.
	template <typename Fn> void ForEach(Fn&& fn) const {
		for (const auto& entry : m_byClsid) fn(entry.second);
	}
};

#endif // __CTOR_REGISTRY_H__
