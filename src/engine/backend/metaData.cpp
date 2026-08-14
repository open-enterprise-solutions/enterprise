////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaData
////////////////////////////////////////////////////////////////////////////

#include "metaData.h"

#include "backend/metaCollection/metaModuleObject.h"
#include "backend/metaCollection/metaFormObject.h"
#include "backend/query/queryableFactory.h"   // ibQueryableFactory::Register/Unregister — the per-config source registry
#include "backend/query/schemaSnapshot.h"     // ibSchemaSnapshot — BuildSchemaSnapshot

#include <algorithm>
#include <cwctype>
#include <string>
#include <unordered_set>

//**************************************************************************************************
//*                          copy-aware identity resolve (metaId <-> guid)                          *
//**************************************************************************************************

ibGuid ibMetaData::GuidByMetaId(const ibMetaID& id) const
{
	const ibValueMetaObject* meta = FindAnyObjectByFilter(id, true);
	return meta != nullptr && meta->IsAllowed() ? meta->GetCommonGuid() : wxNullGuid;
}

ibMetaID ibMetaData::MetaIdByGuid(const ibGuid& guid) const
{
	if (!guid.isValid())
		return wxNOT_FOUND;
	const ibValueMetaObject* meta = FindAnyObjectByFilter(guid, true);
	return meta != nullptr && meta->IsAllowed() ? meta->GetMetaID() : wxNOT_FOUND;
}

// The whole structure this configuration declares — see the header. The walk
// itself belongs to the metaobjects (ibValueMetaObject::ContributeTables descends
// its children); this only names WHERE it starts, which is the one thing every
// caller was repeating.
ibSchemaSnapshot ibMetaData::BuildSchemaSnapshot() const
{
	ibSchemaSnapshot snapshot;
	if (const ibValueMetaObject* common = GetCommonMetaObject())
		common->ContributeTables(snapshot);
	return snapshot;
}

// Register the source into THIS config's OWN factory (the metaobject calls it on run: GetMetaData()->RegisterSource).
// Only metadata-backed sources register now; each config keeps its own set. No factory (closed) → no-op.
void ibMetaData::RegisterSource(ibQueryableSourceDescriptor* descriptor) const
{
	if (ibQueryableFactory* factory = GetSourceFactory())
		factory->Register(descriptor);
}

void ibMetaData::UnregisterSource(ibQueryableSourceDescriptor* descriptor) const
{
	if (ibQueryableFactory* factory = GetSourceFactory())
		factory->Unregister(descriptor);
}

//**************************************************************************************************
//*                                       ibModuleStorage                                          *
//**************************************************************************************************

bool ibModuleStorage::AddCommonModule(ibValueMetaObjectCommonModule* commonModule)
{
	if (commonModule == nullptr)
		return false;
	auto it = std::find(m_initModules.begin(), m_initModules.end(), commonModule);
	if (it == m_initModules.end())
		m_initModules.emplace_back(commonModule);
	return true;
}

bool ibModuleStorage::RenameCommonModule(ibValueMetaObjectCommonModule* /*commonModule*/, const wxString& /*newName*/)
{
	// Descriptor name is owned by the descriptor itself; storage indexes
	// by raw pointer, so rename is a no-op. Kept on the API as a single
	// notification point — runtime mm picks up name changes on its own
	// during compile.
	return true;
}

bool ibModuleStorage::RemoveCommonModule(ibValueMetaObjectCommonModule* commonModule)
{
	if (commonModule == nullptr)
		return false;
	auto it = std::find(m_initModules.begin(), m_initModules.end(), commonModule);
	if (it != m_initModules.end())
		m_initModules.erase(it);
	return true;
}

//**************************************************************************************************
//*                                    ibCompileValueCache                                         *
//**************************************************************************************************

// Out-of-line — the complete ibValueModuleManagerDesigner type is available here
// (moduleManager.h fully included), so the ibValuePtr<ibValueModuleManagerDesigner>
// assign/convert (a ref-counting static_cast through ibValue*) compiles. In
// metaData.h the type is only forward-visible under the moduleManager.h cycle.
ibCompileValueCache::ibCompileValueCache(ibValueModuleManagerDesigner* moduleManager)
	: m_moduleManager(moduleManager)
{
}

ibValueModuleManagerDesigner* ibCompileValueCache::GetModuleManager() const
{
	return m_moduleManager;
}

void ibCompileValueCache::SetModuleManager(ibValueModuleManagerDesigner* moduleManager)
{
	m_moduleManager = moduleManager;
}

bool ibCompileValueCache::AddCompileModule(const ibValueMetaObject* moduleObject, ibValue* object)
{
	if (object == nullptr)
		return true;
	auto it = m_cache.find(moduleObject);
	if (it == m_cache.end()) {
		ibCompileEntry entry;
		entry.m_value = ibValuePtr<ibValue>(object);
		m_cache.emplace(moduleObject, std::move(entry));
		return true;
	}
	return false;
}

bool ibCompileValueCache::AddCompileModule(const ibValueMetaObject* moduleObject, std::function<ibValue*()> builder)
{
	if (moduleObject == nullptr)
		return false;
	// Insert/replace — second registration for the same descriptor (e.g.
	// after a designer rename + re-run) overrides the prior rebuilder
	// and drops any built value so the next Find rebuilds from scratch.
	ibCompileEntry entry;
	entry.m_deferred = std::move(builder);
	m_cache.insert_or_assign(moduleObject, std::move(entry));
	return true;
}

bool ibCompileValueCache::RemoveCompileModule(const ibValueMetaObject* moduleObject)
{
	auto it = m_cache.find(moduleObject);
	if (it != m_cache.end()) {
		m_cache.erase(it);
		return true;
	}

	return false;
}

bool ibCompileValueCache::InvalidateCompileModule(const ibValueMetaObject* moduleObject)
{
	auto it = m_cache.find(moduleObject);
	if (it == m_cache.end()) return false;

	// No rebuilder — entry was registered as an already-built value
	// (e.g. catalog/document module). Nothing to invalidate; rebuild
	// requires a fresh AddCompileModule(meta, value) by the registering
	// side.
	if (!it->second.m_deferred)
		return false;

	// Drop the cached built value; rebuilder stays so the next Find
	// triggers a Construct.
	it->second.m_value = ibValuePtr<ibValue>();
	return true;
}

ibValue* ibCompileValueCache::FindCompileModuleRef(const ibValueMetaObject* moduleObject) const
{
	auto it = m_cache.find(moduleObject);
	if (it == m_cache.end())
		return nullptr;

	// Cached built value — return directly.
	if (it->second.m_value)
		return &(*it->second.m_value);

	// Pending or invalidated — try the rebuilder. mm should be ready by
	// the time the first lookup arrives; on Construct failure the cache
	// entry is dropped to avoid retrying on every subsequent lookup.
	if (!it->second.m_deferred)
		return nullptr;

	// Re-entrancy guard: Construct internally calls
	// CreateObjectForm → CreateAndBuildForm → back into FindCompileModule
	// for the same creator. Without this guard the lookup would trigger
	// another Construct on the still-empty entry and recurse forever
	// (stack-overflow on first new-form add).
	if (it->second.m_constructing)
		return nullptr;
	it->second.m_constructing = true;

	ibValue* value = it->second.m_deferred();

	// Re-find: Construct may have called Invalidate / erase along the way.
	it = m_cache.find(moduleObject);
	if (it == m_cache.end())
		return value;  // cache cleared; just return what we built (may be null)

	it->second.m_constructing = false;
	if (value == nullptr) {
		m_cache.erase(it);
		return nullptr;
	}
	it->second.m_value = ibValuePtr<ibValue>(value);
	return value;
}

ibValue* ibCompileValueCache::FindParentCompileModuleRef(const ibValueMetaObject* moduleObject) const
{
	ibValueMetaObject* parent = moduleObject ? moduleObject->GetParent() : nullptr;
	return parent ? FindCompileModuleRef(parent) : nullptr;
}

//**************************************************************************************************
//*                                          ibMetaData											   *
//**************************************************************************************************

//ID's 
ibMetaID ibMetaData::GenerateNewID() const
{
	// SEED ONCE, THEN COUNT. The walk still decides where the numbering starts — a configuration
	// just loaded must continue past its highest existing id — but it runs once per open image
	// instead of once per new object, and what it produces is a floor, not an answer.
	if (m_nextMetaId == 0) {
		const ibValueMetaObject* commonObject = GetCommonMetaObject();
		wxASSERT(commonObject);
		ibMetaID id = commonObject->GetMetaID() + 1;
		DoGenerateNewID(id, commonObject);
		m_nextMetaId = id;

		// The seed is the whole question: it is the walk's answer about what the tree CURRENTLY holds,
		// and an id already given to something the walk cannot see will be handed out a second time.
	}

	// ⚠ NEVER BACKWARDS. An id whose object was deleted does NOT return to the pool, and that is the
	// whole point: the physical column is named `fld<id>`, so re-issuing an id re-issues a column
	// NAME the database may still be holding. See the note on the declaration.
	return m_nextMetaId++;
}

void ibMetaData::DoGenerateNewID(ibMetaID& id, const ibValueMetaObject* top) const
{
	for (unsigned int idx = 0; idx < top->GetChildCount(); idx++) {
		ibValueMetaObject* child = top->GetChild(idx);
		wxASSERT(child);
		ibMetaID newID = child->GetMetaID() + 1;
		if (newID > id) {
			id = newID;
		}
		DoGenerateNewID(id, child);
	}
}

ibValueMetaObject* ibMetaData::CreateMetaObject(const ibClassID& clsidIn, ibValueMetaObject* parent, bool runObject)
{
	// Resolve the requested clsid against the owner: a tabular section's RAM (MD_TBL) and
	// DB-backed reference (MD_TBLR) variants are the same kind, so the owner remaps a pasted
	// child to ITS variant (ResolveChild). A positive result remaps; 0 (owner has no opinion)
	// leaves the clsid as asked — this only fixes cross-owner equivalents, it never rejects.
	ibClassID clsid = clsidIn;
	if (parent != nullptr) {
		const ibClassID resolved = parent->ResolveChild(clsidIn);
		if (resolved > 0)
			clsid = resolved;
	}

	wxASSERT(clsid != 0);

	ibValue* ppParams[] = { parent };
	ibValueMetaObject* newMetaObject = nullptr;

	try {
		// AddChild (inside Init via ppParams[0]=parent) takes the owning reference.
		newMetaObject = ibValue::CreateAndConvertObjectRef<ibValueMetaObject>(clsid, ppParams, 1);
	}
	catch (...) {
		return nullptr;
	}

	if (newMetaObject != nullptr) {

		newMetaObject->SetName(
			GetNewName(clsid, parent, newMetaObject->GetClassName())
		);

		//always create meta object
		bool success = newMetaObject->OnCreateMetaObject(this, runObject ? newObjectFlag : pasteObjectFlag);

		//first initialization
		if (!success || !newMetaObject->OnLoadMetaObject(this)) {
			if (parent != nullptr)
				parent->RemoveChild(newMetaObject); // owning vector releases → destroys
			else
				wxDELETE(newMetaObject); // never attached (no parent) — destroy directly
			return nullptr;
		}

		//and running initialization
		if (runObject && (!success || !newMetaObject->OnBeforeRunMetaObject(newObjectFlag))) {
			if (parent != nullptr)
				parent->RemoveChild(newMetaObject); // owning vector releases → destroys
			else
				wxDELETE(newMetaObject); // never attached (no parent) — destroy directly
			return nullptr;
		}

		Modify(true);

		if (runObject && (!success || !newMetaObject->OnAfterRunMetaObject(newObjectFlag))) {
			if (parent != nullptr)
				parent->RemoveChild(newMetaObject); // owning vector releases → destroys
			else
				wxDELETE(newMetaObject); // never attached (no parent) — destroy directly
			return nullptr;
		}
	}

	return newMetaObject;
}

wxString ibMetaData::GetNewName(const ibClassID& clsid, ibValueMetaObject* parent, const wxString& strPrefix, bool forConstructor)
{
	const wxString currPrefix = strPrefix.Length() > 0 ?
		strPrefix : wxT("newItem");

	// Normalise to upper case, matching stringUtils::CompareString's
	// case-insensitive semantics, so set lookups are exact comparisons.
	const auto toKey = [](const wxString& name) {
		std::wstring key = name.ToStdWstring();
		std::transform(key.begin(), key.end(), key.begin(), ::towupper);
		return key;
	};

	// Collect the sibling names already taken by objects of this class
	// once, so each candidate is probed in O(1). The old loop rescanned
	// every child for each candidate suffix — O(children * candidates)
	// per call. FilterChild(clsid) is invariant here (every surviving
	// child has class == clsid), so it is hoisted out of the scan.
	std::unordered_set<std::wstring> takenNames;
	if (parent != nullptr && parent->FilterChild(clsid)) {

		for (unsigned int idx = 0; idx < parent->GetChildCount(); idx++) {

			auto child = parent->GetChild(idx);
			if (clsid != child->GetClassType())
				continue;
			if (child->IsDeleted())
				continue;

			takenNames.insert(toKey(child->GetName()));
		}
	}

	// forConstructor allows the bare prefix as the first candidate;
	// otherwise numbering starts at 1.
	unsigned int countRec = forConstructor ? 0 : 1;
	wxString newName = forConstructor ?
		currPrefix : wxString::Format(wxT("%s%d"), currPrefix, countRec);

	while (takenNames.count(toKey(newName)) > 0)
		newName = wxString::Format(wxT("%s%d"), currPrefix, ++countRec);

	return newName;
}

bool ibMetaData::RenameMetaObject(ibValueMetaObject* metaObject, const wxString& newName)
{
	bool foundedName = false;

	for (const auto object : GetAnyArrayObject(metaObject->GetClassType())) {
		if (object->GetParent() != metaObject->GetParent())
			continue;
		if (object != metaObject &&
			stringUtils::CompareString(newName, object->GetName())) {
			foundedName = true;
			break;
		}
	}

	if (foundedName) return false;

	if (metaObject->OnRenameMetaObject(newName)) {
		Modify(true); return true;
	}
	return false;
}

void ibMetaData::RemoveMetaObject(ibValueMetaObject* object, ibValueMetaObject* parent)
{
	// Both close phases — symmetric with CreateMetaObject's Before+After run phases.
	// The before-hook carries the real teardown (RemoveCompileModule /
	// RemoveCommonModule from the compile cache + module storage); skipping it left
	// stale module registrations that later asserted on an "empty registry".
	if (!object->OnBeforeCloseMetaObject())
		return;
	if (object->OnAfterCloseMetaObject()) {
		if (object->OnDeleteMetaObject()) {
			object->MarkAsDeleted();
			for (unsigned int idx = 0; idx < object->GetChildCount(); idx++) {
				auto child = object->GetChild(idx);
				if (!object->FilterChild(child->GetClassType()))
					continue;
				RemoveMetaObject(child, object);
			}
		}
		object->OnReloadMetaObject();
		Modify(true);
	}
}