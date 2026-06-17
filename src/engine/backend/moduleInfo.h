#ifndef __MODULE_INFO_H__
#define __MODULE_INFO_H__

#include "backend/compiler/compileModule.h"
#include "backend/compiler/procUnit.h"

#include <memory>

// Marker interface for descriptors that terminate a runtime tree.
// Today: ibValueModuleManagerRuntimeConfiguration. Used by ibRuntimeModuleDataObject::GetRoot
// parent walk to find "the root above this nested descriptor".
class BACKEND_API ibRuntimeRoot {
public:
	virtual ~ibRuntimeRoot() = default;
};

// Canonical helper alias for a descriptor's script-exported names (module exports
// + bound exports). ONE value across all descriptor classes, so the generic export
// autobind (ibRuntimeModuleDataObject::ExportThunk) can append under it without
// knowing each class's per-enum numbering. Distinct from the small per-class
// aliases (eSystem/eProperty/eTable = 0/1/2); a migrated class sets its own
// `eProcUnit = g_aliasExport` so existing dispatch (`alias == eProcUnit`) routes
// the thunk's entries unchanged.
constexpr long g_aliasExport = 1000;

class BACKEND_API ibRuntimeModuleDataObject {
public:

	// Variadic method call — packs the trailing ibValue& arguments into
	// the ibValue*[] array form expected by the underlying ProcUnit.
	// Public surface for fixed-N event handler invocations:
	// ExecAsProc(wxT("BeforeWrite"), cancel). Mirrors
	// ibProcUnit::CallAsProc/Func variadics (void return). Forwards to
	// the protected array form, which resolves the runtime through
	// GetProcUnit() and pins the shared_ptr against fast-F5 UAF
	// (project_refresh_execute_crash.md).
	template <typename... Types>
	void ExecAsProc(const wxString& strMethodName, Types&&... args) const {
		ibValue* paParams[] = { &args..., nullptr };
		ExecAsProc(strMethodName, paParams, (const long)sizeof...(args));
	}

	template <typename... Types>
	void ExecAsFunc(const wxString& strMethodName, ibValue& pvarRetValue, Types&&... args) const {
		ibValue* paParams[] = { &args..., nullptr };
		ExecAsFunc(strMethodName, pvarRetValue, paParams, (const long)sizeof...(args));
	}

	// Allocate the runtime slot (ProcUnit) for this descriptor on
	// demand. No-op in Designer mode (no script execution ever) and
	// when the slot is already populated. Callers that previously
	// inlined the make_shared + DesignerMode check use this single
	// entry instead.
	void InitializeRuntime();

	// Drop the runtime slot — symmetric teardown for InitializeRuntime.
	// Used by DetachRuntime to release this descriptor's
	// ProcUnit at session end. No-op if already empty.
	void ResetRuntime() { m_procUnit.reset(); }

	// Bind a value on this descriptor's compile module. The value is an
	// ibValue that exposes its method table + attributes at compile time.
	// Lazy-creates m_compileModule from the descriptor's meta-object when it
	// isn't wired yet — subclass code reads like a recipe: Bind… → Compile → Run.
	//
	// Three flavours by editor-visibility / storage:
	//   BindContextVariable — named context (m_listContextValue, name VISIBLE
	//     in autocomplete): the "full object" self-handles ThisObject / ThisForm.
	//   BindScopeVariable   — transparent scope container (m_listContextValue,
	//     name NOT an identifier; only its members surface): Manager /
	//     EnumManager / SystemManager. Replaces the old scopeContext=true flag.
	//   BindExportVariable  — export variable (m_listExternValue, name VISIBLE):
	//     global constants, common modules surfaced as module-valued names.
	//   BindLocalVariable   — plain writable LOCAL (m_listLocalValue, kind=Local):
	//     binder fills the frame slot at init, module reads/writes it as an ordinary
	//     local — no required/type pre-flight, no member access. E.g. a constant's Value.
	void BindContextVariable(const wxString& name, class ibValue* value);
	void BindScopeVariable(const wxString& name, class ibValue* value);
	void BindExportVariable(const wxString& name, class ibValue* value);
	void BindLocalVariable(const wxString& name, class ibValue* value);

	// Symmetric teardown for the Bind… family. RemoveVariable erases the name
	// from BOTH the extern and context maps, so one Unbind undoes any flavour
	// of bind. No-op when no compile module is wired yet.
	void UnbindVariable(const wxString& name);

	// Compile this descriptor's module. Skips in Designer mode (the
	// designer only wants AST-level compile state for intellisense and
	// never emits runtime-reachable bytecode). Returns false when no
	// compile module is wired. Backend compile errors propagate as
	// ibBackendException — callers with swallow semantics wrap in
	// their own try/catch.
	bool Compile();

	// Run the compiled top-level of this descriptor on its ProcUnit —
	// initialization entry run at object/form creation, fires handlers
	// registered at module scope. Skips in Designer mode (no execution
	// ever) and when either side of the pair is absent. delta matches
	// ibProcUnit::Execute's semantics: delta=true EXECUTES the module
	// top-level body (the default — almost every descriptor wants its
	// body to run on init: forms, constants, record objects). Pass false
	// to register the module's functions WITHOUT running the body — only
	// common modules do that (moduleManager.cpp), since a common module's
	// top-level is just declarations.
	void Run(bool delta = true);

	// Low-level execute — unconditional (no Designer guard). Prefer
	// Run() in subclass code; Execute() remains for compatibility and
	// for paths that re-Execute on already-compiled state.
	//
	// Two forms:
	//   - Execute(binder): caller supplies a fully-populated ibByteBinder
	//     (descriptor's runtime values for ThisObject / Reference / etc.
	//     filled via SetVar). Preferred — descriptors fill bindings at
	//     execution time, not at compile-time staging.
	//   - Execute(delta): legacy fallback — builds binder from compile-
	//     side maps (m_listExternValue / m_listContextValue /
	//     m_listLocalValue) populated by the Bind*Variable calls at module
	//     init. Kept for paths that haven't migrated to per-execute binding
	//     yet. delta semantics as in Run() above.
	void Execute(ibByteBinder& br);
	void Execute(bool delta = true);

	// The ONLY ctor — a descriptor is always built by its owning value, which passes
	// its already-constructed helper + itself (and, for the module manager, an eager
	// compile module). Registering the export surface as the helper's TAIL here makes
	// it impossible to construct a descriptor without wiring its exports "in the
	// descriptor", and after the value's own fixed methods (stable CallAsFunc indices).
	// No default / compile-module-only ctor: those would silently skip the export wiring.
	ibRuntimeModuleDataObject(ibValue::ibMemberTable& helper, const ibValue* owner,
		ibCompileModule* compileCode = nullptr)
		: m_compileModule(compileCode) { helper.BindTail(&ExportThunk, owner); }
	virtual ~ibRuntimeModuleDataObject();

	// Module-object currently wired in m_compileModule. Thin post-
	// compile accessor — only meaningful AFTER compile is created.
	const ibValueMetaObjectModuleBase* GetMetaObject() const {
		return GetCompileModule() ? GetCompileModule()->GetObjectModule() : nullptr;
	}

	// Compile-target — the single module this descriptor compiles
	// scripts from. Used by BindContextVariable for lazy
	// ibCompileModule creation BEFORE compile is wired.
	//
	// Different concept from the record-data hierarchy's GetMetaObject()
	// which returns a CONTAINER (ibValueMetaObjectGenericData) that
	// holds manager module + object module + potential further modules.
	// Split responsibility:
	//   - GetMetaObject() on record-data    → the container.
	//   - GetMetaForCompile()               → the single module inside
	//     the container that drives THIS descriptor's bytecode
	//     (usually container->GetObjectModule() for catalog/document
	//     objects; form / common module are modules themselves and
	//     return the metadata directly).
	// Default reads through m_compileModule — works for eager-compile
	// subclasses (Unit, Configuration, External DP) whose compile is
	// wired in their ctor; lazy-compile subclasses override.
	virtual const ibValueMetaObjectModuleBase* GetMetaForCompile() const {
		return m_compileModule ? m_compileModule->GetObjectModule() : nullptr;
	}

	virtual ibCompileModule* GetCompileModule() const { return m_compileModule; }
	// Session-aware: when a ibSessionScope is active, first checks the
	// session's ProcUnit map. Falls back to the descriptor-owned
	// m_procUnit (legacy path, still in use on desktop until sessions
	// fully own runtimes). See project_unified_session_architecture.md.
	//
	// Returns shared_ptr — callers MUST keep it alive across any call
	// on the ProcUnit. A raw pointer cached across CallAsProc is a UAF
	// if the session is concurrently removed (fast-F5 crash).
	virtual std::shared_ptr<ibProcUnit> GetProcUnit() const;

	// Parent descriptor in the runtime tree — scope chain for symbol
	// resolution and call-stack frame chain. Raw pointer because
	// parent-outlives-child is an ownership invariant (session owns
	// root through ibValuePtr, root owns nested through
	// m_listCommonModuleManager / form tabs / script locals). Eval is
	// the only case where parent is a live runtime-executing node
	// picked up via Current() at compile time.
	//
	// Side-effect: cascades to the descriptor's compile+runtime layer.
	// If m_compileModule and parent->GetCompileModule() both exist, the
	// compile module's parent is updated (scope chain for symbol
	// resolution). Same for m_procUnit (call stack frame chain). This
	// lets call sites set parent once at the descriptor level instead
	// of separately wiring both compile and runtime parents.
	void SetParent(const ibRuntimeModuleDataObject* parent);
	const ibRuntimeModuleDataObject* GetParent() const { return m_parent; }

	// Walk the parent chain until we hit a node that acts as runtime
	// root (implements ibRuntimeRoot). Configuration is today's sole
	// root; external DPs / reports parent into it. Returns nullptr
	// for orphan descriptors — no root reachable upward.
	virtual const ibRuntimeRoot* GetRoot() const;

	// Surface ONLY this descriptor's EXPORT *methods* into another value's helper,
	// keyed by the bytecode-function index. For a delegating host (a module-backed
	// manager) that routes CallAsProc/Func straight back through this descriptor — the
	// method numbers line up exactly, no copy of the helper table needed. Reads the
	// runtime ProcUnit's bytecode; AttachRuntime's split Init/Run guarantees the
	// wrapper's ProcUnit is wired (Run(false)) before any business code resolves it.
	// Public on purpose: the caller is a sibling value, not a subclass.
	void ExportMethodsToHelper(ibValue::ibMemberTable* helper, long alias) const {
		if (helper == nullptr) return;
		const auto pu = GetProcUnit();
		if (!pu) return;
		const ibByteCode* bc = pu->GetByteCode();
		if (bc == nullptr) return;
		for (const auto& fn : bc->m_listFunc) {
			if (!fn.IsExport()) continue;
			helper->AppendMethod(fn.m_strRealName,
				bc->GetNParams(fn),
				bc->HasRetVal(fn),
				(long)fn,
				alias);
		}
	}

	// Symmetric export-var (prop) half of ExportNamesToHelper. Public to keep the
	// method/prop split parallel. A module-backed manager host surfaces only the
	// method half (export-vars resolve through the host's own ProcUnit alias, which a
	// manager doesn't have); descriptors that own their runtime surface both via
	// ExportNamesToHelper.
	void ExportPropsToHelper(ibValue::ibMemberTable* helper, long alias) const {
		if (helper == nullptr) return;
		const auto pu = GetProcUnit();
		if (!pu) return;
		const ibByteCode* bc = pu->GetByteCode();
		if (bc == nullptr) return;
		for (const auto& v : bc->m_listVar) {
			if (!v.IsExport()) continue;
			helper->AppendProp(v.m_strRealName, v, alias);
		}
	}

protected:
	// Method call (array form). Resolves the runtime through
	// GetProcUnit(); local shared_ptr keeps the ProcUnit alive for the
	// whole call — without it, a concurrent session teardown would drop
	// the last ref and leave pu dangling mid-Execute (fast-F5 UAF
	// crash, project_refresh_execute_crash.md). Used by derived-class
	// CallAsProc(lMethodNum, paParams, lSizeArray) dispatch and by the
	// public variadic forms above. External callers should use the
	// variadic — this raw form stays protected because the array shape
	// is a derived-class implementation detail.
	bool ExecAsProc(const wxString& strMethodName,
		ibValue** paParams, const long lSizeArray) const
	{
		if (auto pu = GetProcUnit())
			return pu->CallAsProc(strMethodName, paParams, lSizeArray);
		return false;
	}

	bool ExecAsFunc(const wxString& strMethodName,
		ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) const
	{
		if (auto pu = GetProcUnit())
			return pu->CallAsFunc(strMethodName, pvarRetValue, paParams, lSizeArray);
		return false;
	}

	// Populate a value's method-helper from this descriptor's bytecode —
	// every kind=Export entry in m_listFunc / m_listVar is appended as
	// a method / prop alias eProcUnit (the canonical alias all
	// descriptor subclasses use for their script-side surface). Replaces
	// the duplicated pattern that lived in 10 PrepareNames callsites
	// (catalog/document/constant/.../form/moduleManager*).
	//
	// No-op when there's no live ProcUnit or no bytecode (Designer mode,
	// not-yet-compiled descriptor). Helper is read through GetProcUnit()
	// to pin the shared_ptr alive for the duration — same pattern as
	// ExecAsProc / ExecAsFunc above.
	// Free name-binder thunk: surface THIS descriptor's bytecode exports + staged
	// context binds (ThisForm.Controls / DataSource / ...) onto the owning value's
	// helper under g_aliasExport. A descriptor value binds it in its ctor —
	// `m_members.Bind(&ibRuntimeModuleDataObject::ExportThunk, this)` — so NO per-class
	// filler does this by hand ("autobind in the descriptor"). This is the ONLY place
	// the descriptor's own names are surfaced — a form/record contributor must NOT call
	// ExportNamesToHelper / FillHelperFromBinds itself (those make an UNGUARDED virtual
	// GetCompileModule() on `this`; here the cross-cast to the descriptor sibling is
	// dynamic_cast-guarded, so a half-constructed / torn-down object degrades to "no
	// names" instead of calling through a null vtable slot). Diamond-free → ibValue is
	// polymorphic, the sibling cross-cast is well-defined.
	static void ExportThunk(ibValue::ibMemberTable& helper, const ibValue* ctx) {
		if (const auto* desc = dynamic_cast<const ibRuntimeModuleDataObject*>(ctx)) {
			desc->ExportNamesToHelper(&helper, g_aliasExport);
			desc->FillHelperFromBinds(&helper, g_aliasExport);
		}
	}

	// Full descriptor surface = methods + export-var props, both keyed by the
	// canonical alias. Self-surfacing descriptors (records/forms) call this via
	// ExportThunk; a delegating host (manager) surfaces only the method half, see
	// the public ExportMethodsToHelper.
	void ExportNamesToHelper(ibValue::ibMemberTable* helper, long alias) const {
		ExportMethodsToHelper(helper, alias);
		ExportPropsToHelper(helper, alias);
	}

	// Materialise this descriptor's EXPORT bindings (RegisterRecords / Filter /
	// Controls / DataSource) into a value's method-helper, so member access
	// (ThisObject.RegisterRecords / ThisForm.Controls) resolves through the same
	// path as module exports: GetPropVal(alias=eProcUnit) → m_procUnit->
	// GetPropVal(name) → the binder-filled frame slot. Reads the compile-module
	// bind map (present in BOTH Designer and runtime) — unlike ExportNamesToHelper
	// which reads bytecode (runtime only). Context binds (ThisObject / ThisForm
	// themselves) are skipped: they're the handles, not members of themselves.
	// Out-of-line — needs the complete ibCompileModule (only fwd-declared here).
	void FillHelperFromBinds(ibValue::ibMemberTable* helper, long alias) const;

	// Live value of an EXPORT binding by name (the m_listExternValue entry —
	// the same stable object/proxy BindExportVariable staged). Present in BOTH
	// Designer and runtime, so it resolves ThisForm.Controls / ThisObject.
	// RegisterRecords without a ProcUnit. Returns nullptr if not a bound name.
	ibValue* GetBoundValue(const wxString& name) const;

	// Lazy-create m_compileModule from the descriptor's meta-object (shared by
	// all Bind* entry points). Propagates the parent's compile scope chain.
	// Returns nullptr when GetMetaForCompile() has nothing to compile against.
	ibCompileModule* EnsureCompileModule();

	ibCompileModule* m_compileModule;
	// Descriptor owns its runtime slot. shared_ptr so a long-running
	// CallAsProc can pin the ProcUnit alive even if concurrent teardown
	// drops the descriptor (project_refresh_execute_crash.md).
	std::shared_ptr<ibProcUnit> m_procUnit;

	// Per-descriptor binding session — wraps bytecode's manifest +
	// slot table. Created lazily after compile (manifest is finalized
	// then). Subclasses populate via SetVar() at any point before
	// Execute — typical pattern: BindContextVariable() forwards to
	// m_binder->SetVar() so descriptor-instance values (ThisObject,
	// Reference) are wired without compile-time staging.
	std::unique_ptr<ibByteBinder> m_binder;

	// Parent descriptor — set by creation paths (AddCommonModule,
	// CreateNewForm, AddObject) so scope chain walks up correctly.
	// Raw ptr; safe as long as parent outlives child (enforced by
	// owning containers).
	const ibRuntimeModuleDataObject* m_parent = nullptr;
};

#endif
