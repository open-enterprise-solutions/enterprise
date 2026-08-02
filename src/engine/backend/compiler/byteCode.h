#ifndef __BYTE_CODE_H__
#define __BYTE_CODE_H__

#include "backend/compiler/value.h"
#include "backend/guid.h"

//*******************************************************************************

// Per-entry kind tag for the unified bc symbol table (m_listVar /
// m_listFunc). Discriminator that replaces the m_bContext +
// m_strContext + m_bExport combo: one find_if + switch-on-kind, no
// post-fact "found but ctx empty → drop" hack at the emitter site.
//
//   - Local        — user-declared private frame var. Visible only
//                    inside the owning bc; not visible cross-bc.
//                    m_slotIndex = frame slot.
//   - Export       — user-declared export. Same frame-slot semantics
//                    as Local; visibility extends cross-bc through
//                    FindVariable. PrepareNames of business objects
//                    picks up Export entries as props.
//   - External     — runtime-bound by the binder, no prop introspection.
//                    m_slotIndex = extern-slot (binder fills it).
//   - Context      — top-level binding (`Manager`, `ThisForm`). Same
//                    extern-slot semantics as External; the distinction
//                    is that Context exposes a self-ref helper whose
//                    props/methods become bare-callable (Catalogs,
//                    GetForm) and may carry m_bScoped.
//   - ContextProp  — prop of a Context binding (`Catalogs` of Manager).
//                    m_slotIndex = prop-index in the parent's helper;
//                    m_parentRef points at the Context entry. Emit goes
//                    OPER_GET_A on parent + prop name from the const
//                    pool. No frame slot of its own.
enum class ibVarKind : uint8_t {
	Local,
	Export,
	External,
	Context,
	ContextProp,
	Protected,   // appended (AOT-stable) — visible to children, not config-wide
};

// Function-side discriminator. Three categories — matches the
// user-visible function classification (export / context / local),
// no redundant m_bExport flag:
//   - Local         — user-defined module function, private (visible
//                     only to its own bc). m_lCodeLine is the entry IP.
//   - Export        — user-defined function with explicit `export`,
//                     visible cross-bc. m_lCodeLine is the entry IP.
//   - ContextMethod — Pass-3 method of a Context binding (e.g. GetForm
//                     on Manager). No own IP — calls dispatch via
//                     OPER_CALL_METHOD on the parent binding + method name.
//                     m_strContext holds the parent name; m_parentRef
//                     points at the Context entry in m_listVar.
enum class ibFnKind : uint8_t {
	Local,
	Export,
	ContextMethod,
	// Anonymous lambda — pushed to m_listFunc with synthetic name
	// "<lambda@<lo>>" so debugger / eval / FindFunctionByEntry pick
	// up the same metadata path as named functions. Filtered out of
	// name-keyed lookups via IsLambda() — `<` prefix keeps callers
	// safe even if the filter is missed (no valid identifier starts
	// with it). Cross-bc invisible by definition (anonymous).
	Lambda,
	Protected,   // appended (AOT-stable) — visible to children, not config-wide
};

// Forward decl — full definition lives after ibByteCode so the
// binder can reference ibByteCode::ibByteCodeVarInfo as the
// required-bindings table.
class BACKEND_API ibByteBinder;

// Forward decls for the AOT (Ahead-Of-Time) bytecode persistence
// layer. SerializeAOT writes a compiled ibByteCode to a memory
// stream (intended for sys_bytecode_cache.blob); DeserializeAOT
// reads it back. Keeping these out-of-line in byteCodeAOT.cpp so
// byteCode.h stays free of fileSystem/fs.h.
class ibWriterMemory; // backend/fileSystem/fs.h
class ibReaderMemory; // backend/fileSystem/fs.h

struct ibParamRunUnit {
	wxLongLong_t m_numArray = 0;
	wxLongLong_t m_numIndex = 0;
};

struct ibParamUnit : ibParamRunUnit {
	wxString	 m_strType;			//variable type in English notation (in case of explicit typing)
};

//storing one program step
struct ibByteUnit {

	short		 m_numOper = 0;			//instruction code
	unsigned int m_numString = 0;		//source text number (for error output)
	unsigned int m_numLine = 0;			//source line number (for breakpoints)

	//parameters for instructions:
	ibParamRunUnit	 m_param1;
	ibParamRunUnit	 m_param2;
	ibParamRunUnit	 m_param3;
	ibParamRunUnit	 m_param4;			 // - used for optimization

	wxString	 m_strModuleName;	 // module name (since it is possible to include connections from different modules)
	wxString	 m_strDocPath; 	 	 // unique path to the document
	wxString	 m_strFileName; 	 // path to the file (if external processing)
};

struct ibByteCode {

	// Per-parameter info inside ibByteFunction. Mirrors the runtime
	// shape of compile-context's ibParamVariable so cross-module call
	// emit (PushCallFunction → OPER_SET / OPER_SETCONST) can read
	// directly from bytecode without a live ibCompileContext.
	//
	// m_defaultValue.m_numArray == DEF_VAR_SKIP marks "no default"
	// — caller must supply an arg or fail. Other values feed
	// OPER_SETCONST when caller omits the arg.
	struct ibByteParam {
		bool        m_bByRef       = false;
		ibClassID   m_clsid        = 0;        // 0 = dynamic; for CHECK_TYPE
		ibParamUnit m_defaultValue;             // m_numArray = DEF_VAR_SKIP if none
		// Original-cased parameter name. Folded in from the former
		// parallel ibByteFunction::m_listParamRealName vector — the name
		// now travels with the param's own data (debugger SendStack +
		// lambda missing-arg diagnostics read it).
		wxString    m_strName;
	};

	// Symbol entry in m_listVar / ibByteFunction::m_listLocals (both
	// vectors after the unification). Carries the name (m_strRealName),
	// the runtime frame slot (m_slotIndex), and the kind discriminator
	// (m_kind). Implicit `operator long()` returns m_slotIndex so
	// short-form callers (`(long)entry`) read the slot directly.
	struct ibByteCodeVarInfo {
		long      m_slotIndex = 0;
		ibClassID m_clsid     = 0;       // 0 = dynamic, no CHECK_TYPE needed
		// Scope-local entry (ThisObject / ThisForm / similar) — must
		// NOT be visible to children through the bytecode parent walk.
		// Cross-bc resolve (template FindVariable) skips entries with
		// this flag set. PrepareModuleData stamps it on context-props
		// representing per-instance "self" handles.
		bool      m_bScoped    = false;

		// Discriminator — see ibVarKind header comment. Sole "what is
		// this entry" tag; legacy m_bContext / m_bExport mirrors are
		// gone — readers that need them derive via IsContext() /
		// IsExport() helpers.
		ibVarKind m_kind       = ibVarKind::Local;

		// Convenience predicates — preferred over inline `m_kind == X`
		// at callsites. Symmetric set with ibByteFunction's IsLocal /
		// IsExport / IsContextMethod.
		bool IsLocal()       const { return m_kind == ibVarKind::Local; }
		bool IsExport()      const { return m_kind == ibVarKind::Export; }
		bool IsExternal()    const { return m_kind == ibVarKind::External; }
		bool IsContext()     const { return m_kind == ibVarKind::Context; }
		bool IsContextProp() const { return m_kind == ibVarKind::ContextProp; }
		bool IsProtected()   const { return m_kind == ibVarKind::Protected; }
		bool IsPublic()      const { return IsExport(); }  // access-named alias
		bool IsPrivate()     const { return IsLocal(); }   // access-named alias
		// "Required to bind" — runtime binder fills these slots.
		bool IsBindRequired() const { return m_kind == ibVarKind::External || m_kind == ibVarKind::Context; }
		// "Bindable" — the binder MAY seed this slot. Adds plain Local (a bound
		// local like a constant's Value): not must-bind, no pre-flight checks,
		// filled only when the binder actually carries a value for it.
		bool IsBindable() const { return IsBindRequired() || m_kind == ibVarKind::Local; }
		// "User frame var" — visible to debugger's locals view.
		bool IsUserLocal()    const { return m_kind == ibVarKind::Local || m_kind == ibVarKind::Export; }

		// ContextProp only: index of the parent Context entry in the
		// owning vector. -1 until step 2 — the unification step — when
		// the per-kind maps merge into a single std::vector and the
		// parent reference becomes meaningful as an integer index. Until
		// then, parent is identified by m_strContext (the parent's name).
		long      m_parentRef  = -1;

		// Original-cased name. Canonical identifier for the entry —
		// both m_listVar and m_listLocals are vectors keyed by this
		// field via case-insensitive find_if. Debugger renders this so
		// the variable list matches the user's source.
		wxString  m_strRealName;

		// Compile-time scope nesting depth at the var's declaration
		// site. 0 = fn-frame / module-body level (always visible).
		// 1+ = nested inside that many open `{ }` blocks. Runtime
		// tracks current depth via OPER_CTX_BEGIN (++) / CTX_END (--);
		// SendLocalVariables filters entries by m_scopeDepth <=
		// ctx.m_currentScopeDepth so a debugger paused outside the
		// block (current depth less than entry's stamp) doesn't see
		// rows for vars declared inside it.
		int       m_scopeDepth = 0;

		// Parent-context name for context-prop entries (e.g. "Catalogs"
		// is a prop of "MANAGER"). Empty for regular vars. When set,
		// this entry is NOT a flat extern slot — `m_slotIndex` is a
		// prop index in the parent context value, and access compiles
		// to OPER_GET_A on the parent var. Mirrored from compile-side
		// ibVariable::m_strContext.
		wxString  m_strContext;

		ibByteCodeVarInfo() = default;
		ibByteCodeVarInfo(long slot) : m_slotIndex(slot) {}
		operator long() const { return m_slotIndex; }

		// Construct from compile-side ibVariable. Templated to keep
		// byteCode.h free of compileContext.h — instantiated at the
		// mirror site (compileCode.cpp, which includes both headers).
		// CompileVar must expose: m_numVariable, m_strType, m_kind,
		// m_bScoped, m_strRealName, m_strContext, m_scopeDepth, m_clsid.
		// Temps are filtered out at the mirror site — they never reach
		// bc-level structs.
		template<typename CompileVar>
		explicit ibByteCodeVarInfo(const CompileVar& v)
			: m_slotIndex(v.m_numVariable),
			  m_clsid(v.m_clsid != 0
			          ? v.m_clsid
			          : (v.m_strType.IsEmpty() ? 0 : ibValue::GetIDObjectFromString(v.m_strType))),
			  m_bScoped(v.m_bScoped),
			  m_strRealName(v.m_strRealName),
			  m_scopeDepth(v.m_scopeDepth),
			  m_strContext(v.m_strContext)
		{
			// Compile-side ibVariable now carries the authoritative kind in
			// the SAME ibVarKind enum (set at PushVariable + the extern /
			// access stamps), so the mirror is a straight copy — no more
			// boolean-cascade derivation here.
			m_kind = v.m_kind;
		}
	};

	struct ibByteFunction {

		operator long() const { return m_lCodeLine; }

		long m_lCodeLine = -1;       // entry IP into m_listCode
		bool m_bCodeRet = false;     // true → function (returns); false → procedure

		// Extended fields — populated at compile finalize. Allow a
		// cross-module call to be fully assembled from bytecode
		// without reaching into a live ibCompileContext::ibFunction.
		// Older bytecodes (pre-extension) leave these zero / empty;
		// PushCallFunction's null/size checks still cover that case.
		long      m_lVarCount    = 0;          // # of locals; for frame allocation
		ibClassID m_returnClsid  = 0;          // 0 = dynamic; for return-value CHECK_TYPE
		std::vector<ibByteParam> m_listParam;   // params (count = size(); name on each ibByteParam)

		// Discriminator — sole "what is this entry" tag. m_bContext /
		// m_bExport legacy mirrors are gone; readers derive via the
		// helpers (IsContextMethod / IsExport).
		ibFnKind  m_kind         = ibFnKind::Local;

		// Closure capture — true when this function contains at least
		// one inner lambda (any nesting depth). Set on the compile-side
		// ibFunction during lambda body parse, mirrored here at compile
		// finalize. At Phase B runtime drives heap-allocation of the
		// frame on function entry so escaping lambdas can hold a
		// shared_ptr to it; until Phase B lands, the field is a no-op
		// marker (Phase A is compile-only).
		bool      m_needsHeapFrame = false;

		// Convenience predicates — preferred over inline `m_kind == X`
		// at callsites. Symmetric with ibByteCodeVarInfo's helpers.
		bool IsLocal()         const { return m_kind == ibFnKind::Local; }
		bool IsExport()        const { return m_kind == ibFnKind::Export; }
		bool IsContextMethod() const { return m_kind == ibFnKind::ContextMethod; }
		bool IsLambda()        const { return m_kind == ibFnKind::Lambda; }
		bool IsProtected()     const { return m_kind == ibFnKind::Protected; }
		bool IsPublic()        const { return IsExport(); }  // access-named alias
		bool IsPrivate()       const { return IsLocal(); }   // access-named alias
		// Visible cross-bc — Export and ContextMethod (privates + lambdas filtered).
		bool IsCrossBcVisible() const { return m_kind == ibFnKind::Export || m_kind == ibFnKind::ContextMethod; }

		// ContextMethod only: index of the parent Context entry in
		// ibByteCode::m_listVar (cross-table reference — parent is a
		// variable, not a function). -1 until populated at compile
		// finalize via second-pass scan over m_listVar (analogous to
		// ibByteCodeVarInfo::m_parentRef for ContextProp).
		long      m_parentRef    = -1;

		// Original-cased name as written by the user. Canonical
		// identifier on m_listFunc — vector lookup goes through
		// case-insensitive find_if on this field.
		wxString  m_strRealName;
		// Parent-context name for context-method entries (e.g. method
		// of "MANAGER"). Empty for regular module functions. When set,
		// caller emits OPER_CALL_METHOD on the parent context — analogous
		// to ibByteCodeVarInfo::m_strContext for context-props.
		wxString  m_strContext;

		// Function-scope symbol table — all named variables visible
		// inside this function's frame (params + locals + temps).
		// Slot indices in m_listLocals are relative to THIS function's
		// frame (m_pRefLocVars sized by m_lVarCount), NOT to the module
		// body's frame. Mirrors what the compile-side
		// functionContext->m_listVariable held — debugger reads this
		// when paused inside the function (m_currentFunction != null);
		// for module-body frames it reads the bytecode-level m_listVar.
		// This split is mandatory: module-level slot indices and
		// function-level slot indices live in different ranges (the
		// function's frame is sized by its own var count), so a single
		// flat list would index past the wrong frame.
		//
		// Storage is a vector — symmetric with bc-level m_listVar.
		// Entry name lives on m_strRealName; lookup via find_if +
		// CompareString. Eval host-function path (compileContext.cpp's
		// GetVariable bc walk) reads this to resolve the host's locals
		// at depth=1 from an eval expression.
		std::vector<ibByteCodeVarInfo> m_listLocals;

		// L4-2 LINQ pushdown — the lambda body recorded as the L4 query AST
		// (compiler/lambdaQueryAst.*), set ONLY for a single-parameter
		// lambda whose body is a translatable single expression; null = the
		// pipeline runs in RAM (the always-correct floor). Deliberately NOT
		// serialised into the AOT cache: on a cache hit the AST is absent and
		// the pushdown silently degrades to RAM — correctness is unaffected,
		// so the AOT format needs no bump. (docs/query-language-arc.md §23.5)
		std::shared_ptr<struct ibQueryAstExpr> m_lambdaExprAst;

		ibByteFunction() = default;

		// Construct from compile-side ibFunction. Templated to keep
		// byteCode.h free of compileContext.h — instantiated at the
		// CompileFunction finalize site (compileCode.cpp). CompileFn
		// must expose: m_listParam (each with .m_bByRef, .m_strType,
		// .m_puValue, .m_strName), m_lVarCount, m_kind,
		// m_bCodeRet, m_strType, m_strRealName, m_strContext.
		template<typename CompileFn>
		ibByteFunction(long lAddress, const CompileFn& src)
			: m_lCodeLine(lAddress),
			  m_bCodeRet(src.m_bCodeRet),
			  m_lVarCount(src.m_lVarCount),
			  m_returnClsid(src.m_strType.IsEmpty()
				? 0
				: ibValue::GetIDObjectFromString(src.m_strType)),
			  m_kind(src.m_kind),
			  m_needsHeapFrame(src.m_needsHeapFrame),
			  m_strRealName(src.m_strRealName),
			  m_strContext(src.m_strContext)
		{
			m_listParam.reserve(src.m_listParam.size());
			for (const auto& p : src.m_listParam) {
				ibByteParam bp;
				bp.m_bByRef       = p.m_bByRef;
				bp.m_clsid        = p.m_strType.IsEmpty()
					? 0
					: ibValue::GetIDObjectFromString(p.m_strType);
				bp.m_defaultValue = p.m_puValue;
				bp.m_strName      = p.m_strName;
				m_listParam.push_back(bp);
			}
		}
	};

public:

	long FindMethod(const wxString& strMethodName) const;
	long FindExportMethod(const wxString& strMethodName) const;

	long FindFunction(const wxString& funcName) const;
	long FindExportFunction(const wxString& funcName) const;

	long FindProcedure(const wxString& procName) const;
	long FindExportProcedure(const wxString& procName) const;

	// Bytecode-driven cross-module symbol resolution.
	//
	// Resolve* recursively walks own tables, m_dependencies, and the
	// m_parent chain looking for the named symbol. Returns both the
	// found entry and the depth where it was found — depth feeds
	// m_numArray on the caller's emitted opcode (frames up to root
	// descriptor at runtime).
	//
	// Visibility rule, applied at each recursion level:
	//   - Depth ≤ fullVisDepth: full visibility (private + export).
	//   - Depth >  fullVisDepth: export-only. Privates never leak
	//     beyond the budget.
	//
	// `fullVisDepth` mirrors compile-context's `numCanUseLocalInParent`
	// budget: "how deep can we still see private locals of the
	// callee's parent module?". Default 0 = only own bytecode sees
	// privates; transitively visible = export-only (the conservative
	// cross-module semantic). Eval setup that historically wanted
	// to see N levels of parent locals would pass fullVisDepth=N.
	//
	// All eval / cross-module name lookup goes through this path —
	// no back-pointer to live ibCompileCode remains, which is the
	// foundation for AOT cache + bytecode-only deployment / IP
	// protection (production DB without source columns).
	struct ResolvedFunc {
		const ibByteFunction* fn    = nullptr;
		int                    depth = 0;
		explicit operator bool() const { return fn != nullptr; }
	};
	ResolvedFunc ResolveFunction(const wxString& funcName, int fullVisDepth = 0) const;

	// Convenience overload returning a synthesized compile-context
	// variable for callers that walk bc.m_parent chain in their own
	// loop (e.g. ibCompileContext::GetVariable bytecode-walk
	// fallback). Filters to entries visible cross-bc — Export /
	// External / Context / ContextProp pass; plain Locals stay
	// invisible. Templated so byteCode.h stays free of compileContext.h;
	// CompileVar must have a (wxString, ibByteCodeVarInfo) ctor.
	//
	// No scoped filter here: bc.m_parent chain is constructed
	// genealogically (form → object → manager → root for catalogs
	// etc.; eval inherits its host's chain). Foreign bcs never appear
	// in a parent walk, so scoped bindings (ThisForm / ThisObject) are
	// reachable iff they live on a real ancestor — exactly the legit
	// case. Cross-entity isolation through value chain access
	// (`Catalogs.X.CreateElement().ThisObject`) is enforced at the
	// runtime OPER_GET_A handler / autocomplete / debugger via the
	// m_bScoped flag on the prop entry, NOT here.
	template<typename CompileVar>
	bool FindVariable(const wxString& strVarName, std::shared_ptr<CompileVar>& foundedVar) const {
		auto it = std::find_if(m_listVar.begin(), m_listVar.end(),
			[&](const auto& v) {
				if (v.IsLocal()) return false;
				return stringUtils::CompareString(strVarName, v.m_strRealName);
			});
		if (it == m_listVar.end()) {
			foundedVar = nullptr;
			return false;
		}
		foundedVar = std::make_shared<CompileVar>(strVarName, *it);
		return true;
	}

	// Symmetric overload for functions. Single-tier resolve over the
	// unified m_listFunc with kind filter:
	//   - Export / ContextMethod: visible cross-bc.
	//   - Local: private; not visible cross-bc.
	template<typename CompileFn>
	bool FindFunction(const wxString& strFuncName, std::shared_ptr<CompileFn>& foundedFunc) const {
		auto it = std::find_if(m_listFunc.begin(), m_listFunc.end(),
			[&](const auto& fn) {
				if (fn.IsLocal()) return false;
				return stringUtils::CompareString(strFuncName, fn.m_strRealName);
			});
		if (it == m_listFunc.end()) {
			foundedFunc = nullptr;
			return false;
		}
		foundedFunc = std::make_shared<CompileFn>(strFuncName, *it);
		return true;
	}

	// MakeFunction static factory removed — use the templated
	// `ibByteFunction(long lAddress, const CompileFn& src)` ctor.

	// Factory for the per-execution binding session. Wraps this
	// bytecode's m_listVar (filtered to External/Context entries) in
	// an empty binder; caller fills via SetVar(name, value) and passes
	// (bc, binder) to procUnit::Execute(). Body inline at the bottom
	// of this header — needs ibByteBinder's full definition.
	ibByteBinder CreateBinder(bool delta = true);

	long GetNParams(const long lCodeLine) const {
		auto iterator = std::find_if(m_listFunc.begin(), m_listFunc.end(),
			[lCodeLine](const auto& fn) { return lCodeLine == (long)fn; });
		if (iterator != m_listFunc.end())
			return (long)iterator->m_listParam.size();
		return 0;
	}

	bool HasRetVal(const long lCodeLine) const {
		auto iterator = std::find_if(m_listFunc.begin(), m_listFunc.end(),
			[lCodeLine](const auto& fn) { return fn.m_bCodeRet && lCodeLine == (long)fn; });
		return iterator != m_listFunc.end();
	}

	// Reverse lookup: find the function whose entry-line equals
	// lCodeLine. Used by Execute on function entry to populate
	// ibRunContext::m_currentFunction so the runtime carries
	// bytecode-side metadata and never reads compile-context.
	// Linear scan — typical module has < 100 funcs, no hot-path
	// concern. nullptr if no function starts here (i.e. lCodeLine
	// is module-body or arbitrary jump target).
	const ibByteFunction* FindFunctionByEntry(long lCodeLine) const {
		auto iterator = std::find_if(m_listFunc.begin(), m_listFunc.end(),
			[lCodeLine](const auto& fn) { return lCodeLine == (long)fn; });
		return iterator != m_listFunc.end() ? &(*iterator) : nullptr;
	}

	// AOT persistence — see byteCodeAOT.cpp. Writes / reads the
	// fields needed to reconstruct a compiled bytecode in a fresh
	// session: identity (m_id, m_version, m_descriptorClsid),
	// header (m_strModuleName, m_lStartModule, m_lVarCount,
	// m_bExpressionOnly), bytecode body (m_listCode), const pool
	// (m_listConst — primitives only), symbol tables (m_listVar,
	// m_listFunc with embedded m_listLocals), and dependency
	// id/version pairs. Live pointers (m_parent, m_dependencies,
	// m_compileModule) are NOT persisted; the cache loader
	// resolves them via a session-level registry.
	//
	// Returns false on I/O error or on format mismatch (magic /
	// format-version). Caller treats false as cache miss and
	// recompiles from source.
	//
	// Format constants live in byteCodeAOT.cpp; bump
	// kAOTFormatVersion when the layout changes — readers reject
	// older blobs and fall back to recompile.
	// Exported individually (the struct itself isn't BACKEND_API): the AOT
	// cache API is the public seam tools / tests call across the DLL boundary.
	BACKEND_API bool SerializeAOT(ibWriterMemory& writer) const;
	BACKEND_API bool DeserializeAOT(const ibReaderMemory& reader);

	// ----------------------------------------------------------------
	// Process-wide bytecode registry — keyed by descriptor GUID
	// (m_id). Populated by ibRuntimeModuleDataObject::Compile after
	// every successful compile or cache load; cleared by descriptor
	// teardown. Lookup is shared-locked, mutation is unique-locked.
	//
	// Used by ResolveAndVerifyDependencies (below) to walk
	// m_dependencyIds and reach the live target ibByteCode for each
	// dep, then verify the snapshotted m_dependencyVersions still
	// matches the dep's current m_version.
	// ----------------------------------------------------------------
	static void Register(ibByteCode* bc);
	static void Unregister(const ibGuid& descId);
	static ibByteCode* Find(const ibGuid& descId);

	// Walk m_dependencyIds, look each up via Find, populate
	// m_dependencies with live pointers, and verify
	// m_dependencyVersions[i] == dep->m_version. Returns false on
	// any missing dep or version drift — caller treats this as a
	// stale bytecode (case (c) — cache hit but transitive bytecode
	// graph moved underneath) and recompiles from source.
	bool ResolveAndVerifyDependencies();

	void Reset() {
		m_id = ibGuid();
		m_descriptorClsid = 0;
		m_lStartModule = 0;
		m_lVarCount = 0;
		m_strModuleName = wxEmptyString;
		m_bCompile = false;
		m_bExpressionOnly = false;
		m_parent = nullptr;
		m_listCode.clear();
		m_listConst.clear();
		m_listVar.clear();
		m_listFunc.clear();
		m_dependencies.clear();
		m_dependencyIds.clear();
		m_dependencyVersions.clear();
		m_version = ibGuid();
	}

	//Attributes:

	// Stable identifier for this bytecode. Equals the GUID of the
	// owning module descriptor (catalog/document module, common
	// module, form module, …). One bytecode per descriptor at any
	// moment in time — recompile replaces the bytecode body but
	// reuses the same `m_id`, so dependents pointing at this ID via
	// their m_dependencyIds keep resolving to the same target. ID
	// is therefore "where to look" — never identifies a particular
	// version.
	//
	// Versioning lives elsewhere: a cache row in sys_bytecode_cache
	// is keyed by `m_id` and carries source_hash + metadata_version
	// + compiler_version + kind_bindings_hash. Mismatch on any of
	// those triggers cache miss → recompile → write a new row
	// under the same key.
	//
	// Why GUID, not name: user renames in Designer (e.g. "Utilities"
	// → "Tools") don't touch the descriptor's GUID, so dependent
	// bytecodes' references stay valid. Same way `metadataReader`
	// already references metadata objects through their own
	// `ibGuid` and survives renames.
	ibGuid m_id;

	// Kind of descriptor this bytecode was compiled for (CommonModule
	// / CatalogObjectModule / FormModule / etc. — one of the
	// `g_meta*CLSID` family). Informational + sanity check rather
	// than a strict invariant:
	//
	//  - On load from cache: verify that a bytecode wired into a
	//    "Form" slot was actually compiled for FormModule kind. If
	//    not, the cache row was deployed against a different shape
	//    of host configuration — reject and recompile.
	//  - On AOT upgrade: when OES introduces / changes / splits a
	//    descriptor CLSID, old cache rows are detectable here and
	//    can be migrated or rebuilt explicitly.
	//  - For audit / diag: `gfix-bytecode --list` can group
	//    bytecodes by kind. Useful when chasing "why is this
	//    common-module bytecode 50× larger than typical?".
	//
	// 0 (uninitialised) means "compiled before the field existed" —
	// treated as "any kind" by validators; only opt-in checks fire.
	ibClassID m_descriptorClsid = 0;

	// Per-compile fingerprint. Different from m_id (which is the
	// stable descriptor identity) — m_version changes every time the
	// bytecode is rebuilt (source edit, metadata change, compiler
	// upgrade). Used by dependents to detect "the module I depend
	// on has been recompiled since I was built; my references to its
	// entry points may be stale".
	//
	// Derivation (planned, not yet implemented): SHA-128 of
	//   source_hash || metadata_version || compiler_version ||
	//   kind_bindings_hash. Gives a deterministic fingerprint —
	//   equal source + equal compile environment = equal version.
	//   For now allocated as a fresh random GUID per compile pass;
	//   replacement with the deterministic form is straightforward.
	//
	// Future refinement: split into m_contentVersion (changes on
	// any recompile) and m_exportVersion (changes only on public
	// surface change). Then a dependent only needs to re-resolve
	// when m_exportVersion of a dep moved — internal-only changes
	// don't invalidate dependents. Out of scope for the first cut;
	// single fingerprint is the simpler step, and conservative
	// (more cache misses than strictly needed, but never lets a
	// stale reference through).
	ibGuid m_version;

	const ibByteCode* m_parent = nullptr; //Parent bytecode (for checking) — read-only at runtime

	// Dependency list — bytecodes whose EXPORT symbols are visible from
	// this bytecode at compile / eval time. Today populated only by the
	// runtime tree wiring (parent module of current eval/object/form).
	// Tomorrow: a common module that imports several others contributes
	// each as a separate entry, so a single name lookup can hit
	// multiple module's exports.
	//
	// Semantics:
	//  - Search order: own tables (full visibility) → m_dependencies in
	//    declaration order (export-only) → m_parent chain (legacy
	//    single-chain fallback, also export-only).
	//  - First match wins. Conflicts (same name in two deps) surfaces as
	//    "first listed". Future enhancement: explicit qualification
	//    (`ModuleA.Name`) when ambiguity matters.
	//  - Non-owning. Caller guarantees lifetime ≥ this bytecode.
	//
	// Persistence note: when bytecode is saved (sys_bytecode_cache /
	// AOT deploy), the dependency edge is stored by the dependency's
	// `m_id`, NOT by name. So a Designer rename of a dependency
	// module is invisible to callers — their cache rows stay valid,
	// resolution at load time finds the renamed dependency by ID.
	// Live in-memory layout below holds resolved pointers for fast
	// lookup; resolution from m_id list happens once, on bytecode
	// load.
	std::vector<ibByteCode*> m_dependencies;

	// Persistence-only twin of m_dependencies. Carries the GUIDs of
	// each dependency for serialization / cache storage. Populated
	// in two flows:
	//   - On compile: as each dependency is wired into m_dependencies,
	//     its `m_id` is appended to m_dependencyIds in the same
	//     index. The two vectors stay parallel.
	//   - On load from cache: m_dependencies starts empty; m_dependencyIds
	//     comes from the deserialised blob; a session-level registry
	//     resolves each id → ibByteCode* and populates m_dependencies.
	//     Until resolution, lookup methods see only m_parent.
	//
	// Why a separate vector and not on-demand
	// `std::transform(m_dependencies, &ibByteCode::m_id)`:
	//   1. After load-from-cache, m_dependencies is empty until
	//      resolved; m_dependencyIds is the only source.
	//   2. A dropped / not-yet-loaded dependency is detectable by
	//      "m_dependencyIds[i] non-null but m_dependencies[i] ==
	//      nullptr" — caller can decide to defer / fail / fallback.
	//   3. Stable serialization: writer just serialises the id
	//      vector verbatim, no pointer chasing.
	std::vector<ibGuid> m_dependencyIds;

	// Snapshot of each dependency's m_version at the moment THIS
	// bytecode was compiled. Parallel to m_dependencyIds (same
	// index). At load time, the resolution layer compares the
	// recorded version against the dependency's CURRENT m_version:
	//   match  → A's references to dep are still valid; cache hit.
	//   differ → dep was recompiled since A; A's references may be
	//            stale → A is invalidated and recompiled (cache
	//            miss on A even if A's own source_hash didn't
	//            change).
	// This gives proper transitive cache invalidation through the
	// dependency graph — without it, a recompile of common-module B
	// wouldn't invalidate any of its caller's bytecodes.
	std::vector<ibGuid> m_dependencyVersions;

	// Wire one dependency atomically — bumps all three parallel
	// vectors in lock step:
	//   m_dependencies     ← dep            (live pointer, fast lookup)
	//   m_dependencyIds    ← dep->m_id      (persisted; rename-stable)
	//   m_dependencyVersions ← dep->m_version (snapshot for drift check)
	// Caller passes a fully-built dep (id + version already set).
	// Does not check for duplicates — caller's responsibility if
	// uniqueness matters in their use case.
	void AddDependency(ibByteCode* dep) {
		if (dep == nullptr) return;
		m_dependencies.push_back(dep);
		m_dependencyIds.push_back(dep->m_id);
		m_dependencyVersions.push_back(dep->m_version);
	}

	bool m_bCompile = false; //indication of successful compilation

	// Bytecode body is an eval / expression-block, not a real module
	// or function body. Mirror of compile-side ibCompileCode::m_bExpressionOnly,
	// stamped at CompileExpression time so runtime can answer
	// "is this frame eval?" without reaching to compile-context.
	// Used by eval frame-array walk and bDelta detection in
	// procUnit.cpp.
	bool m_bExpressionOnly = false;

	long m_lVarCount = 0; // number of local variables in the module
	long m_lStartModule = 0; // beginning of the module start position
	wxString m_strModuleName;//name of the executable module to which the bytecode belongs

	std::vector <ibByteUnit> m_listCode;//executable code of the module
	std::vector <ibValue> m_listConst;//list of module constants

	// Unified module symbol table — m_kind discriminates {Local,
	// External, Context, ContextProp}. Replaces the historical trio
	// m_listVar / m_listExportVar / m_listContextProp; export filter
	// and context-prop filter happen at read time via m_kind +
	// m_bExport. Storage is a vector — entry name lives on
	// m_strRealName (single source of truth, was the map key
	// historically); iteration order is stable for AOT serialization.
	// ContextProp entries reference their parent Context entry via
	// m_parentRef (index into this vector, populated at compile
	// finalize).
	std::vector<ibByteCodeVarInfo> m_listVar;
	// Unified function table — m_kind discriminates {Local, Export,
	// ContextMethod}. Replaces the historical trio
	// m_listFunc / m_listExportFunc / m_listContextFunc. Storage is a
	// vector — entry name lives on m_strRealName (single source of
	// truth, was the map key historically); iteration order is stable
	// for AOT serialization. Lookup via find_if + kind filter:
	// PrepareNames callsites read kind == Export (user-declared
	// exports) and skip Local (private) and ContextMethod (those
	// belong to the parent binding's value, not the owning module's
	// helper). ContextMethod entries reference their parent Context
	// binding (a variable in m_listVar) via m_parentRef.
	std::vector<ibByteFunction>    m_listFunc;
};

// Runtime binding session — slot table for a bytecode's required
// bindings. References the bc's m_listVar (the canonical "must-bind"
// declarations: kind ∈ {External, Context}). Holds a parallel
// std::vector<ibValue*> indexed by m_slotIndex of those entries — the
// same slot the runtime frame reads via OPER_GET. Bytecode is the
// canonical owner; the binder is just per-execution slot fill.
//
// AOT path: manager loads bc from disk (m_listVar already populated
// with kind tags + clsid), constructs a binder against m_listVar,
// fills SetVar()s by name, hands to Execute.
class BACKEND_API ibByteBinder {
public:
	ibByteBinder(const std::vector<ibByteCode::ibByteCodeVarInfo>& vars, bool delta = true);

	// Bind a live value to the named slot. Lookup walks `m_vars`,
	// matches kind ∈ {External, Context} entries by name. Silently
	// no-ops if the name isn't declared (extra bindings allowed; the
	// runtime only reads what bytecode declared).
	void SetVar(const wxString& name, ibValue* value);

	bool                          IsDelta()  const { return m_delta; }
	void                          SetDelta(bool delta) { m_delta = delta; }
	const std::vector<ibValue*>&  GetSlots() const { return m_slots; }

private:
	const std::vector<ibByteCode::ibByteCodeVarInfo>& m_vars;
	bool                                              m_delta;
	// Indexed by ibByteCodeVarInfo::m_slotIndex (= runtime frame slot).
	// Sized to max(m_slotIndex among External/Context entries) + 1, so
	// non-extern positions are unused null entries — pre-flight only
	// reads slots that correspond to External/Context vars.
	std::vector<ibValue*>                             m_slots;
};

inline ibByteBinder ibByteCode::CreateBinder(bool delta) {
	return ibByteBinder(m_listVar, delta);
}

#endif // !_BYTE_CODE_H__
