#ifndef __CTOR_REGISTRY_H__
#define __CTOR_REGISTRY_H__

#include <unordered_map>
#include <typeindex>
#include <typeinfo>
#include <memory>

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
//   - name      -> O(1) hash index, keyed on the UPPER-CASED name (OES lookup is
//                  case-insensitive, so it cannot ride the case-sensitive clsid
//                  hash — it gets an index of its own)
//
// The name index replaced a linear scan that this comment used to justify with
// "not very hot: compile-time resolution of CreateObject(Name)". That was wrong,
// and measurably so: OPER_NEW resolves the class name AT RUNTIME, on every
// object a script creates (procUnit.cpp, `CreateObject(className, ...)`). With
// ~180 registered types and stringUtils::CompareString taking ToStdWstring() of
// BOTH sides per comparison, a single `New Structure` was paying ~360 heap
// allocations — measured at 18.5 us per New (tests/bench_runtime.cpp,
// DISABLED_RecordParts). See docs/runtime-perf.md §5.9.
//
// First user: the ibValue factory (valueFactory.cpp). The per-metadata factories
// (ibMetaDataDataProcessor / ibMetaDataReport — today raw std::find_if over their
// own m_factoryCtors with an activeMetaData fallback) are the next adopters: same
// three lookups, same shape. Templated so they can reuse it without copying.
// =============================================================================
template <class T>
class ibCtorRegistry {
	// The registry OWNS its ctors through shared_ptr: one ctor lives in both indices
	// (same shared_ptr), so it survives while either index holds it and is freed when
	// both drop it (Unregister, clear, or registry destruction). No manual delete and
	// no leak on a wholesale drop — destroying the registry frees every ctor. The
	// deleter is captured at Register (complete T there), so the maps can hold
	// shared_ptr<T> even where T is only forward-declared.
	std::unordered_map<ibClassID, std::shared_ptr<T>>       m_byClsid;   // hot — primary store + index
	std::unordered_map<std::type_index, std::shared_ptr<T>> m_byType;    // hot — live-object self-id index

	// THE NAME INDEX IS A CACHE OF A COMPUTED VALUE, not a store. A dynamic metatype's ctor derives
	// its name from the METAOBJECT, so a rename changes what every one of them answers while the
	// identities (clsid) stay put. Rather than re-file entries at each rename, the rename just says
	// "stale" and the next lookup rebuilds — one pass however many objects were renamed, and no
	// cost at all if nobody asks by name afterwards.
	//
	// mutable: rebuilding is not a change of STATE, it is bringing the cache up to date with what
	// the ctors already say, so a const lookup may do it.
	mutable std::unordered_map<std::wstring, std::shared_ptr<T>> m_byName;   // hot — every `New <Name>` a script runs
	mutable bool m_namesStale = false;

	// Names are matched case-insensitively, so the index is keyed on the folded
	// form and both sides fold the same way. One conversion per lookup, against
	// the ~180 comparisons (each allocating twice) the linear scan performed.
	static std::wstring NameKey(const wxString& name) { return name.Upper().ToStdWstring(); }

	// Drop the name entry that points AT this identity, whatever string it is filed under. Linear,
	// and deliberately so: it runs on unregister and on rename — never on a lookup — while the
	// alternative (recomputing the key from the ctor) is exactly what breaks when the name behind
	// it has changed.
	void EraseNameEntry(const ibClassID& clsid) {
		for (auto it = m_byName.begin(); it != m_byName.end(); ++it) {
			if (it->second && it->second->GetClassType() == clsid) {
				m_byName.erase(it);
				return;
			}
		}
	}

public:
	bool   IsEmpty() const { return m_byClsid.empty(); }
	size_t Size()    const { return m_byClsid.size(); }

	// The ONLY mutators. Both indices share ONE shared_ptr per ctor, with the keys
	// read off the same ctor, so they stay in lock-step by construction. Register
	// takes ownership of the raw ctor (wraps it); Unregister drops both indices'
	// references (freeing the ctor when the last one goes).
	void Register(T* ctor) {
		std::shared_ptr<T> owned(ctor);
		m_byClsid.emplace(ctor->GetClassType(), owned);
		m_byName.emplace(NameKey(ctor->GetClassName()), owned);
		const std::type_info& typeInfo = ctor->GetTypeInfo();
		if (typeInfo != typeid(void))   // meta/control ctors carry no concrete C++ type -> no self-id index
			m_byType.emplace(std::type_index(typeInfo), owned);
	}
	void Unregister(T* ctor) {
		// Read the keys BEFORE erasing: the registry OWNS the ctor via shared_ptr, so
		// erasing the last index entry drops the last reference and frees *ctor —
		// touching it afterwards is a use-after-free. (typeInfo is a reference to a
		// static std::type_info, so it stays valid after the ctor is gone.)
		const ibClassID       clsid    = ctor->GetClassType();
		const std::type_info& typeInfo = ctor->GetTypeInfo();
		if (typeInfo != typeid(void))
			m_byType.erase(std::type_index(typeInfo));

		// ⚠ THE NAME INDEX IS DROPPED BY VALUE, NOT BY RECOMPUTED KEY. A dynamic metatype's ctor
		// builds its name from the METAOBJECT ("ConstantObject." + GetName()), so after a rename
		// it no longer answers the string it was filed under. Erasing by the recomputed key left
		// the old entry in place forever — a name that resolves to a freed ctor — while the ctor
		// itself was correctly dropped from the clsid index. The identity (clsid) is stable; the
		// name is only a view of it, so the view is found by what it points AT.
		//
		// (A pending rebuild would fix the index anyway, but the entry must go NOW: the shared_ptr
		// it holds is what keeps the ctor alive, and the caller is destroying it.)
		EraseNameEntry(clsid);

		m_byClsid.erase(clsid);
	}

	// A METAOBJECT WAS RENAMED. Nothing is re-filed here — the names are recomputed on the next
	// lookup that needs them (see Find by name). One flag, so a mass rename costs one rebuild and
	// a rename nobody looks up afterwards costs nothing.
	//
	// (Thread-safety is the same as it has always been for this object: Register / Unregister
	// mutate these maps unguarded on every metaobject run and close, so the registry was never
	// safe against a concurrent metadata mutation. The rebuild adds no new kind of hazard — a
	// rename is a Designer-thread edit, exactly like the register/unregister around it.)
	void InvalidateNames() const { m_namesStale = true; }

private:
	// Rebuild the by-name cache from what the ctors answer NOW. No list of kinds to keep in step,
	// nothing for the next metatype to forget, and no way for a stale name to survive pointing at
	// the wrong type.
	void RebuildNames() const {
		std::unordered_map<std::wstring, std::shared_ptr<T>> rebuilt;
		rebuilt.reserve(m_byClsid.size());
		for (const auto& entry : m_byClsid)
			rebuilt.emplace(NameKey(entry.second->GetClassName()), entry.second);
		m_byName.swap(rebuilt);
		m_namesStale = false;
	}

public:

	// --- lookup: one overload per key. Returns a RAW (non-owning) view; callers
	// observe, they don't own — ownership stays with the registry. ---
	T* Find(const ibClassID& clsid) const {
		const auto it = m_byClsid.find(clsid);
		return it != m_byClsid.end() ? it->second.get() : nullptr;
	}
	T* Find(const std::type_info& typeInfo) const {
		const auto it = m_byType.find(std::type_index(typeInfo));
		return it != m_byType.end() ? it->second.get() : nullptr;
	}
	// Case-insensitive (OES convention), through the folded-name index — this is
	// what every `New <Name>` in a script goes through, so it is O(1) with one
	// case fold, not a scan of every registered type.
	//
	// THE RECOMPUTE HAPPENS HERE, and only when a rename said it was needed: the cache is a view of
	// what the ctors compute from their metaobjects, so the moment somebody asks by name after a
	// rename, the view is brought up to date first. Costs nothing on the ordinary path — one bool.
	T* Find(const wxString& className) const {
		if (m_namesStale)
			RebuildNames();
		const auto it = m_byName.find(NameKey(className));
		return it != m_byName.end() ? it->second.get() : nullptr;
	}

	// Iterate every registered ctor (order-independent — a caller that needs a
	// sorted result sorts it itself). For the rare type / category filters.
	template <typename Fn> void ForEach(Fn&& fn) const {
		for (const auto& entry : m_byClsid) fn(entry.second.get());
	}
};

#endif // __CTOR_REGISTRY_H__
