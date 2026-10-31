#ifndef __MODULE_INFO_H__
#define __MODULE_INFO_H__

#include "backend/compiler/compileModule.h"
#include "backend/compiler/procUnit.h"

#include <memory>

class BACKEND_API ibValueMetaObjectModuleBase;

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
	void ExecAsProc(const ibString& strMethodName, Types&&... args) const {
		ibValue* paParams[] = { &args..., nullptr };
		ExecAsProc(strMethodName, paParams, (const long)sizeof...(args));
	}

	template <typename... Types>
	void ExecAsFunc(const ibString& strMethodName, ibValue& pvarRetValue, Types&&... args) const {
		ibValue* paParams[] = { &args..., nullptr };
		ExecAsFunc(strMethodName, pvarRetValue, paParams, (const long)sizeof...(args));
	}

	// ⭐ AN EVENT OF THE OWNER — `BeforeWrite`, `Posting`, `OnCopy`… Its own module's procedure first,
	// then every event handler of this event of the owner's type, the owner handed over as `Source`
	// before the event's own arguments — the same arguments, so a `Cancel` an event handler sets is the
	// one the caller reads. EVERY place that raises an event calls this, and that is what makes an event
	// handler run wherever the module's procedure does: there is no second call beside it to forget. A
	// plain call into the module (ExecAsProc) raises nothing.
	template <typename... Types>
	void ExecAsEvent(const ibString& strEventName, Types&&... args) const {
		ibValue* paParams[] = { &args..., nullptr };
		ExecAsEvent(strEventName, paParams, (const long)sizeof...(args));
	}

	// ⭐ …AND AN EVENT OF A MANAGER — `FormGetProcessing`, `ChoiceDataGetProcessing`, a job's `JobProcessing`.
	// A manager is no module descriptor, so its module is found where the manager value finds it
	// (EditModuleManagerFor) and its procedure called here; then every event handler of it, the manager
	// handed over as `Source`. False — and nothing called — when the manager's module is not registered
	// where this runs; a caller that cannot go on without it (a job) says so.
	static bool ExecAsManagerEvent(const class ibValueMetaObjectGenericData* metaObject, const ibString& strEventName,
		ibValue** paParams, const long lSizeArray);

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

	// Empty the bytecode where it stands: a child's m_parent and a ProcUnit keep pointing at it.
	void ResetByteCode() { if (m_byteCode != nullptr) m_byteCode->Reset(); }

	// Bind a value on this descriptor. The value is an ibValue that exposes
	// its method table + attributes at compile time. A bind is an entry in the
	// descriptor's maps (m_binds), module or no module: a compiler is given a
	// copy, a run fills its binder from them, and the descriptor's own surface
	// reads the exported ones. Subclass code reads like a recipe: Bind… → Compile → Run.
	//
	// The flavours by editor-visibility / storage:
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
	//   BindGlobalModule    — global module (m_listGlobalModule, no name of its own):
	//     a compile here reads its lexems with its own text, and its names are this module's.
	void BindContextVariable(const ibString& name, class ibValue* value);
	void BindScopeVariable(const ibString& name, class ibValue* value);
	void BindExportVariable(const ibString& name, class ibValue* value);
	void BindLocalVariable(const ibString& name, class ibValue* value);
	void BindGlobalModule(const ibString& name, const ibCompilableSourceCode* module);

	// Symmetric teardown for the Bind… family. Erases the name from every
	// map, so one Unbind undoes any flavour of bind.
	void UnbindVariable(const ibString& name);

	// Bring up this descriptor's bytecode: from the cache, and only when it
	// has none there a compiler is made for it, and it goes when the
	// bytecode is made. Skips in Designer mode (the designer
	// never runs a module; it asks a compile chain, CreateCompiler).
	// Returns false when there is no module to compile. Backend compile errors
	// propagate as ibBackendException — callers with swallow semantics wrap in
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
	// for paths that re-Execute on already-compiled state. Nothing compiled,
	// nothing runs. The binder is made for the call, from the binds as they
	// are now. delta semantics as in Run() above.
	void Execute(bool delta = true);

	// The ONLY ctor — a descriptor is always built by its owning value, which passes
	// its already-constructed helper + itself (and, for the module manager and a module
	// unit, the module it compiles). Registering the export surface as the helper's TAIL
	// here makes it impossible to construct a descriptor without wiring its exports "in
	// the descriptor", and after the value's own fixed methods (stable CallAsFunc indices).
	// No default ctor: it would silently skip the export wiring.
	ibRuntimeModuleDataObject(ibValue::ibMemberTable& helper, const ibValue* owner,
		const ibValueMetaObjectModuleBase* metaForCompile = nullptr)
		: m_metaForCompile(metaForCompile), m_binds(std::make_shared<ibBindTable>()) { helper.BindTail(&ExportThunk, owner); }
	virtual ~ibRuntimeModuleDataObject();

	// The module this descriptor compiles, under the descriptor's own name for it.
	const ibValueMetaObjectModuleBase* GetMetaObject() const { return GetMetaForCompile(); }

	// Compile-target — the single module this descriptor compiles
	// scripts from. Null when there is none: a form generated from its source.
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
	// Default reads what the ctor was given — subclasses (Unit, Configuration,
	// External DP) that name their module there; the rest override.
	virtual const ibValueMetaObjectModuleBase* GetMetaForCompile() const { return m_metaForCompile; }

	// What a child of this descriptor compiles against and its ProcUnit runs under.
	// Null until Compile has brought it up, and always in the designer.
	virtual const ibByteCode* GetByteCode() const { return m_byteCode.get(); }

	// A compiler of this descriptor's module with what is bound here and a compiler for every
	// descriptor above it, held by the one below; nothing else keeps them. Null when there is no
	// module. It is what a question about the text asks where nothing is compiled: the designer's
	// check and its hints. The first Compile on it compiles the whole chain, the root first.
	virtual std::unique_ptr<ibCompileModule> CreateCompiler() const;

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
	// Side-effect: cascades to the runtime layer, m_procUnit (call stack
	// frame chain). A compile asks for the parent when it happens. This
	// lets call sites set parent once at the descriptor level.
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
	//
	// ⭐⭐ AND WHERE THERE IS NO BYTECODE, THE TEXT IS ASKED INSTEAD — same question, same
	// answer, second road. The designer never emits bytecode for a module, so everything
	// built on this said nothing there: `Module.` offered no exports in the code editor or
	// in script_complete, and a manager host (Catalogs.Goods., commonObject.cpp) missed its
	// manager module's exports the same way — while the designer's own "Procedures and
	// functions" window listed them perfectly, because IT read the text (ibParseCode).
	//
	// Out-of-line since: reading the text needs the parser and the module object, and this
	// header is included nearly everywhere.
	void ExportMethodsToHelper(ibValue::ibMemberTable* helper, long alias) const;

	// Symmetric export-var (prop) half of ExportNamesToHelper. Public to keep the
	// method/prop split parallel. A module-backed manager host surfaces only the
	// method half (export-vars resolve through the host's own ProcUnit alias, which a
	// manager doesn't have); descriptors that own their runtime surface both via
	// ExportNamesToHelper. Falls back to the text for the same reason as the method half.
	void ExportPropsToHelper(ibValue::ibMemberTable* helper, long alias) const;

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
	//
	// ⭐ THE DOOR EVERY CALL INTO A MODULE PASSES, and so where a privileged module's call opens the
	// session's trusted window. Out of line: the window is the session's, and this header is included
	// nearly everywhere.
	bool ExecAsProc(const ibString& strMethodName,
		ibValue** paParams, const long lSizeArray) const;
	bool ExecAsFunc(const ibString& strMethodName,
		ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) const;

	// The declared default of a parameter of the module's function — the answer a derived class's
	// GetParamDefValue gives for a method it calls through ExecAsProc / ExecAsFunc, when a call left
	// that argument out. False for a parameter without one, or a name that is not the module's.
	bool GetParamDefValue(const ibString& strMethodName, const long lParamNum, ibValue& pvarParamDefValue) const
	{
		if (auto pu = GetProcUnit()) {
			const ibByteCode* const bc = pu->GetByteCode();
			return bc != nullptr && bc->GetParamDefValue(bc->FindMethod(strMethodName), lParamNum, pvarParamDefValue);
		}
		return false;
	}

	// The event's array form — see the variadic above. Out of line: it asks the configuration for its
	// event handlers, and this header is included nearly everywhere.
	void ExecAsEvent(const ibString& strEventName, ibValue** paParams, const long lSizeArray) const;

	// Every event handler of `metaData` for this event of `source`, called with `source` and the event's
	// arguments — the second half of an owner's event (ExecAsEvent) and of a manager's (ExecAsManagerEvent).
	// A member so that a handler's module is called through ExecAsProc, the door every other call passes.
	static void ExecEventHandlers(const class ibMetaData* metaData, const ibValue* source,
		const ibString& strEventName, ibValue** paParams, const long lSizeArray);

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
	// GetMetaForCompile() on `this`; here the cross-cast to the descriptor sibling is
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
	// GetPropVal(name) → the binder-filled frame slot. Reads the bind map
	// (present in BOTH Designer and runtime) — unlike ExportNamesToHelper
	// which reads bytecode (runtime only). Context binds (ThisObject / ThisForm
	// themselves) are skipped: they're the handles, not members of themselves.
	void FillHelperFromBinds(ibValue::ibMemberTable* helper, long alias) const;

	// Live value of an EXPORT binding by name (the m_listExternValue entry —
	// the same stable object/proxy BindExportVariable staged). Present in BOTH
	// Designer and runtime, so it resolves ThisForm.Controls / ThisObject.
	// RegisterRecords without a ProcUnit. Returns nullptr if not a bound name.
	ibValue* GetBoundValue(const ibString& name) const;

	// The same binding, copied into `out`. A cell that already holds a reference
	// hands that reference over, so a mutable one stays mutable. A read-only
	// `out` is a failed read: true must not leave the previous value standing.
	bool GetBoundValue(const ibString& name, ibValue& out) const;

	// The module named at construction — see GetMetaForCompile.
	const ibValueMetaObjectModuleBase* m_metaForCompile;

	// What is bound here — see the Bind… family. shared_ptr: an external object
	// and its manager are one module, and share what it is bound to.
	std::shared_ptr<ibBindTable> m_binds;

	// What runs — made by Compile, null while there is no module or nothing has come up. The
	// object stays through a recompile: a child's m_parent and a ProcUnit keep pointing at it.
	// shared_ptr for the same two as m_binds.
	std::shared_ptr<ibByteCode> m_byteCode;

	// Descriptor owns its runtime slot. shared_ptr so a long-running
	// CallAsProc can pin the ProcUnit alive even if concurrent teardown
	// drops the descriptor (project_refresh_execute_crash.md).
	std::shared_ptr<ibProcUnit> m_procUnit;

	// Parent descriptor — set by creation paths (AddCommonModule,
	// CreateNewForm, AddObject) so scope chain walks up correctly.
	// Raw ptr; safe as long as parent outlives child (enforced by
	// owning containers).
	const ibRuntimeModuleDataObject* m_parent = nullptr;

public:

	// ⭐ THE RUNTIME VALUE THIS DESCRIPTOR IS A PART OF — what an event of it hands its handlers as `Source`.
	// Every owner builds the descriptor with itself, so by default it is read off `this`: a CROSS-cast, the
	// descriptor being the value's sibling base and not derived from it, which is why it cannot be a static
	// one. Null for a descriptor that is no value's part — its events then reach no handler. An owner that
	// knows it is one may answer `this` outright. Last in the class: an optional virtual shifts no slot.
	virtual const ibValue* GetRuntimeOwner() const { return dynamic_cast<const ibValue*>(this); }
};

#endif
