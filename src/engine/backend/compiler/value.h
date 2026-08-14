#ifndef __VALUE_H__
#define __VALUE_H__

#include <memory>
#include <atomic>
#include <type_traits>   // the pointer catch-all below (enable_if / is_base_of / is_same)
#include <typeinfo>
#include <unordered_map>
#include <mutex>
#include <thread>

#include "backend/backend_core.h"
#include "backend/compiler/typeCtor.h"

// Forward declaration - full definition in value_ptr.h included at end of file
template <class T> class ibValuePtr;

class ibValue;

// Cursor-based iteration state. Yielded by ibValue::CreateIterator()
// and driven by OPER_FOREACH / OPER_NEXT_ITER. Wrapped in shared_ptr
// (held by the runtime ibValueIterator), no manual delete.
class BACKEND_API ibValueIteratorState {
public:
	virtual ~ibValueIteratorState() = default;
	// Advance to the next element and copy it into `current`. Returns
	// false when the cursor is past the last element; the caller drops
	// the state. After Reset() the next MoveNext() yields element 0.
	virtual bool MoveNext(ibValue& current) = 0;
	virtual void Reset() = 0;

	// IntelliSense / editor type-hint. Writes a skeleton value of the
	// element type into `current` so the editor's static parser knows
	// what `x` is in `For Each x In container`. Returns false when no
	// typed skeleton is available; caller is expected to pre-initialise
	// `current` and treats it as untyped on a false return. Compile-
	// time only; not invoked at runtime by OPER_FOREACH.
	virtual bool PeekSample(ibValue& /*current*/) const { return false; }
};

extern BACKEND_API const ibValue wxEmptyValue;

// THE PACKED NODE'S TWO FIELD NAMES. Short because they repeat per element in a binary stream,
// stable because they are what a JSON dump shows — and declared HERE, not in the serialiser, because
// more than one place writes into that node: the value base writes the primitives, an enumeration
// writes its member from the template that defines it. Two spellings of the same key is how a value
// gets written under one name and read under another.
inline const wxChar* const kValueFieldClsid = wxT("t");   // the type — written and read FIRST
inline const wxChar* const kValueFieldData  = wxT("v");   // the payload

// Forward declarations for template classes used in ConvertToEnumType/ConvertToEnumValue
template <typename valT> class ibValueEnumerationBase;
template <typename valT> class ibValueEnumerationVariantBase;
template <typename valT> class ibValueEnumeration;

// Forward declarations for CastValue (defined in value_cast.h, included at end of file)
template <typename T, typename U> static inline T* CastValue(U* ptr);
template <typename T, typename U> static inline T* CastValue(const U* ptr);
template <typename T, typename U> static inline T* CastValue(U& ptr);
template <typename T, typename U> static inline T* CastValue(const U& ptr);

class BACKEND_API ibBackendValue {
public:
	virtual ibValue* GetImplValueRef() const = 0;
};

constexpr ibClassID g_valueBooleanCLSID = primitive_to_clsid("VL_BOOL");
constexpr ibClassID g_valueNumberCLSID = primitive_to_clsid("VL_NUMB");
constexpr ibClassID g_valueDateCLSID = primitive_to_clsid("VL_DATE");
constexpr ibClassID g_valueStringCLSID = primitive_to_clsid("VL_STRI");

constexpr ibClassID g_valueNullCLSID = primitive_to_clsid("VL_NULL");

//simple type date
class BACKEND_API ibValue {
public:
	//ATTRIBUTES:
	ibValueTypes m_typeClass;  // 1 byte (enum : unsigned char)
	bool m_bReadOnly;          // 1 byte
public:
	union {
		bool          m_bData;  //TYPE_BOOLEAN
		wxLongLong_t  m_dData;  //TYPE_DATE
		ibValue*      m_pRef;   //TYPE_REFFER (+ VALUE/ENUM/OLE/... aliased)
		// TYPE_CONST_REFFER — read-only view of a NON-owned object (const
		// ibValueMetaObject* from the metadata tree). Aliases m_pRef in storage
		// (same 8 bytes), but the const type documents intent and Reset()/dtor
		// must NEVER ref-count or delete through it — the tree owns the object.
		const ibValue* m_pConstRef;
		// TYPE_STRING — pooled heap ibString, nullptr = empty. A pointer (8)
		// instead of an inline ~40-byte ibString, so a Number/Bool value no
		// longer drags a string buffer (billions of values → big RAM win).
		// SHARES the union (a value is one type at a time): valid ONLY when
		// m_typeClass == TYPE_STRING. All access via GetString()/SetString();
		// the lifetime lives in value.cpp. NEVER delete m_pStr unless
		// STRING — otherwise it aliases m_pRef and you get the random-delete bug.
		ibString*     m_pStr;
	};
	// TYPE_NUMBER — separate by-value. ibNumber is a conditional BigImpl*
	// heap-owner, so its bytes must NEVER share union storage (sharing
	// reinstates the random-delete bug). A pointer would save nothing (ibNumber
	// is already 8 bytes) and only cost a heap alloc per number — stays inline.
	ibNumber m_fData;
public:

	class BACKEND_API ibMemberTable {

		//List of keywords that cannot be variable and function names
		struct ibMemberTableConstructor {

			ibMemberTableConstructor(const wxString& strHelper, const long paramCount, const long lPropAlias = wxNOT_FOUND, const long lData = wxNOT_FOUND)
				: m_strHelper(strHelper), m_paramCount(paramCount), m_lAlias(lPropAlias), m_lData(lData)
			{
			}

			wxString m_strHelper;
			long m_paramCount = 0;
			long m_lAlias, m_lData;
		};

		// Bit-flags for prop visibility / mutability.
		enum ibPropFlags : unsigned char {
			eProp_None     = 0,
			eProp_Readable = 1u << 0,
			eProp_Writable = 1u << 1,
			// Scope-local prop (ThisObject / ThisForm / similar): when
			// the host bc is mirrored to ibByteCode::m_listVar, this
			// flag travels into ibByteCodeVarInfo::m_bScoped, and the
			// cross-bc resolver (template FindVariable) skips the
			// entry. Children resolving `ThisObject` therefore can't
			// silently reach the parent's record.
			eProp_Scoped   = 1u << 2,
		};

		struct ibMemberTableProperty {

			// Flag-based ctor — primary entry point. Use for new
			// callsites and any prop that needs eProp_Scoped or other
			// non-readable/writable bits.
			ibMemberTableProperty(const wxString& strPropName, unsigned int flags, const long lPropAlias = wxNOT_FOUND, const long lData = wxNOT_FOUND)
				: m_fieldName(strPropName), m_lData(lData), m_lAlias((int16_t)lPropAlias), m_flags((uint8_t)flags)
			{
			}

			// Legacy ctor — converts two bools (readable, writable)
			// into the flags word so existing AppendProp(bool, bool,
			// ...) overloads keep working without touching every
			// callsite. Old props default to non-scoped.
			ibMemberTableProperty(const wxString& strPropName, bool readable, bool writable, const long lPropAlias = wxNOT_FOUND, const long lData = wxNOT_FOUND)
				: m_fieldName(strPropName),
				  m_lData(lData), m_lAlias((int16_t)lPropAlias),
				  m_flags((uint8_t)((readable ? eProp_Readable : 0u) | (writable ? eProp_Writable : 0u)))
			{
			}

			// 3-bool ctor — same as legacy plus an explicit `scoped`
			// argument. Readable for callsites that prefer bool args
			// over OR'd flag literals (ThisObject / ThisForm / etc).
			ibMemberTableProperty(const wxString& strPropName, bool readable, bool writable, bool scoped, const long lPropAlias = wxNOT_FOUND, const long lData = wxNOT_FOUND)
				: m_fieldName(strPropName),
				  m_lData(lData), m_lAlias((int16_t)lPropAlias),
				  m_flags((uint8_t)((readable ? eProp_Readable : 0u) | (writable ? eProp_Writable : 0u) | (scoped ? eProp_Scoped : 0u)))
			{
			}

			bool IsReadable() const { return (m_flags & eProp_Readable) != 0; }
			bool IsWritable() const { return (m_flags & eProp_Writable) != 0; }
			bool IsScoped()   const { return (m_flags & eProp_Scoped)   != 0; }

			// WIDTH BY WHAT THE FIELD CARRIES, not by habit. These two were both
			// `long` and are not the same kind of thing at all:
			//
			//   m_lData  — a METAID (SetValueByMetaID / IsDataReference read it).
			//              Full width, stays.
			//   m_lAlias — a small tag: eProcUnit / eProperty / eTable, the largest
			//              being g_aliasExport = 1000. Sixteen bits with room to
			//              spare.
			//
			// One type serving both is why narrowing the flags alone bought nothing
			// earlier: the saving vanished into the field beside it. Ordered wide to
			// narrow, the whole tail now fits the pointer's own alignment slack —
			// 8 + 4 + 2 + 1 = 15, so the record is 16 bytes where it was 24.
			wxString m_fieldName;
			long     m_lData;
			int16_t  m_lAlias;
			uint8_t  m_flags = eProp_Readable | eProp_Writable;
		};

		// Bit-flags for method capabilities — counterpart to ibPropFlags.
		enum ibMethodFlags : unsigned char {
			eMethod_None      = 0,
			eMethod_HasReturn = 1u << 0,   // function (returns) vs procedure (no return)
			eMethod_Scoped    = 1u << 1,   // bc-local — invisible to children
		};

		struct ibMemberTableMethod {

			ibMemberTableMethod(const wxString& strMethodName, const wxString& strHelper, const long paramCount, unsigned int flags, const long lPropAlias = wxNOT_FOUND, const long lData = wxNOT_FOUND)
				: m_fieldName(strMethodName), m_strHelper(strHelper), m_lData(lData), m_lAlias((int16_t)lPropAlias), m_paramCount((int8_t)paramCount), m_flags((uint8_t)flags)
			{
			}

			// Legacy ctor — convert hasRet bool into the flags word.
			ibMemberTableMethod(const wxString& strMethodName, const wxString& strHelper, const long paramCount, bool hasRet, const long lPropAlias = wxNOT_FOUND, const long lData = wxNOT_FOUND)
				: m_fieldName(strMethodName), m_strHelper(strHelper), m_lData(lData),
				  m_lAlias((int16_t)lPropAlias), m_paramCount((int8_t)paramCount),
				  m_flags((uint8_t)(hasRet ? eMethod_HasReturn : 0u))
			{
			}

			bool HasReturn() const { return (m_flags & eMethod_HasReturn) != 0; }
			bool IsScoped()  const { return (m_flags & eMethod_Scoped)    != 0; }

			// Same split as the property record above: m_lData is a metaID and keeps
			// its width, the rest are small tags. Declared arity in this whole tree
			// tops out at FOUR, so a byte is not tight — it is three orders of
			// magnitude of headroom. Wide to narrow: 8 + 8 + 4 + 2 + 1 + 1 = 24,
			// exactly two pointers plus a full 8-byte tail, no padding — where the
			// record used to be 32.
			wxString m_fieldName;
			wxString m_strHelper;
			long     m_lData  = wxNOT_FOUND;
			int16_t  m_lAlias = wxNOT_FOUND;
			int8_t   m_paramCount = 0;
			uint8_t  m_flags = 0;
		};

		// props & methods (the built surface). Constructors (a value may surface
		// SEVERAL) hang off a lazily-allocated vector: only a few value TYPES expose
		// ctors at all (TypeDescription / File / Array), so every per-instance helper
		// carries just the null pointer — not an empty vector header.
		std::unique_ptr<std::vector<ibMemberTableConstructor>> m_ctors;
		std::vector<ibMemberTableProperty> m_props; // tree of attribute names
		std::vector<ibMemberTableMethod> m_methods; // tree of method names

		// ---- bind-based population (push) -------------------------------
		// A contributor appends THIS owner's names into the helper. `ctx` is the
		// owning value (the record / aggregate that holds this helper), or
		// nullptr for type-static names. It is typed `const ibValue*`, not a raw
		// void*, on purpose: the owner is ALWAYS at least an ibValue, and a typed
		// pointer lets a contributor downcast with a plain static_cast that the
		// compiler offsets correctly across multiple-inheritance bases — a void*
		// round-trip would lose that adjustment. A plain comparable (fn, ctx)
		// pair — the wxEvtHandler::Bind idea hand-rolled: no std::function /
		// template instantiation in this hot, everywhere-included header, and
		// identity comparison gives BindOnce dedup + Unbind for free.
	public:
		// Two contributor flavours share one binder list:
		//  - ibNameBinder: a FREE function (type-invariant surfaces built once via
		//    Shared<>), gets the helper + a ctx pointer.
		//  - ibNameFiller: a const MEMBER function of the owning value (per-instance
		//    dynamic surfaces). Build() calls it ON the value, so the contributor
		//    runs with a fully-typed `this` — no ctx cast. Bound through the member
		//    Bind() template below and stored type-erased as a pointer-to-member of
		//    the ibValue base; the hierarchy is diamond-free so the derived->base
		//    pmf cast is unambiguous. This is the ibPropertyValueFunctor /
		//    `(handler->*m_funcHandler)()` idea without a per-binder heap functor.
		using ibNameBinder = void(*)(ibMemberTable& helper, const ibValue* ctx);
		using ibNameFiller = void (ibValue::*)(ibMemberTable& helper) const;
	private:
		struct ibBoundNames {
			const ibValue* m_ctx = nullptr;     // free-fn ctx, or the member-fn target value
			ibNameBinder   m_freeFn = nullptr;  // exactly one of m_freeFn / m_memberFn is set
			ibNameFiller   m_memberFn = nullptr;
			bool           m_tail = false;      // run AFTER all non-tail binders (module exports must
			                                    // follow the class's fixed methods so index-based
			                                    // CallAsFunc dispatch keeps IsNew=0…); set by BindTail.
			bool operator==(const ibBoundNames& o) const {
				return m_ctx == o.m_ctx && m_freeFn == o.m_freeFn && m_memberFn == o.m_memberFn && m_tail == o.m_tail;
			}
		};
		// Contributors, invoked by Build() in bind order — EXCEPT tail-flagged ones,
		// which Build() runs last (a descriptor's module exports).
		//
		// ONE SLOT INLINE, and the vector only past it. Measured shape of the list:
		// every ctor binds exactly once, so a value has ONE contributor; a derived
		// that also binds a base filler has two, and nothing in the tree binds more
		// than that except a descriptor's module exports. The list is also
		// write-once — `Unbind` on a member table has no callsite at all — and its
		// contents are constant per TYPE: same function pointer, same order, only
		// `ctx` differs, and `ctx` is always the owner.
		//
		// So a `std::vector` here was a heap allocation per VALUE for a single
		// element known at compile time. Every Structure, every record object,
		// every form paid it at construction — and a composite pipeline row is a
		// fresh Structure per ROW. The slot costs ~32 bytes inline and removes that
		// malloc/free pair; the vector stays for the rare second binder and
		// allocates nothing while empty.
		ibBoundNames m_binder0;
		std::vector<ibBoundNames> m_binders;   // overflow only — entries 2..N
		uint8_t m_binderCount = 0;

		// The ONE place that knows the storage is split. Everything else asks.
		void AddBinder(const ibBoundNames& b) {
			if (m_binderCount == 0) m_binder0 = b;
			else                    m_binders.push_back(b);
			++m_binderCount;
			m_buildState = kStale;
		}
		bool HasBinder(const ibBoundNames& b) const {
			if (m_binderCount > 0 && m_binder0 == b) return true;
			for (const auto& e : m_binders) if (e == b) return true;
			return false;
		}
		// Rebuilds the list minus whatever `drop` selects. Order is preserved,
		// which matters: Build() runs contributors in bind order.
		template<class Pred>
		void RemoveBinderIf(Pred drop) {
			std::vector<ibBoundNames> kept;
			kept.reserve(m_binderCount);
			if (m_binderCount > 0 && !drop(m_binder0)) kept.push_back(m_binder0);
			for (const auto& e : m_binders) if (!drop(e)) kept.push_back(e);

			m_binderCount = 0;
			m_binders.clear();
			for (const auto& e : kept) AddBinder(e);
			m_buildState = kStale;
		}
		template<class F>
		void ForEachBinder(F&& f) const {
			if (m_binderCount > 0) f(m_binder0);
			for (const auto& e : m_binders) f(e);
		}
		// Build state. A rebuild may be triggered SEQUENTIALLY by any thread (the owner
		// in normal runtime; the debugger inspecting cross-thread), but NEVER in PARALLEL
		// for the same helper — two threads clearing+appending m_props/m_methods at once
		// corrupts them. One atomic byte gives a lock-free claim: CAS Stale->Building wins
		// the right to Build(); a loser just waits for the winner's cache (it must not
		// race a second Build()). No mutex object → zero per-instance footprint, no global
		// serialization. acquire/release: seeing Built implies all the build's writes are
		// visible. Steady state (already Built) is a single relaxed-ish load — no cost.
		enum BuildState : uint8_t { kStale = 0, kBuilding = 1, kBuilt = 2 };
		std::atomic<uint8_t> m_buildState{ kStale };

		// Lazy name->index acceleration for FindProp / FindMethod (the runtime
		// hot path — every Obj.Attr / obj.Method() resolves a name). Keyed on
		// the upper-cased name (lookup is case-insensitive, matching
		// CompareString); value = the FIRST matching vector index, so the result
		// is identical to the old linear "first wins" scan. Used only when the
		// surface is large enough to pay (kFindIndexMin); rebuilt lazily on the
		// first lookup after a structural change. An empty map allocates nothing,
		// so a small or never-searched helper costs zero.
		static constexpr size_t kFindIndexMin = 12;
		// Both name->index maps + their dirty flags live in ONE heap node, allocated
		// lazily only when a surface first crosses kFindIndexMin. The common case (a
		// small or never-searched helper) carries just this null pointer — not two
		// inline std::unordered_map objects (~80 B in Debug). emplace = keep first
		// (lowest index), identical to the old linear "first wins" scan.
		struct FindIndex {
			std::unordered_map<std::wstring, long> prop;
			std::unordered_map<std::wstring, long> method;
			bool propDirty = true;
			bool methodDirty = true;
		};
		mutable std::unique_ptr<FindIndex> m_findIndex;

		void MarkPropDirty()   const { if (m_findIndex) m_findIndex->propDirty = true; }
		void MarkMethodDirty() const { if (m_findIndex) m_findIndex->methodDirty = true; }

		// Allocate-on-demand + rebuild-if-stale; callers gate on size >= kFindIndexMin.
		const std::unordered_map<std::wstring, long>& PropIndex() const {
			if (!m_findIndex) m_findIndex = std::make_unique<FindIndex>();
			if (m_findIndex->propDirty) {
				m_findIndex->prop.clear();
				m_findIndex->prop.reserve(m_props.size());
				for (long i = 0; i < (long)m_props.size(); ++i)
					m_findIndex->prop.emplace(m_props[i].m_fieldName.Upper().ToStdWstring(), i);
				m_findIndex->propDirty = false;
			}
			return m_findIndex->prop;
		}
		const std::unordered_map<std::wstring, long>& MethodIndex() const {
			if (!m_findIndex) m_findIndex = std::make_unique<FindIndex>();
			if (m_findIndex->methodDirty) {
				m_findIndex->method.clear();
				m_findIndex->method.reserve(m_methods.size());
				for (long i = 0; i < (long)m_methods.size(); ++i)
					m_findIndex->method.emplace(m_methods[i].m_fieldName.Upper().ToStdWstring(), i);
				m_findIndex->methodDirty = false;
			}
			return m_findIndex->method;
		}

	public:

		ibMemberTable() {}

	private:
		// Internal — Build() resets the surface before re-running the binders. Not
		// public: nothing outside rebuilds by hand anymore (PrepareNames is gone).
		void ClearHelper() {
			m_ctors.reset();
			m_props.clear();
			m_methods.clear();
			MarkPropDirty(); MarkMethodDirty();
		}
	public:

		// ---- bind API ---------------------------------------------------
		// Free contributor (type-invariant / Shared). Idempotent BindOnce lets a
		// base and a derived each register their own with no guard flag.
		void Bind(ibNameBinder fn, const ibValue* ctx = nullptr) {
			AddBinder(ibBoundNames{ ctx, fn, nullptr });
		}
		void BindOnce(ibNameBinder fn, const ibValue* ctx = nullptr) {
			const ibBoundNames b{ ctx, fn, nullptr };
			if (HasBinder(b)) return;
			AddBinder(b);
		}
		void Unbind(ibNameBinder fn, const ibValue* ctx = nullptr) {
			const ibBoundNames b{ ctx, fn, nullptr };
			RemoveBinderIf([&b](const ibBoundNames& e) { return e == b; });
		}
		// Member contributor: a const method of the owning value. `obj` is the
		// value (typically `this` in its ctor); Build() runs (obj->*fn)(helper),
		// so the contributor sees the fully-typed instance. A class may bind several
		// fillers — they run in bind order, accumulating the surface.
		// The method's class M may be a BASE of the object's class C — a derived ctor
		// binding a base-defined filler (e.g. Ext binding ibValueRecordDataObject's
		// FillBaseMethods) deduces C=Ext, M=base; both upcast to ibValue uniformly.
		//
		// PROJECT INVARIANT — ibValue MUST be the FIRST base (offset 0) of any class
		// that member-binds. Reason: the pmf is type-erased to void(ibValue::*), and on
		// MSVC `ibValue::*` is a single-inheritance pmf (4 bytes, NO this-adjustment
		// slot — ibValue has no bases). Casting a multiple-inheritance pmf to it DROPS
		// the MI this-adjustment, so if ibValue sat at a non-zero offset, Build() would
		// invoke the filler with `this` pointing at the ibValue sub-object instead of
		// the real object → reads members at wrong offsets → garbage / crash. Keep the
		// ibValue-holding base (ibValueDynamicMembers / ibValueStaticMembers, or
		// ibValueFrame which carries them) FIRST in the base list. ibValueForm hit this
		// exactly (ibBackendValueForm was first) and was reordered ibValueFrame-first.
		// The wxASSERT below catches any future violator on its first bind.
		template<class C, class M>
		void Bind(const C* obj, void (M::*fn)(ibMemberTable&) const) {
			static_assert(std::is_base_of<M, C>::value || std::is_same<M, C>::value,
				"Bind: the filler's class must be the object's class or a base of it");
			wxASSERT_MSG(static_cast<const void*>(static_cast<const ibValue*>(obj)) == static_cast<const void*>(obj),
				wxT("ibValue must be the FIRST base (offset 0) of a member-binding class - see PROJECT INVARIANT above; make the ibValue-holding base first"));
			AddBinder(ibBoundNames{ static_cast<const ibValue*>(obj), nullptr, static_cast<ibNameFiller>(fn) });
		}
		template<class C, class M>
		void Unbind(const C* obj, void (M::*fn)(ibMemberTable&) const) {
			const ibBoundNames b{ static_cast<const ibValue*>(obj), nullptr, static_cast<ibNameFiller>(fn) };
			RemoveBinderIf([&b](const ibBoundNames& e) { return e == b; });
		}
		// Tail contributor — Build() runs it AFTER every non-tail binder, so its entries
		// land at the END of the surface regardless of bind order. Used for a
		// descriptor's module exports: they must follow the class's fixed methods
		// (index-based CallAsFunc). Set by the ibRuntimeModuleDataObject ctor. Only one
		// tail is expected; replace any existing one so a re-bind stays idempotent.
		void BindTail(ibNameBinder fn, const ibValue* ctx = nullptr) {
			RemoveBinderIf([](const ibBoundNames& b) { return b.m_tail; });
			AddBinder(ibBoundNames{ ctx, fn, nullptr, /*tail=*/true });
		}
		bool HasBinders() const {
			return m_binderCount > 0;
		}
		// Rebuild the name surface from the bound contributors, in bind order.
		// No-op when nothing is bound, so it is SAFE on a helper still populated
		// the old way (a PrepareNames() override + Append*). Defined out-of-line —
		// a member-contributor call needs the complete ibValue.
		void Build();
		// Lazy trigger for GetPMethods(): no-op once built or when nothing is bound.
		// Lock-free claim — exactly one thread builds; a concurrent caller (debugger)
		// waits for that build instead of starting a second one on the same cache.
		void EnsureBuilt() {
			if (!HasBinders()) return;
			if (m_buildState.load(std::memory_order_acquire) == kBuilt) return;
			uint8_t expected = kStale;
			if (m_buildState.compare_exchange_strong(expected, kBuilding,
					std::memory_order_acq_rel, std::memory_order_acquire)) {
				try {
					Build();   // sets kBuilt (release) at its end
				}
				catch (...) {
					// Don't get stuck in kBuilding — let a later access retry the build.
					m_buildState.store(kStale, std::memory_order_release);
					throw;
				}
			} else {
				// Another thread already claimed the build — wait for it to finish
				// (kBuilt) or fail (kStale); never start a second Build() on the same
				// cache. Virtually never taken: a helper has a single owner thread, and
				// a debugger inspects only while the debuggee is frozen (not mid-Build).
				while (m_buildState.load(std::memory_order_acquire) == kBuilding)
					std::this_thread::yield();
			}
		}
		// Mark the surface stale (mutating values — e.g. Map keys); the next
		// EnsureBuilt() rebuilds from the binders.
		void Invalidate() { m_buildState.store(kStale, std::memory_order_release); }
		bool IsBuilt() const { return m_buildState.load(std::memory_order_acquire) == kBuilt; }

		// Reusable SHARED helper for a TYPE-INVARIANT name surface: one helper
		// per distinct contributor, built once and thread-safely (function-local
		// static init) — no per-class static member / magic-static boilerplate.
		// A static-shape value's DoGetPMethods() is then one line:
		//     return ibMemberTable::Shared<&BindXxxNames>();
		// Only for shapes that DON'T depend on the instance (ctx is nullptr);
		// per-metaobject / mutating values keep their own per-instance helper.
		template<ibNameBinder Binder>
		static ibMemberTable* Shared() {
			static ibMemberTable s;
			static const bool once = [] { s.Bind(Binder); s.Build(); return true; }();
			(void)once;
			return &s;
		}

		// Tier-2: SHARED helper per OWNER (e.g. one per metaobject) — for a shape
		// that is stable per owner but DIFFERS between owners and has MANY instances
		// per owner (record objects: shape = metaobject). Built once per owner key,
		// cached, thread-safe (worker pool). Contributor gets ctx = the owner key.
		// unordered_map element refs are rehash-stable, so the returned pointer stays
		// valid while other threads insert. Call InvalidateSharedByOwner() on config
		// reload (metaobjects rebuilt → stale keys / address-reuse) — it bumps a
		// generation that lazily clears every per-Binder cache.
		static std::atomic<unsigned>& OwnerGeneration() { static std::atomic<unsigned> g{ 0 }; return g; }
		static void InvalidateSharedByOwner() { OwnerGeneration().fetch_add(1, std::memory_order_relaxed); }

		template<ibNameBinder Binder>
		static ibMemberTable* SharedByOwner(const ibValue* owner) {
			static std::unordered_map<const ibValue*, ibMemberTable> s_byOwner;
			static unsigned s_gen = 0;
			static std::mutex s_mtx;
			std::lock_guard<std::mutex> lk(s_mtx);
			const unsigned g = OwnerGeneration().load(std::memory_order_relaxed);
			if (s_gen != g) { s_byOwner.clear(); s_gen = g; }
			ibMemberTable& h = s_byOwner[owner];
			if (!h.IsBuilt()) {       // build under the lock → no race with the NVI wrapper's EnsureBuilt
				h.BindOnce(Binder, owner);
				h.Build();
			}
			return &h;
		}

		inline long AppendConstructor(const wxString& strHelper) { return AppendConstructor(0, strHelper, wxNOT_FOUND, wxNOT_FOUND); }
		inline long AppendConstructor(const wxString& strHelper, const long lCtorNum) { return AppendConstructor(0, strHelper, wxNOT_FOUND, lCtorNum); }
		inline long AppendConstructor(const long paramCount, const wxString& strHelper, const long lCtorNum) { return AppendConstructor(paramCount, strHelper, wxNOT_FOUND, lCtorNum); }
		inline long AppendConstructor(const long paramCount, const wxString& strHelper) { return AppendConstructor(paramCount, strHelper, wxNOT_FOUND, wxNOT_FOUND); }

		inline long AppendConstructor(const long paramCount, const wxString& strHelper, const long lCtorNum, const long lCtorAlias) {
			if (!m_ctors) m_ctors = std::make_unique<std::vector<ibMemberTableConstructor>>();
			m_ctors->emplace_back(strHelper, paramCount, lCtorAlias, lCtorNum);
			return m_ctors->size();
		}

		void CopyConstructor(const ibMemberTable* src, const long lCtorNum) {
			if (lCtorNum < src->GetNConstructors()) {
				if (!m_ctors) m_ctors = std::make_unique<std::vector<ibMemberTableConstructor>>();
				m_ctors->push_back((*src->m_ctors)[lCtorNum]);
			}
		}

		wxString GetConstructorHelper(const long lCtorNum) const {
			if (lCtorNum < 0 || lCtorNum >= GetNConstructors())
				return wxEmptyString;
			return (*m_ctors)[lCtorNum].m_strHelper;
		}

		long GetConstructorAlias(const long lCtorNum) const {
			if (lCtorNum < 0 || lCtorNum >= GetNConstructors())
				return wxNOT_FOUND;
			return (*m_ctors)[lCtorNum].m_lAlias;
		}

		long GetConstructorData(const long lCtorNum) const {
			if (lCtorNum < 0 || lCtorNum >= GetNConstructors())
				return wxNOT_FOUND;
			return (*m_ctors)[lCtorNum].m_lData;
		}

		// Constructor names are surfaced by only a handful of static (Shared) values
		// (TypeDescription / File / Array); every per-instance helper leaves m_ctors null.
		const long int GetNConstructors() const noexcept { return m_ctors ? (long)m_ctors->size() : 0; }

		///////////////////////////////////////////////////////////////////////////////////////////////////////////////

		inline long AppendProp(const wxString& strPropName) { return AppendProp(strPropName, true, true, wxNOT_FOUND, wxNOT_FOUND); }
		inline long AppendProp(const wxString& strPropName, const long lPropNum) { return AppendProp(strPropName, true, true, lPropNum, wxNOT_FOUND); }
		inline long AppendProp(const wxString& strPropName, const long lPropNum, const long lPropAlias) { return AppendProp(strPropName, true, true, lPropNum, lPropAlias); }
		inline long AppendProp(const wxString& strPropName, bool readable, const long lPropNum, const long lPropAlias) { return AppendProp(strPropName, readable, true, lPropNum, lPropAlias); }
		inline long AppendProp(const wxString& strPropName, bool readable, bool writable, const long lPropNum) { return AppendProp(strPropName, readable, writable, lPropNum, wxNOT_FOUND); }

		inline long AppendProp(const wxString& strPropName, bool readable, bool writable, const long lPropNum, const long lPropAlias) {
			const unsigned int flags = (readable ? eProp_Readable : 0u) | (writable ? eProp_Writable : 0u);
			return AppendProp(strPropName, flags, lPropNum, lPropAlias);
		}

		// 3-bool variant — same as legacy plus explicit `scoped`.
		// Out-of-line so the symbol is properly exported from
		// backend.dll for wfrontend / other consumers (otherwise MSVC
		// in Debug emits __imp_ stubs that the inline body can't
		// satisfy across the DLL boundary).
		long AppendProp(const wxString& strPropName, bool readable, bool writable, bool scoped, const long lPropNum, const long lPropAlias);

		// Flag-based variant. Use eProp_Readable | eProp_Writable
		// (or | eProp_Scoped for bc-local entries like ThisObject).
		inline long AppendProp(const wxString& strPropName, unsigned int flags, const long lPropNum, const long lPropAlias) {

			//auto iterator = std::find_if(m_props.begin(), m_props.end(),
			//	[strPropName](const auto& f) { return stringUtils::CompareString(f.m_fieldName, strPropName); });
			//if (iterator != m_props.end())
			//	return std::distance(m_props.begin(), iterator);

			m_props.emplace_back(strPropName, flags, lPropAlias, lPropNum);
			MarkPropDirty();
			return m_props.size();
		}

		void CopyProp(const ibMemberTable* src, const long lPropNum) {
			if (lPropNum < src->GetNProps()) {
				m_props.push_back(src->m_props[lPropNum]);
				MarkPropDirty();
			}
		}

		void RemoveProp(const wxString& strPropName) {
			m_props.erase(
				std::remove_if(m_props.begin(), m_props.end(), [&strPropName](const auto& f) {
					return stringUtils::CompareString(f.m_fieldName, strPropName); }), m_props.end());
			MarkPropDirty();
		}

		long FindProp(const wxString& strPropName) const {
			if (m_props.size() >= kFindIndexMin) {
				const auto& idx = PropIndex();
				const auto it = idx.find(strPropName.Upper().ToStdWstring());
				return it != idx.end() ? it->second : wxNOT_FOUND;
			}
			auto iterator = std::find_if(m_props.begin(), m_props.end(),
				[&strPropName](const auto& f) { return stringUtils::CompareString(f.m_fieldName, strPropName); }
			);
			if (iterator != m_props.end())
				return (long)std::distance(m_props.begin(), iterator);
			return wxNOT_FOUND;
		}

		wxString GetPropName(const long lPropNum) const {
			if (lPropNum < 0 || lPropNum >= GetNProps())
				return wxEmptyString;
			return m_props[lPropNum].m_fieldName;
		}

		long GetPropAlias(const long lPropNum) const {
			if (lPropNum < 0 || lPropNum >= GetNProps())
				return wxNOT_FOUND;
			return m_props[lPropNum].m_lAlias;
		}

		long GetPropData(const long lPropNum) const {
			if (lPropNum < 0 || lPropNum >= GetNProps())
				return wxNOT_FOUND;
			return m_props[lPropNum].m_lData;
		}

		virtual bool IsPropReadable(const long lPropNum) const {
			if (lPropNum < 0 || lPropNum >= GetNProps())
				return false;
			return m_props[lPropNum].IsReadable();
		}

		virtual bool IsPropWritable(const long lPropNum) const {
			if (lPropNum < 0 || lPropNum >= GetNProps())
				return false;
			return m_props[lPropNum].IsWritable();
		}

		// Scope-local props (ThisObject / ThisForm / similar) must
		// not leak across bc boundaries. Pass-3 PrepareModuleData
		// reads this and stamps ibCompileContext::ibVariable::m_bScoped
		// on the freshly pushed entry.
		virtual bool IsPropScoped(const long lPropNum) const {
			if (lPropNum < 0 || lPropNum >= GetNProps())
				return false;
			return m_props[lPropNum].IsScoped();
		}

		const long GetNProps() const noexcept { return m_props.size(); }

		///////////////////////////////////////////////////////////////////////////////////////////////////////////////

		inline long AppendProc(const wxString& strMethodName) { return AppendMethod(strMethodName, wxEmptyString, 0, false, wxNOT_FOUND, wxNOT_FOUND); }
		inline long AppendProc(const wxString& strMethodName, const wxString& strHelper) { return AppendMethod(strMethodName, strHelper, 0, false, wxNOT_FOUND, wxNOT_FOUND); }
		inline long AppendProc(const wxString& strMethodName, const wxString& strHelper, const long lMethodNum, const long lMethodAlias) { return AppendMethod(strMethodName, strHelper, 0, false, lMethodNum, lMethodAlias); }
		inline long AppendProc(const wxString& strMethodName, const wxString& strHelper, const long paramCount, const long lMethodNum, const long lMethodAlias) { return AppendMethod(strMethodName, strHelper, paramCount, false, lMethodNum, lMethodAlias); }
		inline long AppendProc(const wxString& strMethodName, const long paramCount, const wxString& strHelper) { return AppendMethod(strMethodName, strHelper, paramCount, false, wxNOT_FOUND, wxNOT_FOUND); }
		inline long AppendProc(const wxString& strMethodName, const long paramCount, const wxString& strHelper, const long lMethodNum) { return AppendMethod(strMethodName, strHelper, paramCount, false, lMethodNum, wxNOT_FOUND); }
		inline long AppendProc(const wxString& strMethodName, const long paramCount, const wxString& strHelper, const long lMethodNum, const long lMethodAlias) { return AppendMethod(strMethodName, strHelper, paramCount, false, lMethodNum, lMethodAlias); }

		inline long AppendFunc(const wxString& strMethodName) { return AppendMethod(strMethodName, wxEmptyString, 0, true, wxNOT_FOUND, wxNOT_FOUND); }
		inline long AppendFunc(const wxString& strMethodName, const wxString& strHelper) { return AppendMethod(strMethodName, strHelper, 0, true, wxNOT_FOUND, wxNOT_FOUND); }
		inline long AppendFunc(const wxString& strMethodName, const long paramCount, const wxString& strHelper) { return AppendMethod(strMethodName, strHelper, paramCount, true, wxNOT_FOUND, wxNOT_FOUND); }
		inline long AppendFunc(const wxString& strMethodName, const wxString& strHelper, const long lMethodNum, const long lMethodAlias) { return AppendMethod(strMethodName, strHelper, 0, true, lMethodNum, lMethodAlias); }
		inline long AppendFunc(const wxString& strMethodName, const wxString& strHelper, const long paramCount, const long lMethodNum, const long lMethodAlias) { return AppendMethod(strMethodName, strHelper, paramCount, true, lMethodNum, lMethodAlias); }
		inline long AppendFunc(const wxString& strMethodName, const long paramCount, const wxString& strHelper, const long lMethodAlias) { return AppendMethod(strMethodName, strHelper, paramCount, true, wxNOT_FOUND, lMethodAlias); }
		inline long AppendFunc(const wxString& strMethodName, const long paramCount, const wxString& strHelper, const long lMethodNum, const long lMethodAlias) { return AppendMethod(strMethodName, strHelper, paramCount, true, lMethodNum, lMethodAlias); }

		inline long AppendMethod(const wxString& strMethodName, const long paramCount, bool hasRet, const long lMethodNum, const long lMethodAlias) { return AppendMethod(strMethodName, wxEmptyString, paramCount, hasRet, lMethodNum, lMethodAlias); }

		inline long AppendMethod(const wxString& strMethodName, const wxString& strHelper, const long paramCount, bool hasRet, const long lMethodNum, const long lMethodAlias) {

			//auto iterator = std::find_if(m_methods.begin(), m_methods.end(),
			//	[strMethodName](const auto& f) { return stringUtils::CompareString(f.m_fieldName, strMethodName); });
			//if (iterator != m_methods.end())
			//	return std::distance(m_methods.begin(), iterator);

			m_methods.emplace_back(strMethodName, strHelper, paramCount, hasRet, lMethodAlias, lMethodNum);
			MarkMethodDirty();
			return m_methods.size();
		}

		void CopyMethod(const ibMemberTable* src, const long lMethodNum) {
			if (lMethodNum < src->GetNMethods()) {
				m_methods.push_back(src->m_methods[lMethodNum]);
				MarkMethodDirty();
			}
		}

		void RemoveMethod(const wxString& strMethodName) {
			m_methods.erase(
				std::remove_if(m_methods.begin(), m_methods.end(), [&strMethodName](const auto& f) {
					return stringUtils::CompareString(f.m_fieldName, strMethodName); }), m_methods.end());
			MarkMethodDirty();
		}

		long FindMethod(const wxString& strMethodName) const {
			if (m_methods.size() >= kFindIndexMin) {
				const auto& idx = MethodIndex();
				const auto it = idx.find(strMethodName.Upper().ToStdWstring());
				return it != idx.end() ? it->second : wxNOT_FOUND;
			}
			auto iterator = std::find_if(m_methods.begin(), m_methods.end(), [&strMethodName](const auto& f) {
				return stringUtils::CompareString(f.m_fieldName, strMethodName); });

			if (iterator != m_methods.end())
				return (long)std::distance(m_methods.begin(), iterator);
			return wxNOT_FOUND;
		}

		wxString GetMethodName(const long lMethodNum) const {
			if (lMethodNum < 0 || lMethodNum >= GetNMethods())
				return wxEmptyString;
			return m_methods[lMethodNum].m_fieldName;
		}

		wxString GetMethodHelper(const long lMethodNum) const {
			if (lMethodNum < 0 || lMethodNum >= GetNMethods())
				return wxEmptyString;
			return m_methods[lMethodNum].m_strHelper;
		}

		long GetMethodAlias(const long lMethodNum) const {
			if (lMethodNum < 0 || lMethodNum >= GetNMethods())
				return wxNOT_FOUND;
			return m_methods[lMethodNum].m_lAlias;
		}

		long GetMethodData(const long lMethodNum) const {
			if (lMethodNum < 0 || lMethodNum >= GetNMethods())
				return wxNOT_FOUND;
			return m_methods[lMethodNum].m_lData;
		}

		bool HasRetVal(const long lMethodNum) const {
			if (lMethodNum < 0 || lMethodNum >= GetNMethods())
				return true;
			return m_methods[lMethodNum].HasReturn();
		}

		// Scope-local method (bc-local — invisible to children
		// through cross-bc resolution). Symmetric with IsPropScoped.
		bool IsMethodScoped(const long lMethodNum) const {
			if (lMethodNum < 0 || lMethodNum >= GetNMethods())
				return false;
			return m_methods[lMethodNum].IsScoped();
		}

		long GetNParams(const long lMethodNum) const {
			if (lMethodNum < 0 || lMethodNum >= GetNMethods())
				return wxNOT_FOUND;
			return m_methods[lMethodNum].m_paramCount;
		}

		const long GetNMethods() const noexcept { return m_methods.size(); }
	};

public:

	//METHODS:
	 //constructors:
	ibValue();
	ibValue(const ibValue& cParam);
	ibValue(ibValue&& cParam);
	ibValue(ibValue* pParam);
	ibValue(ibBackendValue* pParam);
	ibValue(ibValueTypes nType, bool readOnly = false);

	//copy constructors:
	ibValue(bool cParam); //boolean
	ibValue(signed int cParam); //number
	ibValue(unsigned int cParam); //number
	ibValue(double cParam); //number
	ibValue(const ibNumber& cParam); //number
	ibValue(wxLongLong_t cParam); //date
	ibValue(const wxDateTime& cParam); //date
	ibValue(int nYear, int nMonth, int nDay, unsigned short nHour = 0, unsigned short nMinute = 0, unsigned short nSecond = 0); //date

	// CONST char pointers, and that const is load-bearing. Declared as `char*` these
	// did not match a `const char*` argument at all, so `ibValue v = wxEmptyString`
	// (wxEmptyString IS a `const wxChar*`) fell through to ibValue(bool) — pointer-to-bool
	// is a STANDARD conversion and beats the user-defined one to wxString — and the value
	// became Boolean TRUE. Same trap, same shape, as the const ibValue* overload below.
	ibValue(const char* sParam); //string
	ibValue(const wchar_t* sParam); //string
	ibValue(const wxStringImpl& sParam); //string
	ibValue(const wxString& sParam); //string
	ibValue(ibString&& sParam); //string — native move (runtime string functions)

	// ⭐⭐ ANY OTHER POINTER IS A MISTAKE, AND IT USED TO BECOME `TRUE`.
	//
	// Pointer-to-bool is a STANDARD conversion, so an unrelated `Foo*` handed to anything taking an
	// ibValue beat every user-defined overload here and arrived as Boolean TRUE — silently, and
	// TRUE-because-non-null looks exactly like a value somebody meant. The `const char*` overloads
	// above were half of this trap, closed when `wxEmptyString` turned out to be arriving as a
	// boolean; this is the other half, and it is the general case rather than one more spelling.
	//
	// Deleted rather than defined: there is no sensible ibValue to make out of an arbitrary pointer,
	// so the answer is a compile error naming the callsite.
	//
	// What still passes, and why each is exempt:
	//   * `char*` / `wchar_t*` — a STRING, and the overloads above take them;
	//   * anything derived from ibValue or ibBackendValue — the two pointer constructors above are
	//     for exactly those, and a DERIVED pointer would otherwise match this template exactly and
	//     lose the base overload it was meant for.
	template <class T, class = typename std::enable_if<
		!std::is_same<typename std::remove_cv<T>::type, char>::value &&
		!std::is_same<typename std::remove_cv<T>::type, wchar_t>::value &&
		!std::is_base_of<ibValue, T>::value &&
		!std::is_base_of<ibBackendValue, T>::value>::type>
	ibValue(T*) = delete;

	//destructor:
	virtual ~ibValue();

	//clear values
	void Reset();

	//ref counter
	void IncrRef() { m_refCount.fetch_add(1, std::memory_order_relaxed); }
	void DecrRef() {
		wxASSERT_MSG(m_refCount.load(std::memory_order_relaxed) > 0, "invalid ref data count");
		if (m_refCount.fetch_sub(1, std::memory_order_acq_rel) == 1) delete this;
	}

	//operators:
	void operator = (const ibValue& cParam);
	void operator = (ibValue&& cParam);

	void operator = (bool cParam);
	void operator = (short cParam);
	void operator = (unsigned short cParam);
	void operator = (int cParam);
	void operator = (unsigned int cParam);
	void operator = (float cParam);
	void operator = (double cParam);
	void operator = (const ibNumber& cParam);
	void operator = (const wxDateTime& cParam);
	void operator = (wxLongLong_t cParam);
	void operator = (const wxString& cParam);
	// Character pointers — see the ctor note above. A string literal or wxEmptyString
	// on the right-hand side has no wxString overload to bind to without these, and
	// pointer-to-bool wins the resolution.
	void operator = (const char* cParam);
	void operator = (const wchar_t* cParam);
	void operator = (ibString&& cParam);   // native string assign — moves into m_pStr, no wxString round-trip

	void operator = (ibValueTypes cParam);
	void operator = (ibBackendValue* pParam);
	void operator = (ibValue* pParam);
	// const source (e.g. a const ibValueMetaObject* returned by GetMetaObject()):
	// store a READ-ONLY reference — mutation through this value is blocked
	// (m_bReadOnly). Without this overload `value = constPtr` silently picked
	// operator=(bool) (const ptr → bool) and the object became a Boolean.
	void operator = (const ibValue* pParam);

	//Implementation of comparison operators:
	bool operator > (const ibValue& cParam) const { return CompareValueGT(cParam) > 0; }
	bool operator >= (const ibValue& cParam) const { return CompareValueGE(cParam); }
	bool operator < (const ibValue& cParam) const { return CompareValueLS(cParam) < 0; }
	bool operator <= (const ibValue& cParam) const { return CompareValueLE(cParam); }
	bool operator == (const ibValue& cParam) const { return CompareValueEQ(cParam); }
	bool operator != (const ibValue& cParam) const { return CompareValueNE(cParam); }

	const ibValue& operator+(const ibValue& cParam);
	const ibValue& operator-(const ibValue& cParam);

	// Comparison. CompareValueLS / CompareValueGT are the two three-way ordering primitives (<0 / 0 / >0,
	// NULL = smallest, SQL-aligned — see value.cpp): one hook per direction so a class can retune `<` and
	// `>` independently. By default GT follows LS (same total order), GE derives from GT and LE from LS, so
	// a class normally overrides ONE method (CompareValueLS) to change the whole order, yet can still tune
	// an individual operator. EQ/NE stay separate (type-strict equality, distinct from order-equal).
	virtual int  CompareValueLS(const ibValue& cParam) const;
	virtual int  CompareValueGT(const ibValue& cParam) const;
	virtual bool CompareValueGE(const ibValue& cParam) const;
	virtual bool CompareValueLE(const ibValue& cParam) const;
	virtual bool CompareValueEQ(const ibValue& cParam) const;
	virtual bool CompareValueNE(const ibValue& cParam) const;

	//special converting
	template <typename valueType> inline valueType* ConvertToType() const {
		return CastValue<valueType>(this);
	}

	template <typename enumType> inline enumType ConvertToEnumType() const {
		ibValueEnumerationBase<enumType>* enumValue =
			CastValue<ibValueEnumerationBase<enumType>>(this);
		return enumValue ? enumValue->GetEnumValue() : enumType();
	}

	template <typename enumType> inline enumType ConvertToEnumValue() {
		ibValueEnumerationVariantBase<enumType>* enumValue =
			CastValue<ibValueEnumerationVariantBase<enumType>>(this);
		return enumValue ? enumValue->GetEnumValue() : enumType();
	}

	template <typename enumType > inline enumType ConvertToEnumValue() const {
		const ibValueEnumerationVariantBase<enumType>* enumValue =
			CastValue<ibValueEnumerationVariantBase<enumType>>(this);
		return enumValue ? enumValue->GetEnumValue() : enumType();
	}

	//convert to value
	template <typename T> inline bool ConvertToValue(T*& ptr) const {
		if (IsReference()) {
			ibValue* non_const_value = GetRef();
			ptr = dynamic_cast<T*>(non_const_value);
			return ptr != nullptr;
		}
		else if (m_typeClass != ibValueTypes::TYPE_EMPTY) {
			ptr = dynamic_cast<T*>(const_cast<ibValue*>(this));
			return ptr != nullptr;
		}
		return false;
	}

	// True for both an owned object reference (TYPE_REFFER) and a non-owned
	// read-only reference (TYPE_CONST_REFFER). Read paths that resolve through
	// the referenced object (GetRef / GetPMethods / method dispatch / ConvertTo)
	// must treat both alike; only ownership (ref-count / delete) and write paths
	// distinguish them. m_pRef and m_pConstRef alias the same union pointer.
	inline bool IsReference() const {
		return m_typeClass == ibValueTypes::TYPE_REFFER
			|| m_typeClass == ibValueTypes::TYPE_CONST_REFFER;
	}

	// True only for the non-owned, read-only reference. Use to guard object-
	// mutating delegates (SetType / SetPropVal): a const reference must never
	// retype or write a field of the object it does not own. The union aliases
	// const/non-const, so the compiler can't catch this — these runtime checks
	// (+ a Debug wxASSERT) are the only protection.
	inline bool IsConstReference() const {
		return m_typeClass == ibValueTypes::TYPE_CONST_REFFER;
	}

public:

	template<typename T>
	static ibValue CreateObject(ibValue** paParams = nullptr, const long lSizeArray = 0) {
		return CreateObjectRef<T>(paParams, lSizeArray);
	}
	static ibValue CreateObject(const ibClassID& clsid, ibValue** paParams = nullptr, const long lSizeArray = 0) {
		return CreateObjectRef(clsid, paParams, lSizeArray);
	}
	static ibValue CreateObject(const std::type_info& typeInfo, ibValue** paParams = nullptr, const long lSizeArray = 0) {
		return CreateObjectRef(typeInfo, paParams, lSizeArray);
	}
	static ibValue CreateObject(const wxString& className, ibValue** paParams = nullptr, const long lSizeArray = 0) {
		return CreateObjectRef(className, paParams, lSizeArray);
	}
	template<typename T, typename... Args>
	static ibValue CreateObjectValue(Args&&... args) {
		return CreateObjectValueRef<T>(std::forward<Args>(args)...);
	}

	template<typename T>
	static ibValue* CreateObjectRef(ibValue** paParams = nullptr, const long lSizeArray = 0) {
		return CreateObjectRef(typeid(T), paParams, lSizeArray);
	}
	static ibValue* CreateObjectRef(const ibClassID& clsid, ibValue** paParams = nullptr, const long lSizeArray = 0);
	static ibValue* CreateObjectRef(const std::type_info& typeInfo, ibValue** paParams = nullptr, const long lSizeArray = 0) {
		const ibClassID& clsid = GetTypeIDByRef(typeInfo);
		return CreateObjectRef(clsid, paParams, lSizeArray);
	}
	static ibValue* CreateObjectRef(const wxString& className, ibValue** paParams = nullptr, const long lSizeArray = 0) {
		const ibClassID& clsid = GetIDObjectFromString(className);
		return CreateObjectRef(clsid, paParams, lSizeArray);
	}
	template<typename T, typename... Args>
	static ibValue* CreateObjectValueRef(Args&&... args) {
		return CreateAndConvertObjectValueRef<T>(std::forward<Args>(args)...);
	}

	template<typename T>
	static T* CreateAndConvertObjectRef(ibValue** paParams = nullptr, const long lSizeArray = 0) {
		return CastValue<T>(CreateObjectRef(typeid(T), paParams, lSizeArray));
	}
	template<class T = ibValue>
	static T* CreateAndConvertObjectRef(const ibClassID& clsid, ibValue** paParams = nullptr, const long lSizeArray = 0) {
		return CastValue<T>(CreateObjectRef(clsid, paParams, lSizeArray));
	}
	template<class T = ibValue>
	static T* CreateAndConvertObjectRef(const std::type_info& typeInfo, ibValue** paParams = nullptr, const long lSizeArray = 0) {
		return CastValue<T>(CreateObjectRef(typeInfo, paParams, lSizeArray));
	}
	template<class T = ibValue>
	static T* CreateAndConvertObjectRef(const wxString& className, ibValue** paParams = nullptr, const long lSizeArray = 0) {
		return CastValue<T>(CreateObjectRef(className, paParams, lSizeArray));
	}
	template<typename T, typename... Args>
	static T* CreateAndConvertObjectValueRef(Args&&... args) {
		const ibClassID& clsid = ibValue::GetTypeIDByRef(typeid(T));
		if (ibValue::IsRegisterCtor(clsid))
			return new T(args...);
		wxASSERT_MSG(false, "CreateAndConvertObjectValueRef ret null!");
		return nullptr;
	}

	template<typename T, typename valT>
	static ibValue CreateEnumObject(const valT& v) {
		return CreateEnumObjectRef<T>(v);
	}

	template<typename T, typename valT>
	static ibValue* CreateEnumObjectRef(const valT& v) {
		return CreateAndConvertEnumObjectRef<T>(v);
	}

	template<typename T, typename valT>
	static ibValue* CreateAndConvertEnumObjectRef(const valT& v);

	static void RegisterCtor(ibCtorAbstractType* typeCtor);
	static void UnRegisterCtor(ibCtorAbstractType*& typeCtor);
	static void UnRegisterCtor(const wxString& className);

	// (No unregister-by-clsid and no name invalidation here on purpose: THIS registry holds the
	//  STATIC types, whose names are compile-time constants and cannot drift. The metaobject types
	//  — the ones whose name is computed from a renameable object — live in the metadata's own
	//  registry, which is where both live: ibMetaData::UnRegisterCtor / InvalidateCtorNames.)

	static bool IsRegisterCtor(const wxString& className);
	static bool IsRegisterCtor(const wxString& className, ibCtorObjectType objectType);
	static bool IsRegisterCtor(const ibClassID& clsid);

	static ibClassID GetTypeIDByRef(const std::type_info& typeInfo);
	static ibClassID GetTypeIDByRef(const ibValue* objectRef);

	static ibClassID GetIDObjectFromString(const wxString& className);
	static bool CompareObjectName(const wxString& className, ibValueTypes valueType) {
		return stringUtils::CompareString(className, GetNameObjectFromVT(valueType));
	}

	static wxString GetNameObjectFromID(const ibClassID& clsid, bool upper = false);
	static wxString GetNameObjectFromVT(ibValueTypes valueType, bool upper = false);
	static ibValueTypes GetVTByID(const ibClassID& clsid);
	static ibClassID GetIDByVT(const ibValueTypes& valueType);

	static ibCtorAbstractType* GetAvailableCtor(const wxString& className);
	static ibCtorAbstractType* GetAvailableCtor(const ibClassID& clsid);
	static ibCtorAbstractType* GetAvailableCtor(const std::type_info& typeInfo);

	// Static type-trait: do values of this class form a TABLE (a row source rendered as a
	// tablebox), or a scalar attribute? Default = attribute (false). ibValueModel flips the
	// gate to true ONCE, so every model (list / tree / table / dynamic list) inherits it via
	// name-lookup — no per-class override. The class factory reads it through the ctor's T
	// (ibCtorValueType<T>::IsTableValue), so the answer is known by CLSID BEFORE any instance
	// exists (source selection time). NOT a value-representation tag (that is ibValueTypes) —
	// this is the SOURCE ROLE. A reference is never a table; a table is never a reference.
	static bool IsTableValue() { return false; }


	static std::vector<ibCtorAbstractType*> GetListCtorsByType(ibCtorObjectType objectType = ibCtorObjectType::ibCtorObjectType_object_value);

	//static event 
	static void OnRegisterObject(const wxString& className, ibCtorAbstractType* typeCtor) {}
	static void OnUnRegisterObject(const wxString& className) {}

	//factory version 
	static unsigned int GetFactoryCountChanges();

public:

	//special copy & move function
	inline void Copy(const ibValue& cOld);
	inline void Move(ibValue&& cOld);

	void FromDate(int& nYear, int& nMonth, int& nDay) const;
	void FromDate(int& nYear, int& nMonth, int& nDay, unsigned short& nHour, unsigned short& nMinute, unsigned short& nSecond) const;
	void FromDate(int& nYear, int& nMonth, int& nDay, int& DayOfWeek, int& DayOfYear, int& WeekOfYear) const;

#pragma region serialization

	// THE HEADER IS THE BASE'S JOB, the contents are the children's (DoSerialize,
	// further down). Serialize writes what every value has — its type — and then
	// asks the value to fill in what only it knows; Deserialize mirrors it.
	//
	// Splitting it this way is what keeps a new type honest: it overrides one
	// method, describes only its own contents, and cannot forget to write the
	// type or spell the header differently from everybody else.
	//
	// WHY A NODE. ibDataNode is the same tree metadata is written through, and it
	// already has providers: binary for storage, JSON for a wire or a dump. One
	// description of what a value IS, and the choice of representation stays with
	// the provider — a text form is a rendering, not a second implementation.
	//
	// A value is BLIND to metadata: it packs and unpacks ITSELF and never reaches
	// for a configuration.
	bool Serialize(class ibDataNode& node) const;
	bool Deserialize(const class ibDataNode& node);

	// CREATING a value from a node — THE mechanism, in one place.
	//
	// A reader holding a node has no value yet, so the type in the header has to
	// become an instance. That is a registry question, and this answers it from
	// the VALUE registry: the built-in classes, the ones that exist whether or
	// not a configuration is open.
	//
	// A metadata is a step in FRONT of this, not a copy of it: it creates the
	// types only it has — a catalog reference, an enum member, whose ids come
	// from metaIDs no static table knows — and redirects everything else here.
	// One mechanism, reached from both doors.
	//
	// THROWS when the type is registered nowhere, when creation fails, or when
	// the value cannot read its own contents. An empty would be
	// indistinguishable from a value that legitimately IS empty.
	static ibValue FromNode(const class ibDataNode& node);
#pragma endregion

	//Virtual methods:
	virtual void SetType(ibValueTypes type);
	virtual ibValueTypes GetType() const;

	virtual bool IsEmpty() const;

	// SQL / explicit NULL: the `Null` literal, and a NULL DB column (the driver yields ibValue(TYPE_NULL)).
	// Distinct from IsEmpty (TYPE_EMPTY = Undefined — a COMPOSITE value with no type chosen yet). Only
	// this is the SQL null the query layer keys on (three-valued filter, null ordering, join-key skip);
	// an empty reference (type chosen, no guid) and Undefined are NOT it.
	bool IsNull() const { return GetType() == ibValueTypes::TYPE_NULL; }

	virtual wxString GetClassName() const;
	virtual ibClassID GetClassType() const;

	// May this value cross into ANOTHER session?
	//
	// Everything the interpreter passes around within one session is fine; the
	// question only arises at a session boundary — handing arguments to a
	// background job, which runs on its own session and its own thread. Nothing
	// is serialised on the way (one process, one address space), so what is being
	// asked is about OWNERSHIP, not transport: is this value safe for a second
	// session to hold and read?
	//
	// Default YES, because the overwhelming majority of values are — numbers,
	// strings, dates, references, enumeration values: immutable, owned by nobody.
	// A type says NO when it is either
	//   - MUTABLE and owned by its session (a form, an open recordset, a live
	//     object): two sessions mutating one object coordinate through nothing; or
	//   - BOUND to its session's runtime (a lambda holds m_parentBc, a pointer
	//     into the compiling session's bytecode — which is per-session, built by
	//     CompileRoot; an iterator is a cursor over somebody else's collection; an
	//     OLE handle belongs to the thread that created it).
	//
	// Answering here rather than switching on the tag at the boundary is what
	// keeps the rule with the type: a new value kind states its own case, and the
	// job layer never grows a list of what it knows about.
	//
	// TYPE_REFFER and TYPE_CONST_REFFER are ALIASES, not things of their own — both
	// hop to the object they wrap and let IT answer. Without the hop the wrapper
	// would say "yes" on behalf of whatever it points at, which is exactly the
	// case that matters: a form, an object or a lambda is almost always reached
	// through one. Const-ness is not the question either — a read-only alias to a
	// mutable object still aliases a mutable object.
	virtual bool IsTransferable() const {
		if (m_pRef != nullptr
		 && (m_typeClass == ibValueTypes::TYPE_REFFER
		  || m_typeClass == ibValueTypes::TYPE_CONST_REFFER))
			return m_pRef->IsTransferable();
		return true;
	}

	virtual bool Init() {
		if (m_pRef != nullptr && m_typeClass == ibValueTypes::TYPE_REFFER)
			return m_pRef->Init();
		return true;
	}

	virtual bool Init(ibValue** paParams, const long lSizeArray) {
		if (m_pRef != nullptr && m_typeClass == ibValueTypes::TYPE_REFFER)
			return m_pRef->Init(paParams, lSizeArray);
		return true;
	}

	virtual void SetValue(const ibValue& varValue);

	virtual bool SetBoolean(const wxString& strValue);
	virtual bool SetNumber(const wxString& strValue);
	virtual bool SetDate(const wxString& strValue);
	virtual bool SetString(const wxString& strValue);
	virtual bool SetString(ibString&& strValue);   // native — steals the buffer, no wxString round-trip
	// Character pointers — the same trap the ctor note above describes, one door further along.
	// With only the two overloads above visible, a string literal reaches EITHER of them through
	// exactly one user-defined conversion, so `SetString(wxT("x"))` is AMBIGUOUS to GCC and Clang
	// while MSVC ranks it and compiles — invisible to the local build by construction. The ctor
	// and operator= closed this by declaring the pointer forms; the setter had not.
	// Non-virtual on purpose: they forward to the virtual wxString overload, so a derived type
	// still decides what a string assignment means.
	bool SetString(const char* sParam) { return SetString(wxString(sParam)); }
	bool SetString(const wchar_t* sParam) { return SetString(wxString(sParam)); }

	virtual bool FindValue(const wxString& findData, std::vector<ibValue>& listValue) const;

	void SetData(const ibValue& varValue); //setting the value without changing the type

	virtual ibValue GetValue(bool getThis = false) const;

	// A FRESH, INDEPENDENT COPY — the verb behind `Val`.
	//
	// VIRTUAL, because copying is a type's own business: anything deriving from
	// ibValue must be able to state how it duplicates itself, and some types have
	// a cheaper or a truer answer than the default one.
	//
	// THE DEFAULT IS A MECHANISM, not a refusal. A primitive is its own copy.
	// Anything else copies THE WAY IT TRAVELS: it packs itself into a node and is
	// then CREATED from what it packed, which runs the registered constructor for
	// its class and hands the new instance its own contents back. So a type that
	// already describes DoSerialize / DoDeserialize gets a correct copy without
	// writing one, a plugin's type copies exactly as a built-in one does, and
	// "can this value be duplicated" has the same answer as "can it be stored".
	//
	// AND IT RAISES when there is no mechanism and the value is not a primitive —
	// a form, a running object, a lambda. It used to return an EMPTY value there,
	// which is worse than the share it was avoiding: indistinguishable from a
	// value that legitimately is empty. `Val form` is a mistake and is told so at
	// the call, rather than compiling, running and quietly aliasing until the day
	// it matters.
	virtual ibValue Clone() const;

	virtual bool GetBoolean() const;
	virtual int GetInteger() const { return GetNumber().ToInt(); }
	virtual unsigned int GetUInteger() const { return GetNumber().ToUInt(); }
	virtual double GetDouble() const { return GetNumber().ToDouble(); }
	virtual wxDateTime GetDateTime() const { return wxLongLong(GetDate()); }

	virtual ibNumber GetNumber() const;
	virtual wxString GetString() const;

	// Canonical IDENTITY key of the value — for grouping / hierarchy linking where the DISPLAY string
	// is not the identity. A REFFER value is a wrapper whose m_pRef points at the real object, so the
	// base DELEGATES through the reffer chain (like Init / method dispatch) to the target's GetHashKey
	// — the referenced object overrides it (e.g. a reference keys by guid). A plain value keys by its
	// string. Extend per type as needed. Used by the L3 selector engine, which speaks only ibValue.
	virtual wxString GetHashKey() const {
		if (IsReference() && m_pRef != nullptr)
			return m_pRef->GetHashKey();
		return GetString();
	}
	// Native-ibString overload for the runtime string functions. Zero-copy
	// when the value is already TYPE_STRING (returns the stored buffer by
	// const ref); otherwise coerces number/bool/date/ref via GetString()
	// into `scratch` and returns that. The returned ref is valid for the
	// value's lifetime (string case) or `scratch`'s (coerced case).
	virtual const ibString& GetString(ibString& scratch) const;
	virtual wxLongLong_t GetDate() const;

	/////////////////////////////////////////////////////////////////////////

	virtual ibValue* GetRef() const;

	/////////////////////////////////////////////////////////////////////////

	// OPEN THIS VALUE — its card, its editor, whatever window it has.
	//
	// It reports nothing back: whether anything CHANGED is known only to the window that did the
	// editing, and that window is the one that says so (the schedule editor marks the form on OK).
	// A caller here cannot tell "the value changed" from "somebody looked at a linked object and
	// closed it" — a reference opens a whole card and is the same reference afterwards.
	virtual void ShowValue();

	/////////////////////////////////////////////////////////////////////////

#pragma region attribute_support

	// NVI: the single public entry — non-virtual. Everyone calls this; it
	// resolves the per-class helper, lazily builds it, and returns it, so a
	// caller can never get an unbuilt helper. Replaces the per-override
	// EnsureBuilt boilerplate — build lives in ONE place (EnsureMethods).
	ibMemberTable* GetPMethods() const { return EnsureMethods(DoGetPMethods()); }

	// Mark the name surface stale after a STRUCTURAL change (a control added to a
	// form, a module re-registered, …). The next GetPMethods() rebuilds it from the
	// bound contributors. No-op for values with no helper. (Replaces the old
	// PrepareNames() forced rebuild — most creation-time calls are gone, since
	// GetPMethods() already builds lazily on first access.)
	void InvalidateNames() const {
		if (ibMemberTable* helper = DoGetPMethods())
			helper->Invalidate();
	}

protected:
	// Raw per-class helper getter — THE virtual, overridden by value subclasses
	// (each returns its own helper; no build). For a reference, delegate to the
	// referenced object's raw getter; GetPMethods() above does the build.
	virtual ibMemberTable* DoGetPMethods() const {
		return IsReference() && m_pRef != nullptr ?
			m_pRef->DoGetPMethods() : nullptr;
	}

private:
	// Lazy build, in ONE place. Static — no `this`, just acts on the passed
	// helper. `h` is non-const, so EnsureBuilt() needs no const_cast.
	static ibMemberTable* EnsureMethods(ibMemberTable* h) {
		if (h != nullptr) h->EnsureBuilt();
		return h;
	}

public:

	/// Returns number of component properties
	/**
	*  @return number of properties
	*/
	virtual long GetNProps() const;

	/// Finds property by name
	/**
	 *  @param wsPropName - property name
	 *  @return property index or -1, if iterator is not found
	 */
	virtual long FindProp(const wxString& strPropName) const;

	/// Returns property name
	/**
	 *  @param lPropNum - property index (starting with 0)
	 *  @return proeprty name or 0 if iterator is not found
	 */
	virtual wxString GetPropName(const long lPropNum) const;

	/// Returns property value
	/**
	 *  @param lPropNum - property index (starting with 0)
	 *  @return the result of
	 */
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);

	/// Sets the property value
	/**
	 *  @param lPropNum - property index (starting with 0)
	 *  @param varPropVal - the pointer to a variable for property value
	 *  @return the result of
	 */
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);

	/// Is property readable?
	/**
	 *  @param lPropNum - property index (starting with 0)
	 *  @return true if property is readable
	 */
	virtual bool IsPropReadable(const long lPropNum) const;

	/// Is property writable?
	/**
	 *  @param lPropNum - property index (starting with 0)
	 *  @return true if property is writable
	 */
	virtual bool IsPropWritable(const long lPropNum) const;

	/// Is property scope-local (bc-local — invisible to children
	/// through cross-bc resolution; ThisObject / ThisForm / etc.)?
	virtual bool IsPropScoped(const long lPropNum) const;

	/// Returns number of component methods
	/**
	 *  @return number of component  methods
	 */
	virtual long GetNMethods() const;

	/// Finds a method by name 
	/**
	 *  @param wsMethodName - method name
	 *  @return - method index
	 */
	virtual long FindMethod(const wxString& strMethodName) const;

	/// Returns method name
	/**
	 *  @param lMethodNum - method index(starting with 0)
	 *  @return method name or 0 if method is not found
	 */
	virtual wxString GetMethodName(const long lMethodNum) const;

	/// Returns method helper
	/**
	*  @param lMethodNum - method index(starting with 0)
	*  @return method name or 0 if method is not found
	*/
	virtual wxString GetMethodHelper(const long lMethodNum) const;

	/// Returns number of method parameters
	/**
	 *  @param lMethodNum - method index (starting with 0)
	 *  @return number of parameters
	 */
	virtual long GetNParams(const long lMethodNum) const;

	/// Returns default value of method parameter
	/**
	 *  @param lMethodNum - method index(starting with 0)
	 *  @param lParamNum - parameter index (starting with 0)
	 *  @param pvarParamDefValue - the pointer to a variable for default value
	 *  @return the result of
	 */
	virtual bool GetParamDefValue(const long lMethodNum,
		const long lParamNum,
		ibValue& pvarParamDefValue) const;

	/// Does the method have a return value?
	/**
	 *  @param lMethodNum - method index (starting with 0)
	 *  @return true if the method has a return value
	 */
	virtual bool HasRetVal(const long lMethodNum) const;

	// LINQ — universal pipeline ops (Where / Select / OrderBy / ...) on
	// every iterable value. Resolved at compile time by name via
	// FindLinqMethodByName and emitted as OPER_CALL_LINQ (NOT
	// OPER_CALL_METHOD); the opcode carries the ibLinqMethod enum id
	// directly. Runtime dispatch goes through virtual DispatchLinqMethod —
	// derived classes don't have to teach FindMethod / CallAsFunc about
	// LINQ surface.

	// LINQ method id — strongly-typed enum. Single source of truth
	// for method-name resolution: name-string == enum-value's
	// stringization (`Where` enumerator ↔ `"Where"` script-side
	// method name). Adding a new op = append here AND an entry in
	// GetLinqMethodTable() + a case in the dispatch switch
	// (procUnitLinq.cpp). Runtime arg-count validation lives inside
	// each dispatch case.
	enum class ibLinqMethod : long {
		Where,
		Select,
		Count,
		ToArray,
		First,
		Any,
		Distinct,
		OrderBy,
		OrderByDescending,
		GroupBy,
		Join,
		Skip,
		Take,
		SkipWhile,
		TakeWhile,
		Reverse,
		Concat,
		Union,
		Intersect,
		Except,
		Last,
		LastOrDefault,
		Single,
		SingleOrDefault,
		FirstOrDefault,
		ElementAt,
		ElementAtOrDefault,
		Contains,
		SequenceEqual,
		Aggregate,
		WhereIndexed,
		SelectIndexed,
		ToTable,        // materialise into the built-in value table (data sources / Queryable)
		SelectMany,     // flatten: fn(elem) -> a source, and its elements are yielded in turn
	};

	// Method-table entry — enum id + script-side name + one-line help
	// for IntelliSense tooltips. Same table services compile-side
	// FindLinqMethodByName (name → enum) and frontend autocomplete
	// (after `.`, append every LINQ method as a suggestion). Single
	// source of truth: no separate IB_LINQ_TRY macro per name in the
	// resolver, no parallel list in the autocomplete loader.
	struct ibLinqMethodInfo {
		ibLinqMethod   id;
		const wchar_t* name;
		const wchar_t* helper;
	};
	static const std::vector<ibLinqMethodInfo>& GetLinqMethodTable();

	// Compile-time resolver — script method name → enum value (or
	// -1 if not a LINQ method). Used by the chain-method emit path
	// (compileCode.cpp) to decide OPER_CALL_METHOD (per-class) vs
	// OPER_CALL_LINQ (universal pipeline op). Case-insensitive match
	// per OES convention. Implemented in terms of GetLinqMethodTable().
	static long FindLinqMethodByName(const wxString& strMethodName);

	// LINQ dispatch entry point — virtual so subclasses can override
	// kind-specific behavior (e.g. ibValueQuery extending an existing
	// pipeline rather than wrapping via CreateIterator + new state;
	// ibValueArray returning concrete Array on ToArray() rather than
	// materialising into a fresh one; DB-backed values pushing
	// OrderBy / GroupBy / Join down to SQL). Default impl walks
	// TYPE_REFFER chain to the underlying object, then dispatches
	// through the central state-class machinery (file-scope helper
	// in procUnit.cpp). One virtual entry rather than 32 keeps
	// vtable bloat down — per-method virtuals can be lifted later
	// when concrete override pressure surfaces (Phase 2 of the
	// virtual-dispatch arc).
	virtual void DispatchLinqMethod(ibLinqMethod method, ibValue& ret,
	                                ibValue** args, long n);

	/// Calls the method as a procedure
	/**
	 *  @param lMethodNum - method index (starting with 0)
	 *  @param paParams - the pointer to array of method parameters
	 *  @param lSizeArray - the size of array
	 *  @return the result of
	 */
	virtual bool CallAsProc(const long lMethodNum,
		ibValue** paParams,
		const long lSizeArray);

	/// Calls the method as a function
	/**
	 *  @param lMethodNum - method index (starting with 0)
	 *  @param pvarRetValue - the pointer to returned value
	 *  @param paParams - the pointer to array of method parameters
	 *  @param lSizeArray - the size of array
	 *  @return the result of
	 */
	virtual bool CallAsFunc(const long lMethodNum,
		ibValue& pvarRetValue,
		ibValue** paParams,
		const long lSizeArray);

public:

	ibValue* GetThis() { return this; }

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	virtual bool SetAt(const ibValue& varKeyValue, const ibValue& varValue);
	virtual bool GetAt(const ibValue& varKeyValue, ibValue& pvarValue);

	ibValue operator[](const ibValue& varKeyValue) {
		ibValue retValue;
		GetAt(varKeyValue, retValue);
		return retValue;
	}
#pragma region iterator_support
	// Cursor-based iteration. Returns a state walked by OPER_FOREACH /
	// OPER_NEXT_ITER; null means "this value isn't iterable" —
	// OPER_FOREACH raises a runtime error in that case. shared_ptr
	// matches the project convention for owned heap pointers.
	//
	// Base default delegates through the TYPE_REFFER chain to the
	// underlying object; every container class supporting script-level
	// `For Each` overrides this with its own walker. The state's
	// PeekSample() doubles as the IntelliSense type hint, so there's
	// no separate "empty element" surface on ibValue.
	virtual std::shared_ptr<ibValueIteratorState> CreateIterator();
#pragma endregion

protected:

#pragma region serialization

	// CONTENTS — the override point, the other half of the pair declared above.
	//
	// The base knows the PRIMITIVES and nothing else, which is all it can
	// honestly claim. A composite (array, structure, reference) fills its own
	// child nodes and asks its elements the same question, so the walk continues
	// by itself, one class at a time.
	//
	// A MUTABLE value — a form, an open object, a lambda — overrides nothing and
	// is refused by IsTransferable before any of this runs.
	//
	// Returns false when this value has no packed form: said out loud rather
	// than written as something else.
	virtual bool DoSerialize(class ibDataNode& node) const;
	virtual bool DoDeserialize(const class ibDataNode& node);

#pragma endregion
private:
	// NOTE: this sits at the END of the class, so under MSVC's declaration-order
	// layout it occupies the TAIL word — the intended repack next to the two
	// 1-byte scalars (to fill the hole before the 8-aligned union) never
	// happened, and that hole is still padding. See docs/value-audit.md.
	// std::atomic (not wxAtomicInt) — same 4 bytes, but a defined memory
	// model; part of the incremental wxBase→std migration. Never copied /
	// moved: ibValue's copy/move ctors value-init it to 0, Copy/Move only
	// touch the payload.
	std::atomic<unsigned int> m_refCount;
};

// ---------------------------------------------------------------------------
// Aggregate-value bases (NVI). ibValue itself stays BARE for primitives
// (Number / String / Boolean / Date / Guid — no methods/props; the inherited
// DoGetPMethods() returns nullptr, zero helper machinery). Values that DO have
// a name surface inherit one of the two bases below, so DoGetPMethods is
// PROTECTED by construction (no public raw-getter override anywhere) and the
// helper lifetime / wiring lives in one place. The hierarchy is diamond-free by
// design — capability mixins never derive ibValue — so replacing a class's
// `ibValue` base slot with one of these introduces ibValue exactly once.
// ---------------------------------------------------------------------------

// PER-INSTANCE dynamic values (records, Map keys, ResultSet columns, table rows):
// own the helper BY VALUE (auto create/destroy, no new/wxDELETE), built lazily by
// the NVI wrapper. A descendant supplies its name surface by binding one or more
// of its own const member fillers in its ctor —
// `m_members.Bind(this, &Class::FillXxx)` — and calls m_members.Invalidate()
// when it mutates. Several fillers accumulate in bind order, so a class can split
// its surface (e.g. methods + data members) and a derived class can add to or
// replace the base's (Unbind the base filler first to replace).
class BACKEND_API ibValueDynamicMembers : public ibValue {
public:
	ibValueDynamicMembers(ibValueTypes type = ibValueTypes::TYPE_VALUE, bool readOnly = false) : ibValue(type, readOnly) {}
protected:
	mutable ibMemberTable m_members;
	virtual ibMemberTable* DoGetPMethods() const override { return &m_members; }
	// Each concrete value binds its own contributor(s) in its ctor:
	//   m_members.Bind(this, &Self::FillMembers);
	// Binders accumulate in Build(); compose by also binding the base contributor.
	// Module exports autobind as the helper's tail in the ibRuntimeModuleDataObject ctor.
};

// TYPE-INVARIANT values (Array, Point, File, ...): NO per-instance helper — one
// shared helper per contributor via Shared<Binder>(), built once. Descendants
// just inherit ibValueStaticMembers<&BindXxx> (BindXxx must be a free/static
// function — the deriving class is incomplete in its own base clause).
template<ibValue::ibMemberTable::ibNameBinder Binder>
class ibValueStaticMembers : public ibValue {
public:
	ibValueStaticMembers(ibValueTypes type = ibValueTypes::TYPE_VALUE, bool readOnly = false) : ibValue(type, readOnly) {}
protected:
	virtual ibMemberTable* DoGetPMethods() const override { return ibMemberTable::Shared<Binder>(); }
};

#include "backend/value_ptr.h"
#include "backend/value_cast.h"

/////////////////////////////////////////////////////////////////////////
// ibValue template implementations deferred until ibValuePtr is complete
/////////////////////////////////////////////////////////////////////////

template<typename T, typename valT>
ibValue* ibValue::CreateAndConvertEnumObjectRef(const valT& v) {
	ibValuePtr<ibValueEnumeration<valT>> createdEnum(ibValue::CreateAndConvertObjectRef<T>());
	wxASSERT(createdEnum != nullptr);
	return createdEnum->CreateEnumVariantValue(v);
}

/////////////////////////////////////////////////////////////////////////
// value_register template implementations (deferred from typeCtor.h
// because they require the complete ibValue type)
/////////////////////////////////////////////////////////////////////////

template<typename typeCtor>
value_register<typeCtor>::value_register(typeCtor* so) : m_so(so) {
	try {
		if (m_so != nullptr) {
			ibValue::RegisterCtor(m_so);
		}
	}
	catch (...) {
#ifdef DEBUG
		wxLogDebug(wxT("! failed to register class: %s"), m_so->GetClassName());
#endif
		wxDELETE(m_so);
	}
}

template<typename typeCtor>
value_register<typeCtor>::~value_register() {
	try {
		if (m_so != nullptr) {
			ibValue::UnRegisterCtor(m_so);
		}
	}
	catch (...) {
#ifdef DEBUG
		wxLogDebug(wxT("! failed to unregister class: %s"), m_so->GetClassName());
#endif
		wxDELETE(m_so);
	}
}

#endif
