////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko 
//	Description : module information for ibValue 
////////////////////////////////////////////////////////////////////////////

#include "moduleInfo.h"

#include "appData.h"                 // DesignerMode() guard in Compile()
#include "backend/compiler/cache/byteCodeCache.h"              // AOT cache Load / Save
#include "backend/metaCollection/metaModuleObject.h"  // ibValueMetaObjectModuleBase full type for GetGuid/GetClassType
#include "backend/utils/md5.hpp"

namespace {

ibGuid MakeByteCodeVersion(const ibValueMetaObjectModuleBase* meta)
{
	if (meta == nullptr)
		return ibGuid();

	const wxString seed =
		wxString(meta->GetGuid()) + wxT("\n") +
		meta->GetFullName() + wxT("\n") +
		wxString::Format(wxT("%llu"), (unsigned long long)meta->GetClassType()) + wxT("\n") +
		meta->GetModuleText() + wxT("\n") +
		wxT("bytecode-aot-format-v10");
	const wxString md5 = ibMD5::ComputeMd5(seed);
	if (md5.length() != 32)
		return ibGuid();
	const wxString guid =
		md5.Mid(0, 8) + wxT("-") +
		md5.Mid(8, 4) + wxT("-") +
		md5.Mid(12, 4) + wxT("-") +
		md5.Mid(16, 4) + wxT("-") +
		md5.Mid(20, 12);
	return ibGuid(guid);
}

} // namespace

ibRuntimeModuleDataObject::ibRuntimeModuleDataObject() :
	m_compileModule(nullptr)
{
}

ibRuntimeModuleDataObject::ibRuntimeModuleDataObject(ibCompileModule* compileCode) :
	m_compileModule(compileCode)
{
}

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

void ibRuntimeModuleDataObject::BindContextVariable(const wxString& name, ibValue* value)
{
	// Lazy-create m_compileModule on first BindContextVariable —
	// subclass provides its meta-object via GetMetaObject() override.
	if (m_compileModule == nullptr) {
		if (const ibValueMetaObjectModuleBase* meta = GetMetaForCompile()) {
			m_compileModule = new ibCompileModule(meta);
			// Propagate parent's compile scope chain — SetParent can
			// be called before BindContextVariable; we pick up the
			// parent compile on creation.
			if (m_parent != nullptr) {
				if (ibCompileModule* parentCompile = m_parent->GetCompileModule())
					m_compileModule->SetParent(parentCompile);
			}
		}
	}
	if (m_compileModule != nullptr)
		m_compileModule->AddContextVariable(name, value);
	// If the runtime binder is already built (post-Compile), forward
	// the value into its slot table too — keeps compile-time staging
	// and runtime binder in sync without subclass plumbing.
	if (m_binder != nullptr)
		m_binder->SetVar(name, value);
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
	const ibGuid expectedVersion = MakeByteCodeVersion(meta);

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
	bool ready = false;
	if (meta != nullptr && ibByteCodeCache::Load(bc, meta->GetGuid(), &expectedVersion)) {
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
		// Deterministic compile fingerprint. Dependents snapshot it
		// in m_dependencyVersions, so source / descriptor-kind /
		// compiler-seed drift makes their cached rows miss and re-resolve.
		bc.m_version = expectedVersion.isValid()
			? expectedVersion
			: ibGuid::newGuid();
		// Persist the freshly-compiled bytecode. Best-effort — Save
		// returns false on serialization rejection (e.g. non-primitive
		// constants) or DB error; the runtime keeps the live bc and
		// the next session pays the recompile cost again.
		if (meta != nullptr)
			ibByteCodeCache::Save(bc);
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
		if (kv.second) kv.second->PrepareNames();
		m_binder->SetVar(kv.first, kv.second);
	}
	for (auto& kv : m_compileModule->m_listContextValue) {
		if (kv.second) kv.second->PrepareNames();
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
