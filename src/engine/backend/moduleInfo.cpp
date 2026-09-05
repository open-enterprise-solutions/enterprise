////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko 
//	Description : module information for ibValue 
////////////////////////////////////////////////////////////////////////////

#include "moduleInfo.h"

#include "appData.h"                 // DesignerMode() guard in Compile()
#include "backend/compiler/cache/byteCodeCache.h"              // AOT cache Load / Save
#include "backend/metaCollection/metaModuleObject.h"  // ibValueMetaObjectModuleBase full type for GetGuid/GetClassType
#include "backend/metaData.h"                         // ibMetaData::GetConfigMD5 — the cache key's second half

// The single ctor is inline in moduleInfo.h (it must reference ExportThunk +
// BindTail). Only the dtor lives out-of-line.
ibRuntimeModuleDataObject::~ibRuntimeModuleDataObject()
{
	// Drop this descriptor's bytecode from the process-wide registry
	// before destroying the compile module. Dependents resolving by
	// m_id mustn't see a dangling pointer.
	if (m_compileModule != nullptr) {
		const ibGuid& descId = m_compileModule->m_cByteCode.m_id;
		if (descId.isValid())
			ibByteCode::Unregister(descId);
	}
	wxDELETE(m_compileModule);
	// m_procUnit auto-destructs via shared_ptr when the last reference
	// drops.
}

std::shared_ptr<ibProcUnit> ibRuntimeModuleDataObject::GetProcUnit() const
{
	return m_procUnit;
}

const ibRuntimeRoot* ibRuntimeModuleDataObject::GetRoot() const
{
	// Default: walk up. Root descriptor overrides and returns `this`
	// cast to the interface.
	return m_parent ? m_parent->GetRoot() : nullptr;
}

void ibRuntimeModuleDataObject::InitializeRuntime()
{
	// ProcUnit is the runtime slot — allocated lazily per descriptor.
	// Designer never executes so no slot is needed; duplicate calls
	// (already-set slot) short-circuit to keep the same instance.
	if (m_procUnit != nullptr || appData->DesignerMode())
		return;
	m_procUnit = std::make_shared<ibProcUnit>();
	// Propagate parent's ProcUnit as scope chain if parent is already
	// wired — allows SetParent to be called BEFORE this, subsequent
	// InitializeRuntime picks up the parent automatically.
	if (m_parent != nullptr) {
		if (auto parentPu = m_parent->GetProcUnit())
			m_procUnit->SetParent(parentPu.get());
	}
}

ibCompileModule* ibRuntimeModuleDataObject::EnsureCompileModule()
{
	// Lazy-create m_compileModule on first Bind… — subclass provides its
	// meta-object via GetMetaForCompile() override.
	if (m_compileModule == nullptr) {
		if (const ibValueMetaObjectModuleBase* meta = GetMetaForCompile()) {
			m_compileModule = new ibCompileModule(meta);
			// Propagate parent's compile scope chain — SetParent can be
			// called before the first Bind…; pick up parent compile here.
			if (m_parent != nullptr) {
				if (ibCompileModule* parentCompile = m_parent->GetCompileModule())
					m_compileModule->SetParent(parentCompile);
			}
		}
	}
	return m_compileModule;
}

// See header. Append the EXPORT bindings as eProcUnit-aliased props so member
// access (ThisForm.Controls / ThisObject.RegisterRecords) resolves them via the
// descriptor's ProcUnit, exactly like ExportNamesToHelper does for module
// exports. Context binds are the self-handles — skipped (no ThisForm.ThisForm).
void ibRuntimeModuleDataObject::FillHelperFromBinds(ibValue::ibMemberTable* helper, long alias) const
{
	if (helper == nullptr) return;
	const ibCompileModule* cm = GetCompileModule();
	if (cm == nullptr) return;
	for (const auto& kv : cm->m_listExternValue)
		helper->AppendProp(kv.first, wxNOT_FOUND, alias);
}

ibValue* ibRuntimeModuleDataObject::GetBoundValue(const wxString& name) const
{
	const ibCompileModule* cm = GetCompileModule();
	if (cm == nullptr) return nullptr;
	auto it = cm->m_listExternValue.find(name);
	return (it != cm->m_listExternValue.end()) ? it->second : nullptr;
}

// Named context variable — name VISIBLE in the editor (ThisObject / ThisForm).
void ibRuntimeModuleDataObject::BindContextVariable(const wxString& name, ibValue* value)
{
	if (ibCompileModule* cm = EnsureCompileModule())
		cm->AddContextVariable(name, value, /*scopeContext=*/false);
	// If the runtime binder is already built (post-Compile), forward the
	// value into its slot table too — keeps compile-time staging and runtime
	// binder in sync without subclass plumbing.
	if (m_binder != nullptr)
		m_binder->SetVar(name, value);
}

// Transparent scope container — name NOT an identifier, members surface into
// scope (Manager / EnumManager / SystemManager).
void ibRuntimeModuleDataObject::BindScopeVariable(const wxString& name, ibValue* value)
{
	if (ibCompileModule* cm = EnsureCompileModule())
		cm->AddContextVariable(name, value, /*scopeContext=*/true);
	// Binder is name→value only; the scope flag is an editor-display concern
	// with no runtime slot, so the runtime binding is identical to context.
	if (m_binder != nullptr)
		m_binder->SetVar(name, value);
}

// Export variable — name VISIBLE, stored in the extern map (global constants,
// module-valued names).
void ibRuntimeModuleDataObject::BindExportVariable(const wxString& name, ibValue* value)
{
	if (ibCompileModule* cm = EnsureCompileModule())
		cm->AddVariable(name, value);
	if (m_binder != nullptr)
		m_binder->SetVar(name, value);
}

// Plain writable LOCAL — name resolves to an ordinary frame local (kind=Local),
// but the binder seeds its slot with `value` at init. No required/type pre-flight,
// no member access. E.g. a constant's Value backed by &m_constValue.
void ibRuntimeModuleDataObject::BindLocalVariable(const wxString& name, ibValue* value)
{
	if (ibCompileModule* cm = EnsureCompileModule())
		cm->AddLocalVariable(name, value);
	if (m_binder != nullptr)
		m_binder->SetVar(name, value);
}

// Undo any Bind… for `name`. Does NOT lazy-create the compile module — there's
// nothing to remove from a module that was never wired.
void ibRuntimeModuleDataObject::UnbindVariable(const wxString& name)
{
	if (m_compileModule != nullptr)
		m_compileModule->RemoveVariable(name);
	// Binder has no slot-erase; nulling the slot unbinds the live value while
	// leaving the bytecode-declared slot in place (re-bind via SetVar later).
	if (m_binder != nullptr)
		m_binder->SetVar(name, nullptr);
}

void ibRuntimeModuleDataObject::Run(bool delta)
{
	// Designer never executes script — the editor only cares about
	// AST / symbol table. Runtime / codeRunner / daemon all go through
	// here.
	if (appData->DesignerMode())
		return;
	Execute(delta);
}

bool ibRuntimeModuleDataObject::Compile()
{
	if (m_compileModule == nullptr)
		return false;
	// Designer never runs bytecode — keep compile state untouched so
	// the intellisense walker sees a consistent AST without emit
	// side-effects that only make sense at runtime.
	if (appData->DesignerMode())
		return true;

	ibByteCode& bc = m_compileModule->m_cByteCode;
	const ibValueMetaObjectModuleBase* meta = GetMetaForCompile();

	// Phase A — find a usable bytecode.
	//
	// (a) Try the AOT cache first. ibByteCodeCache::Load looks the
	//     row up by descriptor_id and runs DeserializeAOT into bc.
	//     Magic / format-version mismatch is reported as miss so an
	//     OES binary upgrade transparently triggers full recompile.
	//     After load, ResolveAndVerifyDependencies walks
	//     m_dependencyIds via the bc registry; missing dep or version
	//     drift downgrades the hit to a miss (case (c)) and the row
	//     is invalidated.
	// (b) Cache miss → fall through to compile-from-source. Single
	//     unified assemble path at the bottom — descriptor doesn't
	//     care which arm produced bc.
	// WHAT THIS BYTECODE WOULD BE COMPILED AGAINST, as one value. It is part of the cache KEY, so a
	// row saved under any earlier state of the configuration is not found at all — see byteCodeCache.h.
	const ibMetaData* const owner = meta != nullptr ? meta->GetMetaData() : nullptr;

	// ⚠ THE KEY IS THE CONFIGURATION'S DIGEST, and it stays that. Mixing the module's own text into
	// it was tried on 2026-09-04 and taken back out: it patches the symptom at the reader's end,
	// while the cause is a WRITER that changes metadata without going through the save the rest of
	// the product goes through (Max: you change a form, you change objects, and you go PAST the
	// standard save). In the Designer, editing a module and closing the document saves the metadata —
	// the digest moves, the cache row retires by itself. A door that skips that has to be fixed at
	// the door.
	const wxString configMd5 = owner != nullptr ? owner->GetConfigMD5() : wxString();

	bool ready = false;
	if (meta != nullptr && ibByteCodeCache::Load(bc, meta->GetGuid(), configMd5)) {
		if (bc.ResolveAndVerifyDependencies()) {
			// Restore live pointers AOT skipped on serialize. m_parent
			// points at the parent compile module's bytecode —
			// fresh-compile path sets this inside ibCompileModule::Compile
			// (compileModule.cpp:79); the cache-hit path bypasses
			// Compile, so we mirror the wire-up here. Without it
			// runtime's parent-conformity check at procUnit.cpp:1115
			// derefs nullptr.
			if (ibCompileModule* parentCompile = m_compileModule->GetParent())
				bc.m_parent = &parentCompile->m_cByteCode;

			// ⭐⭐ AND THE NAME SURFACE, which the cache-hit path used to leave unbuilt.
			//
			// A name is resolved by walking, and there are two walks: the live COMPILE CONTEXTS
			// first, the parent BYTECODE chain after. They do not count the same number of rungs,
			// and the rung count IS the address a receiver operand carries.
			//
			// So the same module compiled against a freshly-compiled parent and against a parent
			// LOADED FROM CACHE gets two different addresses for the same name — because a loaded
			// parent skips Compile(), and with it PrepareModuleData, so its context is empty and the
			// search falls through to the second walk. Measured 2026-09-04: the first run after an
			// apply posts a document; the next run, with the parent coming from the cache, fails in
			// the posting handler with "'ValueIsFilled' is a global function - it is not a member of
			// this value". Same text, same byte code (vars=68 funcs=101 both ways), different road.
			//
			// Building the surface here makes the two roads agree. It is the same call the compile
			// path makes, it is idempotent, and it costs one pass over the bound values.
			// ⚠ AND NO COMPARISON AGAINST THE LOADED TABLE HERE. Checking the surface against
			// bc.m_listVar on every hit was written and taken back out the same day: it turns the
			// reader into the place that notices staleness, which is one place too late. A row that
			// no longer matches the configuration must not be READABLE at all — it is thrown away
			// where it goes stale (OnSaveMetaObject drops it on saveConfigFlag; the key retires
			// every row of a previous configuration digest), and this branch may then trust what it
			// gets (Max, 2026-09-04: a stale cache must be THROWN AWAY, not worked around).
			m_compileModule->PrepareModuleData();
			ready = true;
		} else {
			// (c) — dep registry missing the target or version drift.
			// Clear the stale row and let the recompile branch
			// repopulate. Reset() drops bc's body so
			// m_compileModule->Compile() starts from a clean state
			// instead of merging into the loaded layout.
			ibByteCodeCache::Invalidate(meta->GetGuid());
			bc.Reset();
		}
	}

	if (!ready) {
		if (!m_compileModule->Compile())
			return false;
		// Stamp the freshly-compiled bytecode with the descriptor
		// identity. m_id is the descriptor's GUID — stable across
		// renames in Designer; cache rows in sys_bytecode_cache key
		// off it. m_descriptorClsid records the kind (CommonModule /
		// FormModule / etc.) for sanity checks at AOT-load time and
		// for diag tooling. Both are best-effort here: if
		// GetMetaForCompile is null the bytecode keeps zeroed
		// identity (treated as "any" by validators).
		if (meta != nullptr) {
			bc.m_id = meta->GetGuid();
			bc.m_descriptorClsid = meta->GetClassType();
		}
		// Per-compile version fingerprint. Random GUID for now —
		// dependents snapshot it in m_dependencyVersions so any
		// successful recompile makes their cached rows fail the dep
		// version check (Step 4) and forces re-resolve.
		bc.m_version = ibGuid::newGuid();
		// Persist the freshly-compiled bytecode. Best-effort — Save
		// returns false on serialization rejection (e.g. non-primitive
		// constants) or DB error; the runtime keeps the live bc and
		// the next session pays the recompile cost again.
		//
		// ⭐ NOT WHAT WAS COMPILED UNDER AN EVAL. A watch or a sandbox opens a compile inside its
		// OWN host frame, and that frame is a rung: every operand this compile stamps carries a
		// depth counted with it. Executed later on the ordinary road — a posting, a form — the
		// rung is not there and the address points one step past the ladder. Live, that byte code
		// is consistent with the run that made it; SAVED, it is handed to every later run as if
		// it were ordinary, and the failure arrives on the SECOND launch, which is why it read as
		// "worked yesterday" (Max, 2026-09-04). A module first touched from a sandbox therefore
		// compiles again next time — the cost of a recompile, against an address that is wrong
		// for everybody else.
		if (meta != nullptr && ibBackendException::IsEvalMode() == eval_none)
			ibByteCodeCache::Save(bc, configMd5);
	}

	// Publish bc in the process-wide registry. Dependents resolving
	// their m_dependencyIds via ibByteCode::Find should now see this
	// descriptor's live ibByteCode and the freshly-stamped m_version.
	// Idempotent — overwrites the slot (e.g. recompile after a
	// drift-induced invalidate publishes the new pointer + version).
	if (bc.m_id.isValid())
		ibByteCode::Register(&bc);

	// Phase B — assemble. Same code path for cache-hit and
	// fresh-compile arms; bc.m_listVar is final after either, so the
	// binder is built once. Compile-side staged context values
	// (AddContextVariable / AddVariable calls before Compile, plus
	// any future BindContextVariable forwarding) seed the binder.
	//
	// PrepareNames on each bound value: fresh-compile path runs this
	// inside ibCompileCode::PrepareModuleData (compileCode.cpp:145, 163)
	// to populate the value's prop / method name tables — runtime
	// resolution of "ShowCommonForm" et al. reads them. Cache-hit
	// path skips PrepareModuleData entirely. Calling it here on every
	// bind is idempotent (PrepareNames is const + mutable internals)
	// and covers both arms cheaply.
	m_binder = std::make_unique<ibByteBinder>(bc.m_listVar);
	for (auto& kv : m_compileModule->m_listExternValue) {
		if (kv.second) kv.second->InvalidateNames();
		m_binder->SetVar(kv.first, kv.second);
	}
	for (auto& kv : m_compileModule->m_listContextValue) {
		if (kv.second.m_value) kv.second.m_value->InvalidateNames();
		m_binder->SetVar(kv.first, kv.second.m_value);
	}
	// Bound locals (e.g. a constant's Value backed by &m_constValue): plain
	// writable frame slots — no PrepareNames (they're values, not surfaced
	// objects). To the binder a local is indistinguishable from an external:
	// both just seed a slot; IsBindable() unifies them in SetVar / pre-flight.
	for (auto& kv : m_compileModule->m_listLocalValue) {
		m_binder->SetVar(kv.first, kv.second);
	}
	return true;
}

void ibRuntimeModuleDataObject::Execute(ibByteBinder& br)
{
	if (m_procUnit != nullptr && m_compileModule != nullptr)
		m_procUnit->Execute(m_compileModule->m_cByteCode, br);
}

void ibRuntimeModuleDataObject::Execute(bool delta)
{
	// Use the descriptor's own binder — populated post-Compile from
	// compile-side staging and kept in sync by BindContextVariable.
	// Subclasses can also reach m_binder->SetVar() directly for
	// per-execute live values.
	if (m_procUnit != nullptr && m_compileModule != nullptr && m_binder != nullptr) {
		m_binder->SetDelta(delta);
		Execute(*m_binder);
	}
}

void ibRuntimeModuleDataObject::SetParent(const ibRuntimeModuleDataObject* parent)
{
	m_parent = parent;

	// Cascade to compile+runtime layers so callers don't have to wire
	// them separately. Propagate only when both sides have the
	// corresponding object — parent may be a bare descriptor without a
	// compiled module (e.g. root in Designer mode) or without a
	// ProcUnit (system session that never runs scripts).
	if (m_compileModule != nullptr && parent != nullptr) {
		if (ibCompileModule* parentCompile = parent->GetCompileModule())
			m_compileModule->SetParent(parentCompile);
	}
	if (m_procUnit != nullptr && parent != nullptr) {
		if (auto parentPu = parent->GetProcUnit())
			m_procUnit->SetParent(parentPu.get());
	}
}
