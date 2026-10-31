////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko 
//	Description : module information for ibValue 
////////////////////////////////////////////////////////////////////////////

#include "moduleInfo.h"

#include "appData.h"                 // DesignerMode() guard in Compile()
#include "backend/compiler/cache/byteCodeCache.h"              // AOT cache Load / Save
#include "backend/metaCollection/metaModuleObject.h"  // ibValueMetaObjectModuleBase full type for GetGuid/GetClassType
#include "backend/metaData.h"                         // ibMetaData::GetConfigMD5 — the cache key's second half
#include "core/compiler/scriptParseCode.h"          // ibParseCode — the export names a TEXT declares
// DEF_VAR_SKIP — the sentinel a parameter's default-value slot carries when there is NO default.
// ⚠ It lives on the COMPILE side while the field it marks (ibByteParam::m_defaultValue) lives in
// byteCode.h, which describes it in a comment and cannot see it. Included rather than duplicated;
// the pair belongs together and moving it is wider than this change.
#include "backend/compiler/compileContext.h"

// The single ctor is inline in moduleInfo.h (it must reference ExportThunk +
// BindTail). Only the dtor lives out-of-line.
ibRuntimeModuleDataObject::~ibRuntimeModuleDataObject()
{
	// Drop this descriptor's bytecode from the process-wide registry.
	// Dependents resolving by m_id mustn't see a dangling pointer.
	if (m_byteCode != nullptr && m_byteCode->m_id.isValid())
		ibByteCode::Unregister(m_byteCode->m_id);
	// m_procUnit auto-destructs via shared_ptr when the last reference
	// drops.
}

std::shared_ptr<ibProcUnit> ibRuntimeModuleDataObject::GetProcUnit() const
{
	return m_procUnit;
}

#include "backend/metaCollection/metaEventHandlerObject.h"        // who handles an event
#include "backend/metaCollection/partial/commonObject.h"          // ibValueManagerDataObject — a manager event's Source
#include "backend/session/session.h"                              // ibSession::EditModuleManagerFor — where they run
#include "backend/moduleManager/moduleManager.h"                  // FindCommonModule — an event handler's module

// A module value's method, a job, an owner's event, a manager's, an event handler: every call into a module
// comes through here, so a privileged module's window is opened in one place and no road can miss it. For
// the whole call, and for everything the call runs: nested windows compose, and Await pauses this one while
// a question is open (ibSessionTrustPause). The flag is read off the module at the call, not copied anywhere.
bool ibRuntimeModuleDataObject::ExecAsProc(const ibString& strMethodName,
	ibValue** paParams, const long lSizeArray) const
{
	const std::shared_ptr<ibProcUnit> pu = GetProcUnit();
	if (!pu)
		return false;
	const ibValueMetaObjectModuleBase* const module = GetMetaForCompile();
	const ibAccessTrustScope trusted(module != nullptr && module->IsPrivilegedModule() ? ibSession::Current() : nullptr);
	return pu->CallAsProc(strMethodName, paParams, lSizeArray);
}

bool ibRuntimeModuleDataObject::ExecAsFunc(const ibString& strMethodName,
	ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) const
{
	const std::shared_ptr<ibProcUnit> pu = GetProcUnit();
	if (!pu)
		return false;
	const ibValueMetaObjectModuleBase* const module = GetMetaForCompile();
	const ibAccessTrustScope trusted(module != nullptr && module->IsPrivilegedModule() ? ibSession::Current() : nullptr);
	return pu->CallAsFunc(strMethodName, pvarRetValue, paParams, lSizeArray);
}

void ibRuntimeModuleDataObject::ExecEventHandlers(const ibMetaData* metaData, const ibValue* source,
	const ibString& strEventName, ibValue** paParams, const long lSizeArray)
{
	if (metaData == nullptr || source == nullptr)
		return;
	const std::vector<ibValueMetaObjectEventHandler*> handlers =
		metaData->GetAnyArrayObject<ibValueMetaObjectEventHandler>(g_metaEventHandlerCLSID);
	if (handlers.empty())
		return;

	// WHO TAKES THIS EVENT, asked first: by its number (its name's hash, a UTF-8 pass over it — made once, not
	// once per handler) and by the value's type. Nobody — the event is over here, and nothing below is paid for.
	const long eventId = ibValueMetaObjectEventHandler::EventId(strEventName);
	const ibClassID sourceType = source->GetClassType();
	std::vector<const ibValueMetaObjectEventHandler*> taking;
	for (const ibValueMetaObjectEventHandler* handler : handlers)
		if (handler != nullptr && !handler->IsDeleted() && handler->Handles(sourceType, eventId))
			taking.push_back(handler);
	if (taking.empty())
		return;

	// An event handler's module is a MANAGER module, found where the manager value and a parameterized
	// job find theirs (ibSession::EditModuleManagerFor): the modules registered for this configuration.
	// No such manager, nothing runs and nobody is called.
	const ibValueModuleManager* const moduleManager = ibSession::EditModuleManagerFor(metaData);
	if (moduleManager == nullptr)
		return;

	// Source first — the value itself, referred to the way an aggregate hands itself out, and only now that
	// somebody takes the event: a reference taken and dropped on every event would put an object nobody holds
	// through zero. Then the event's arguments — the SAME ones, by pointer, so what a procedure writes into
	// them (Cancel, StandardProcessing) is what the raiser reads.
	ibValue sourceArgument = source->GetValue(true);
	std::vector<ibValue*> params = { &sourceArgument };
	params.insert(params.end(), paParams, paParams + lSizeArray);

	for (const ibValueMetaObjectEventHandler* handler : taking) {
		if (const ibRuntimeModuleDataObject* const unit = moduleManager->FindCommonModule(handler->GetHandlerModule()))
			unit->ExecAsProc(strEventName, params.data(), static_cast<long>(params.size()));
	}
}

void ibRuntimeModuleDataObject::ExecAsEvent(const ibString& strEventName, ibValue** paParams, const long lSizeArray) const
{
	// The owner's own procedure first — what the event always did.
	ExecAsProc(strEventName, paParams, lSizeArray);

	// …then its event handlers, the runtime value this descriptor is a part of handed over as Source. The
	// configuration is the owner's own, asked of the module it runs.
	if (const ibValueMetaObjectModuleBase* const module = GetMetaForCompile())
		ExecEventHandlers(module->GetMetaData(), GetRuntimeOwner(), strEventName, paParams, lSizeArray);
}

bool ibRuntimeModuleDataObject::ExecAsManagerEvent(const ibValueMetaObjectGenericData* metaObject, const ibString& strEventName,
	ibValue** paParams, const long lSizeArray)
{
	if (metaObject == nullptr)
		return false;
	const ibMetaData* const metaData = metaObject->GetMetaData();

	// The manager module's own procedure first — found where the manager value finds it (EditModuleManagerFor),
	// NOT in the session's runtime root. The root holds the configuration's common modules; a manager module is
	// registered where its metaobject's metadata lives, which in the Designer is a compile-cache manager with no
	// root at all. Asked of the root, a job "ran" and did nothing.
	const ibValueMetaObjectCommonModule* const module = metaObject->GetManagerModule();
	const ibValueModuleManager* const moduleManager = module != nullptr ? ibSession::EditModuleManagerFor(metaData) : nullptr;
	const ibRuntimeModuleDataObject* const unit = moduleManager != nullptr ? moduleManager->FindCommonModule(module) : nullptr;
	if (unit == nullptr)
		return false;
	unit->ExecAsProc(strEventName, paParams, lSizeArray);

	// …then its event handlers, the manager itself handed over as Source.
	const ibValuePtr<ibValueManagerDataObject> manager = metaObject->CreateManagerDataObjectValue();
	ExecEventHandlers(metaData, manager, strEventName, paParams, lSizeArray);
	return true;
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

std::unique_ptr<ibCompileModule> ibRuntimeModuleDataObject::CreateCompiler() const
{
	const ibValueMetaObjectModuleBase* const meta = GetMetaForCompile();
	if (meta == nullptr)
		return nullptr;
	return std::make_unique<ibCompileModule>(meta, *m_binds, m_parent != nullptr ? m_parent->CreateCompiler() : nullptr);
}

// See header. Append the EXPORT bindings as eProcUnit-aliased props so member
// access (ThisForm.Controls / ThisObject.RegisterRecords) resolves them via the
// descriptor's ProcUnit, exactly like ExportNamesToHelper does for module
// exports. Context binds are the self-handles — skipped (no ThisForm.ThisForm).
// THE NAMES A MODULE EXPORTS, AND THE TWO PLACES THEY COME FROM.
//
// ⭐⭐ THE DESIGNER HAS NO RUNTIME, so it cannot be asked for bytecode — and bytecode is the only
// thing this used to read. Everything built on it therefore said NOTHING in the designer: a common
// module offered no exports after its dot, a manager host (Catalogs.Goods., commonObject.cpp)
// missed its manager module's exports, an object module's own exported procedures were invisible
// on ThisObject — while the designer's "Procedures and functions" window listed all of them,
// because IT read the TEXT (ibParseCode). One question, two answers, and one of them empty.
//
// ⚠ AND THE TEST IS "DESIGNER", NOT "NO BYTECODE" — which is the tempting one and is wrong. At
// RUNTIME a descriptor can also be without bytecode: a module that failed to compile is exactly
// that. Naming its exports there would advertise something nobody can call, and the failure moves
// from an honest "method not found" to a call into an absent ProcUnit. The designer executes
// nothing, so there a name read off the text is a promise it can keep. Asked the same way by the
// neighbour in this class: Compile() steps aside on appData->DesignerMode().
static void ibExportNamesFromText(const ibValueMetaObjectModuleBase* moduleObject,
	ibValue::ibMemberTable* helper, long alias, bool methods)
{
	if (moduleObject == nullptr)
		return;

	ibParseCode parser(ibCompileCode::GetCodeStyle());

	// A refusal is ordinary and says nothing: the text is somebody's work in progress, and half a
	// declaration is what a module looks like while it is being typed. No names, no noise.
	if (!parser.ParseModule(moduleObject->GetModuleText()))
		return;

	for (const ibModuleElement& element : parser.GetAllContent()) {

		// EXPORTED ONLY — the same line the bytecode road draws (IsExport). A private procedure is
		// reachable from inside its module and nowhere else, and offering it here would name
		// something the compiler then refuses.
		if (methods) {
			// ⭐ THE CALL FORM, NOT ONLY THE ARITY. This passed an empty helper string, so every
			// method a configuration declares arrived at a caller as a bare name while the
			// platform's own came with `GetTemplate(name : string)` beside them — the arity was
			// known and the names were not shown (2026-09-09, reading a document manager's
			// members: `Print` with nothing to say how to call it).
			//
			// Written here rather than at each reader: the parser is the one that saw the text.
			const ibString signature = ibModuleCallForm(element);

			if (element.m_eType == eExportFunction)
				helper->AppendFunc(element.m_name, element.ParamCount(), signature, wxNOT_FOUND, alias);
			else if (element.m_eType == eExportProcedure)
				helper->AppendProc(element.m_name, element.ParamCount(), signature, wxNOT_FOUND, alias);
		}
		else if (element.m_eType == eExportVariable) {
			helper->AppendProp(element.m_name, wxNOT_FOUND, alias);
		}
	}
}

void ibRuntimeModuleDataObject::ExportMethodsToHelper(ibValue::ibMemberTable* helper, long alias) const
{
	if (helper == nullptr)
		return;

	if (appData->DesignerMode()) {
		ibExportNamesFromText(GetMetaObject(), helper, alias, /*methods*/ true);
		return;
	}

	const auto pu = GetProcUnit();
	if (!pu) return;
	const ibByteCode* bc = pu->GetByteCode();
	if (bc == nullptr) return;
	for (const auto& fn : bc->m_listFunc) {
		if (!fn.IsExport()) continue;

		// ⭐ THE SAME CALL FORM THE TEXT ROAD WRITES, spelled from the bytecode's own parameter
		// records — the names travel there too (ibByteParam::m_strName), and this passed an empty
		// helper string beside them. One convention, both roads: a caller must not be able to tell
		// which side of the designer/runtime line answered it.
		ibString signature = fn.m_strRealName + ibT("(");
		for (size_t index = 0; index < fn.m_listParam.size(); ++index) {

			const auto& param = fn.m_listParam[index];
			const bool optional = param.m_defaultValue.m_numArray != DEF_VAR_SKIP;

			if (index > 0)
				signature += ibT(", ");

			if (optional)
				signature += ibT("[");
			if (param.m_bByValue)
				signature += ibT("Val ");

			signature += param.m_strName;

			if (optional)
				signature += ibT("]");
		}
		signature += ibT(")");

		helper->AppendMethod(fn.m_strRealName,
			signature,
			bc->GetNParams(fn),
			bc->HasRetVal(fn),
			(long)fn,
			alias);
	}
}

void ibRuntimeModuleDataObject::ExportPropsToHelper(ibValue::ibMemberTable* helper, long alias) const
{
	if (helper == nullptr)
		return;

	if (appData->DesignerMode()) {
		ibExportNamesFromText(GetMetaObject(), helper, alias, /*methods*/ false);
		return;
	}

	const auto pu = GetProcUnit();
	if (!pu) return;
	const ibByteCode* bc = pu->GetByteCode();
	if (bc == nullptr) return;
	for (const auto& v : bc->m_listVar) {
		if (!v.IsExport()) continue;
		helper->AppendProp(v.m_strRealName, v, alias);
	}
}

void ibRuntimeModuleDataObject::FillHelperFromBinds(ibValue::ibMemberTable* helper, long alias) const
{
	if (helper == nullptr) return;
	for (const auto& kv : m_binds->m_listExternValue)
		helper->AppendProp(kv.first, wxNOT_FOUND, alias);
}

ibValue* ibRuntimeModuleDataObject::GetBoundValue(const ibString& name) const
{
	auto it = m_binds->m_listExternValue.find(name);
	return (it != m_binds->m_listExternValue.end()) ? it->second : nullptr;
}

bool ibRuntimeModuleDataObject::GetBoundValue(const ibString& name, ibValue& out) const
{
	// A slot that cannot be written must not answer true and leave `out` stale.
	if (out.m_bReadOnly)
		return false;
	ibValue* bound = GetBoundValue(name);
	if (bound == nullptr)
		return false;
	// Copy what the cell holds. When the cell is a reference, that copy is the
	// object it points at: the document stays writable, a const reference stays
	// const, and the cell itself (an embedded member, refcount 0) is not counted.
	out = *bound;
	return true;
}

// Named context variable — name VISIBLE in the editor (ThisObject / ThisForm).
void ibRuntimeModuleDataObject::BindContextVariable(const ibString& name, ibValue* value)
{
	if (!name.IsEmpty())
		m_binds->m_listContextValue[name] = { value, /*scopeContext=*/false };
}

// Transparent scope container — name NOT an identifier, members surface into
// scope (Manager / EnumManager / SystemManager). The scope flag is an editor-display
// concern with no runtime slot, so at run time this is a context bind like the above.
void ibRuntimeModuleDataObject::BindScopeVariable(const ibString& name, ibValue* value)
{
	if (!name.IsEmpty())
		m_binds->m_listContextValue[name] = { value, /*scopeContext=*/true };
}

// Export variable — name VISIBLE, stored in the extern map (global constants,
// module-valued names).
void ibRuntimeModuleDataObject::BindExportVariable(const ibString& name, ibValue* value)
{
	if (!name.IsEmpty())
		m_binds->m_listExternValue[name] = value;
}

// Plain writable LOCAL — name resolves to an ordinary frame local (kind=Local),
// but the binder seeds its slot with `value` at init. No required/type pre-flight,
// no member access. E.g. a constant's Value backed by &m_constValue.
void ibRuntimeModuleDataObject::BindLocalVariable(const ibString& name, ibValue* value)
{
	if (!name.IsEmpty())
		m_binds->m_listLocalValue[name] = value;
}

// Global module — no value, only its text: a compile reads its lexems with this module's own.
void ibRuntimeModuleDataObject::BindGlobalModule(const ibString& name, const ibCompilableSourceCode* module)
{
	if (!name.IsEmpty())
		m_binds->m_listGlobalModule[name] = module;
}

// Undo any Bind… for `name`.
void ibRuntimeModuleDataObject::UnbindVariable(const ibString& name)
{
	m_binds->m_listExternValue.erase(name);
	m_binds->m_listContextValue.erase(name);
	m_binds->m_listLocalValue.erase(name);
	m_binds->m_listGlobalModule.erase(name);
}

void ibRuntimeModuleDataObject::Run(bool delta)
{
	// Designer never executes script — the editor only cares about
	// AST / symbol table. Runtime / codeRunner / appserver all go through
	// here.
	if (appData->DesignerMode())
		return;
	Execute(delta);
}

bool ibRuntimeModuleDataObject::Compile()
{
	const ibValueMetaObjectModuleBase* const meta = GetMetaForCompile();
	if (meta == nullptr)
		return false;
	// Designer never runs bytecode. What it asks about a text it asks
	// a compile chain (CreateCompiler).
	if (appData->DesignerMode())
		return true;

	if (m_byteCode == nullptr)
		m_byteCode = std::make_shared<ibByteCode>();
	ibByteCode& bc = *m_byteCode;

	// What this bytecode stands under, compiled or loaded alike.
	const ibByteCode* const parent = m_parent != nullptr ? m_parent->GetByteCode() : nullptr;

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
	const ibMetaData* const owner = meta->GetMetaData();

	// ⚠ THE KEY IS THE CONFIGURATION'S DIGEST, and it stays that. Mixing the module's own text into
	// it was tried on 2026-09-04 and taken back out: it patches the symptom at the reader's end,
	// while the cause is a WRITER that changes metadata without going through the save the rest of
	// the product goes through (Max: you change a form, you change objects, and you go PAST the
	// standard save). In the Designer, editing a module and closing the document saves the metadata —
	// the digest moves, the cache row retires by itself. A door that skips that has to be fixed at
	// the door.
	const ibString configMd5 = owner != nullptr ? owner->GetConfigMD5() : ibString();

	const auto loadCached = [&]() {
		if (!ibByteCodeCache::Load(bc, meta->GetGuid(), configMd5))
			return false;
		if (bc.ResolveAndVerifyDependencies()) {
			// Restore live pointers AOT skipped on serialize. Without
			// m_parent runtime's parent-conformity check refuses the module.
			bc.m_parent = parent;

			// ⚠ NO COMPARISON AGAINST THE LOADED TABLE HERE. Checking the surface against
			// bc.m_listVar on every hit was written and taken back out the same day: it turns the
			// reader into the place that notices staleness, which is one place too late. A row that
			// no longer matches the configuration must not be READABLE at all — it is thrown away
			// where it goes stale (OnSaveMetaObject drops it on saveConfigFlag; the key retires
			// every row of a previous configuration digest), and this branch may then trust what it
			// gets (Max, 2026-09-04: a stale cache must be THROWN AWAY, not worked around).
			return true;
		}
		// (c) — dep registry missing the target or version drift.
		// Clear the stale row and let the recompile branch
		// repopulate.
		ibByteCodeCache::Invalidate(meta->GetGuid());
		bc.Reset();
		return false;
	};

	// An external report or data processor has no digest: a row under an empty one would outlive its file's text, and
	// saving it weeds out every row of the base's configuration (WriteRow). It compiles when it is opened.
	const bool hasDigest = !configMd5.IsEmpty();

	// ⭐ NOT WHAT WAS COMPILED UNDER AN EVAL — see below, where it is saved. It holds no row either.
	const bool cacheable = hasDigest && ibBackendException::IsEvalMode() == eval_none;

	// A miss holds the module's row: whoever misses with it stands in the database until this compile
	// has saved it, and reads it then (ibByteCodeCache::ibRowLock). Let go at the end of this scope if
	// the compile below did not get as far as the save, and the next one compiles.
	ibByteCodeCache::ibRowLock rowLock;
	bool ready = hasDigest && loadCached();
	if (!ready && cacheable) {
		rowLock.Acquire(meta->GetGuid(), configMd5);
		ready = loadCached();
	}

	if (!ready) {
		// ⭐ THE ONE PLACE A COMPILER IS MADE TO BE RUN: no bytecode, so it makes it and goes. Under a parent that is
		// bytecode alone there is one walk for a name (ibCompileContext::GetVariable, step 3), so one address
		// for it, whether the parent itself came from a compile or from the cache. There used to be two:
		// measured 2026-09-04, the first run after an apply posted a document and the next, the parent coming
		// from the cache, failed in the posting handler with "'ValueIsFilled' is a global function".
		ibCompileModule compiler(meta, *m_binds);
		compiler.SetParent(parent);
		if (!compiler.Compile())
			return false;
		// What stays is the base: the tree on the compiler's own form goes with the compiler (byteCode.h).
		bc = std::move(static_cast<ibByteCode&>(compiler.m_cByteCode));
		// Stamp the freshly-compiled bytecode with the descriptor
		// identity. m_id is the descriptor's GUID — stable across
		// renames in Designer; cache rows in sys_bytecode_cache key
		// off it. m_descriptorClsid records the kind (CommonModule /
		// FormModule / etc.) for sanity checks at AOT-load time and
		// for diag tooling.
		bc.m_id = meta->GetGuid();
		bc.m_descriptorClsid = meta->GetClassType();
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
		if (cacheable && !rowLock.Save(bc))
			ibByteCodeCache::Save(bc, configMd5);
	}

	// Publish bc in the process-wide registry. Dependents resolving
	// their m_dependencyIds via ibByteCode::Find should now see this
	// descriptor's live ibByteCode and the freshly-stamped m_version.
	// Idempotent — overwrites the slot (e.g. recompile after a
	// drift-induced invalidate publishes the new pointer + version).
	if (bc.m_id.isValid())
		ibByteCode::Register(&bc);

	return true;
}

void ibRuntimeModuleDataObject::Execute(bool delta)
{
	if (m_procUnit == nullptr || m_byteCode == nullptr || !m_byteCode->m_bCompile)
		return;

	// Made for this call: the frame takes the values out of it before the body runs (ibProcUnit::Execute).
	//
	// ⚠ A BOUND VALUE'S NAMES ARE NOT RESET HERE, nor in PrepareModuleData. They build themselves when first
	// asked, and a value whose names change says so itself. A session bringing its modules up has no business
	// resetting them: the system functions' table is one for the whole process (ibMemberTable::Shared), and a
	// login rebuilt it under every session reading it — ten logins at once and the process was gone (2026-10-31).
	ibByteBinder binder(m_byteCode->m_listVar, delta);
	for (const auto& kv : m_binds->m_listExternValue)
		binder.SetVar(kv.first, kv.second);
	for (const auto& kv : m_binds->m_listContextValue)
		binder.SetVar(kv.first, kv.second.m_value);
	// Bound locals (e.g. a constant's Value backed by &m_constValue): plain
	// writable frame slots — no PrepareNames (they're values, not surfaced
	// objects). To the binder a local is indistinguishable from an external:
	// both just seed a slot; IsBindable() unifies them in SetVar / pre-flight.
	for (const auto& kv : m_binds->m_listLocalValue)
		binder.SetVar(kv.first, kv.second);
	m_procUnit->Execute(*m_byteCode, binder);
}

void ibRuntimeModuleDataObject::SetParent(const ibRuntimeModuleDataObject* parent)
{
	m_parent = parent;

	// Cascade to the runtime layer so callers don't have to wire it
	// separately. Propagate only when both sides have a ProcUnit — parent
	// may be a system session that never runs scripts.
	if (m_procUnit != nullptr && parent != nullptr) {
		if (auto parentPu = parent->GetProcUnit())
			m_procUnit->SetParent(parentPu.get());
	}
}
