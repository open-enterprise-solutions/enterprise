#ifndef __MODULE_INFO_H__
#define __MODULE_INFO_H__

#include "backend/compiler/compileModule.h"
#include "backend/compiler/procUnit.h"
// What the template's calls into the module name as they stand (the trusted window, the module asked whether it
// is privileged): GCC and Clang read a name that does not depend on TValue where the template is written.
#include "backend/metaCollection/metaModuleObject.h"
#include "backend/session/session.h"

#include <concepts>
#include <memory>

// Marker interface for descriptors that terminate a runtime tree.
// Today: ibValueModuleManagerRuntimeApplication. Used by ibRuntimeModuleDataObject::GetRoot
// parent walk to find "the root above this nested descriptor".
class BACKEND_API ibRuntimeRoot {
public:
	virtual ~ibRuntimeRoot() = default;
};

template <class TValue> requires std::derived_from<TValue, ibValue>
class ibValueModuleDataObject;

class BACKEND_API ibRuntimeModuleDataObject {
public:

	// ⭐ AN EVENT OF A MANAGER — `FormGetProcessing`, `ChoiceDataGetProcessing`, a job's `JobProcessing`.
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
	//     global constants, shared modules surfaced as module-valued names.
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
	// shared modules do that (moduleManager.cpp), since a shared module's
	// top-level is just declarations.
	void Run(bool delta = true);

	// Low-level execute — unconditional (no Designer guard). Prefer
	// Run() in subclass code; Execute() remains for compatibility and
	// for paths that re-Execute on already-compiled state. Nothing compiled,
	// nothing runs. The binder is made for the call, from the binds as they
	// are now. delta semantics as in Run() above.
	void Execute(bool delta = true);

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
	//     objects; form / shared module are modules themselves and
	//     return the metadata directly).
	// Default reads what the ctor was given — subclasses (Unit, Application,
	// External data tool) that name their module there; the rest override.
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
	// m_listSharedModuleManager / form tabs / script locals). Eval is
	// the only case where parent is a live runtime-executing node
	// picked up via Current() at compile time.
	//
	// Side-effect: cascades to the runtime layer, m_procUnit (call stack
	// frame chain). A compile asks for the parent when it happens. This
	// lets call sites set parent once at the descriptor level.
	void SetParent(const ibRuntimeModuleDataObject* parent);
	const ibRuntimeModuleDataObject* GetParent() const { return m_parent; }

	// Walk the parent chain until we hit a node that acts as runtime
	// root (implements ibRuntimeRoot). Application is today's sole
	// root; external data tools / reports parent into it. Returns nullptr
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
	void ExportMethodsToHelper(ibValue::ibMemberTable* helper) const;

	// The variables' half of the module's names. Public to keep the method/prop split
	// parallel. A module-backed manager host surfaces only the method half (its
	// variables resolve through the host's own ProcUnit, which a manager doesn't
	// have); a value carrying its module surfaces both (ibValueModuleDataObject::
	// FillMembers). Falls back to the text for the same reason as the method half.
	void ExportPropsToHelper(ibValue::ibMemberTable* helper) const;

protected:
	// Every event handler of `module`'s application for this event of `source`, called with `source` and the
	// event's arguments — the second half of an owner's event (ExecAsEvent) and of a manager's
	// (ExecAsManagerEvent). A member so that a handler's module is called through ExecAsProc, the door every
	// other call passes.
	static void ExecEventHandlers(const ibValueMetaObjectModuleBase* module, const ibValue* source,
		const ibString& strEventName, ibValue** paParams, const long lSizeArray);

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

	// Parent descriptor — set by creation paths (AddSharedModule,
	// CreateNewForm, AddObject) so scope chain walks up correctly.
	// Raw ptr; safe as long as parent outlives child (enforced by
	// owning containers).
	const ibRuntimeModuleDataObject* m_parent = nullptr;

private:

	// ⭐ BUILT BY THE TEMPLATE ALONE (ibValueModuleDataObject below) — the compiler and the runtime of a module live
	// here; where they meet a runtime value — its table, its scopes, the value itself — is the template's, the one
	// class that is both. No other class can derive from this one: there is no other ctor to call.
	explicit ibRuntimeModuleDataObject(const ibValueMetaObjectModuleBase* metaForCompile)
		: m_metaForCompile(metaForCompile), m_binds(std::make_shared<ibBindTable>()) {}

	template <class TValue> requires std::derived_from<TValue, ibValue>
	friend class ibValueModuleDataObject;
};

// ⭐ A VALUE THAT CARRIES ITS MODULE — a runtime value (TValue, an ibValue: anything else is refused at compile
// time) and the module's descriptor, joined in one place, and everything where the two meet is here: the module's
// names on the value's table, the value's standard methods for them, whether a row is scoped, the value an event
// hands its handlers. The module the descriptor compiles comes first (nullptr where the owner answers
// GetMetaForCompile itself), the value base's own arguments after it.
//
// ⭐ THE ORDER IS THE BASE'S. It binds first, so the module's rows come first; a class below answers the rows its
// own binder appended (GetMethodData with its filler — the row's level, then its place on it) and hands every other
// row up here.
template <class TValue> requires std::derived_from<TValue, ibValue>
class ibValueModuleDataObject : public TValue, public ibRuntimeModuleDataObject {
public:

	template <class... TArgs>
	ibValueModuleDataObject(const ibValueMetaObjectModuleBase* metaForCompile, TArgs&&... args)
		: TValue(std::forward<TArgs>(args)...), ibRuntimeModuleDataObject(metaForCompile) { this->m_members.Bind(this, &ibValueModuleDataObject::FillMembers); }

	// A Public variable of the module or an export binding (RegisterRecords / Filter / Controls / DataSource).
	// The write goes to a variable the module HAS: the unit's write by name made a slot for any other name and
	// answered true.
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal) override {
		const std::shared_ptr<ibProcUnit> pu = GetProcUnit();
		if (pu == nullptr)
			return false;
		const long lVarNum = pu->FindProp(this->GetPropName(lPropNum));
		return lVarNum != ibNOT_FOUND && pu->SetPropVal(lVarNum, varPropVal);
	}

	// The read asks the running module, then the binding itself, which the designer has as well (no ProcUnit there).
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override {
		const ibString& strPropName = this->GetPropName(lPropNum);
		const std::shared_ptr<ibProcUnit> pu = GetProcUnit();
		if (pu != nullptr && pu->GetPropVal(strPropName, pvarPropVal))
			return true;
		// A slot that cannot be written must not answer true and leave the value stale.
		if (pvarPropVal.m_bReadOnly)
			return false;
		const auto bound = m_binds->m_listExternValue.find(strPropName);
		if (bound == m_binds->m_listExternValue.end() || bound->second == nullptr)
			return false;
		// Copy what the cell holds. When the cell is a reference, that copy is the object it points at: the
		// document stays writable, a const reference stays const, and the cell itself (an embedded member,
		// refcount 0) is not counted.
		pvarPropVal = *bound->second;
		return true;
	}

	// SCOPED — the module's own and not public: a variable declared without Export, and the context it compiles
	// against (ThisObject / ThisForm) — one behaviour. The module sees it, the dot does not.
	virtual bool IsPropScoped(const long lPropNum) const override {
		const ibString& strPropName = this->GetPropName(lPropNum);
		if (m_binds->m_listContextValue.find(strPropName) != m_binds->m_listContextValue.end())
			return true;
		const std::shared_ptr<ibProcUnit> pu = GetProcUnit();
		const ibByteCode* const bc = pu != nullptr ? pu->GetByteCode() : nullptr;
		if (bc == nullptr)
			return false;
		for (const auto& v : bc->m_listVar) {
			if ((v.IsLocal() || v.IsProtected()) && strPropName.IsSameAs(v.m_strRealName, false))
				return true;
		}
		return false;
	}

	// SCOPED — a procedure or function declared without Export: the unit finds it, and finds no public one of
	// that name. Asked only of a compiled unit: its FindMethod raises on any other.
	virtual bool IsMethodScoped(const long lMethodNum) const override {
		const std::shared_ptr<ibProcUnit> pu = GetProcUnit();
		const ibByteCode* const bc = pu != nullptr ? pu->GetByteCode() : nullptr;
		if (bc == nullptr || !bc->m_bCompile)
			return false;
		const ibString& strMethodName = this->GetMethodName(lMethodNum);
		return pu->FindMethod(strMethodName, false, 0) != ibNOT_FOUND
			&& pu->FindMethod(strMethodName, false, 1) == ibNOT_FOUND;
	}

	// The declared default of a parameter of the module's function, when a call left that argument out.
	virtual bool GetParamDefValue(const long lMethodNum, const long lParamNum, ibValue& pvarParamDefValue) const override {
		const std::shared_ptr<ibProcUnit> pu = GetProcUnit();
		const ibByteCode* const bc = pu != nullptr ? pu->GetByteCode() : nullptr;
		return bc != nullptr && bc->GetParamDefValue(bc->FindMethod(this->GetMethodName(lMethodNum)), lParamNum, pvarParamDefValue);
	}

	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) override {
		return ExecAsProc(this->GetMethodName(lMethodNum), paParams, lSizeArray);
	}

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override {
		return ExecAsFunc(this->GetMethodName(lMethodNum), pvarRetValue, paParams, lSizeArray);
	}

	// A call into the running module BY ITS NAME — the one the value's CallAsProc / CallAsFunc and an event come to,
	// the name looked up by the module itself. The local shared_ptr keeps the ProcUnit alive for the whole call:
	// without it, a concurrent session teardown would drop the last ref and leave pu dangling mid-Execute (fast-F5
	// crash, project_refresh_execute_crash.md). ⭐ EVERY CALL INTO A MODULE PASSES HERE, and so here a privileged
	// module's call opens the session's trusted window — for the whole call and everything it runs.
	bool ExecAsProc(const ibString& strMethodName, ibValue** paParams, const long lSizeArray) const {
		const std::shared_ptr<ibProcUnit> pu = GetProcUnit();
		if (pu == nullptr)
			return false;
		const ibValueMetaObjectModuleBase* const module = GetMetaForCompile();
		const ibAccessTrustScope trusted(module != nullptr && module->IsPrivilegedModule() ? ibSession::Current() : nullptr);
		return pu->CallAsProc(strMethodName, paParams, lSizeArray);
	}

	bool ExecAsFunc(const ibString& strMethodName, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) const {
		const std::shared_ptr<ibProcUnit> pu = GetProcUnit();
		if (pu == nullptr)
			return false;
		const ibValueMetaObjectModuleBase* const module = GetMetaForCompile();
		const ibAccessTrustScope trusted(module != nullptr && module->IsPrivilegedModule() ? ibSession::Current() : nullptr);
		return pu->CallAsFunc(strMethodName, pvarRetValue, paParams, lSizeArray);
	}

	template <typename... Types>
	bool ExecAsProc(const ibString& strMethodName, Types&&... args) const {
		ibValue* paParams[] = { &args..., nullptr };
		return ExecAsProc(strMethodName, paParams, (const long)sizeof...(args));
	}

	template <typename... Types>
	bool ExecAsFunc(const ibString& strMethodName, ibValue& pvarRetValue, Types&&... args) const {
		ibValue* paParams[] = { &args..., nullptr };
		return ExecAsFunc(strMethodName, pvarRetValue, paParams, (const long)sizeof...(args));
	}

	// ⭐ AN EVENT OF THE VALUE — `BeforeWrite`, `Posting`, `OnCopy`… Its own module's procedure first, then every
	// event handler of this event of the value's type, the value handed over as `Source` before the event's own
	// arguments — the same arguments, so a `Cancel` an event handler sets is the one the caller reads. EVERY place
	// that raises an event calls this, and that is what makes an event handler run wherever the module's procedure
	// does: there is no second call beside it to forget. A plain call into the module (ExecAsProc) raises nothing.
	template <typename... Types>
	void ExecAsEvent(const ibString& strEventName, Types&&... args) const {
		ibValue* paParams[] = { &args..., nullptr };
		ExecAsEvent(strEventName, paParams, (const long)sizeof...(args));
	}

private:

	void ExecAsEvent(const ibString& strEventName, ibValue** paParams, const long lSizeArray) const {
		ExecAsProc(strEventName, paParams, lSizeArray);
		ExecEventHandlers(GetMetaForCompile(), this, strEventName, paParams, lSizeArray);
	}

	// The module's names — its procedures and functions, its variables, its export bindings (RegisterRecords /
	// Filter / Controls / DataSource, read as a Public variable is). Not the context bindings (ThisObject /
	// ThisForm): they're the handles, not members of themselves.
	void FillMembers(ibValue::ibMemberTable& helper) const {
		ExportMethodsToHelper(&helper);
		ExportPropsToHelper(&helper);
		for (const auto& kv : m_binds->m_listExternValue)
			helper.AppendProp(kv.first);
	}
};

#endif
